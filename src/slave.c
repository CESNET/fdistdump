/** Slave process functionality.
 */

/*
 * Copyright (C) 2015 CESNET
 *
 * LICENSE TERMS
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of the Company nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * ALTERNATIVELY, provided that this notice is retained in full, this
 * product may be distributed under the terms of the GNU General Public
 * License (GPL) version 2 or later, in which case the provisions
 * of the GPL apply INSTEAD OF those given above.
 *
 * This software is provided ``as is'', and any express or implied
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose are disclaimed.
 * In no event shall the company or contributors be liable for any
 * direct, indirect, incidental, special, exemplary, or consequential
 * damages (including, but not limited to, procurement of substitute
 * goods or services; loss of use, data, or profits; or business
 * interruption) however caused and on any theory of liability, whether
 * in contract, strict liability, or tort (including negligence or
 * otherwise) arising in any way out of the use of this software, even
 * if advised of the possibility of such damage.
 */

#include "common.h"
#include "slave.h"
#include "path_array.h"
#include "print.h"
#ifdef HAVE_LIBBFINDEX
#include "bfindex.h"
#endif  // HAVE_LIBBFINDEX

#include <string.h> //strlen()
#include <assert.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <limits.h> //PATH_MAX
#include <unistd.h> //access

#ifdef _OPENMP
#include <omp.h>
#endif //_OPENMP
#include <mpi.h>
#include <libnf.h>
#include <ffilter.h>
#include <dirent.h> //list directory
#include <sys/stat.h> //stat()


#if LNF_MAX_RAW_LEN > XCHG_BUFF_SIZE
#error "LNF_MAX_RAW_LEN > XCHG_BUFF_SIZE"
#endif

#if LNF_MAX_RAW_LEN > UINT32_MAX
#error "LNF_MAX_RAW_LEN > UINT32_MAX"
#endif


/*
 * Global variables.
 */
static const struct cmdline_args *args;


/*
 * Data types declarations.
 */
struct slave_ctx {  // thread-shared context
    lnf_mem_t *lnf_mem;  // libnf memory used for aggregation
    lnf_filter_t *filter; // libnf compiled filter expression
#ifdef HAVE_LIBBFINDEX
    struct bfindex_node *bfindex_root;  // indexing IP address tree root
                                        // (created from the the libnf filter)
#endif  // HAVE_LIBBFINDEX
    uint8_t *buff[2];  // two chunks of memory for the record storage
    size_t proc_rec_cntr;  // processed record counter
    bool rec_limit_reached; // true if rec_limit records has been read
    size_t slave_cnt;  // slave count
    struct processed_summ processed_summ;  // summary of processed records
    struct metadata_summ metadata_summ;    // summary of flow files metadata
};

struct thread_ctx {  // thread-private context
    lnf_file_t *file;  // libnf file
    lnf_rec_t *lnf_rec;    // libnf record

    uint8_t *buff[2];  // two chunks of memory for the record storage
    struct processed_summ processed_summ;  // summary of processed records
    struct metadata_summ metadata_summ;    // summary of flow files metadata
};


/*
 * Static functions.
 */
/**
 * @brief TODO
 *
 * @param private
 * @param lnf_rec
 */
static void
processed_summ_update(struct processed_summ *private, lnf_rec_t *lnf_rec)
{
    assert(private && lnf_rec);

    uint64_t tmp;

    lnf_rec_fget(lnf_rec, LNF_FLD_AGGR_FLOWS, &tmp);
    private->flows += tmp;

    lnf_rec_fget(lnf_rec, LNF_FLD_DPKTS, &tmp);
    private->pkts += tmp;

    lnf_rec_fget(lnf_rec, LNF_FLD_DOCTETS, &tmp);
    private->bytes += tmp;
}

/**
 * @brief TODO
 *
 * @param shared
 * @param private
 */
static void
processed_summ_share(struct processed_summ *shared,
                     const struct processed_summ *private)
{
    assert(shared && private);

    #pragma omp atomic
    shared->flows += private->flows;
    #pragma omp atomic
    shared->pkts += private->pkts;
    #pragma omp atomic
    shared->bytes += private->bytes;
}

/**
 * @brief TODO
 *
 * Read to the temporary variable, check validity, and update private counsters.
 *
 * @param private
 * @param file
 */
static void
metadata_summ_update(struct metadata_summ *private, lnf_file_t *file)
{
    assert(private && file);

    struct metadata_summ tmp;
    int lnf_ret;

    // flows
    lnf_ret |= lnf_info(file, LNF_INFO_FLOWS, &tmp.flows, sizeof (tmp.flows));
    lnf_ret |= lnf_info(file, LNF_INFO_FLOWS_TCP, &tmp.flows_tcp,
                        sizeof (tmp.flows_tcp));
    lnf_ret |= lnf_info(file, LNF_INFO_FLOWS_UDP, &tmp.flows_udp,
                        sizeof (tmp.flows_udp));
    lnf_ret |= lnf_info(file, LNF_INFO_FLOWS_ICMP, &tmp.flows_icmp,
                        sizeof (tmp.flows_icmp));
    lnf_ret |= lnf_info(file, LNF_INFO_FLOWS_OTHER, &tmp.flows_other,
                        sizeof (tmp.flows_other));
    assert(lnf_ret = LNF_OK);

    if (tmp.flows != tmp.flows_tcp + tmp.flows_udp + tmp.flows_icmp
            + tmp.flows_other) {
        PRINT_WARNING(E_LNF, 0, "metadata flow count mismatch "
                      "(total != TCP + UDP + ICMP + other)");
    }

    private->flows += tmp.flows;
    private->flows_tcp += tmp.flows_tcp;
    private->flows_udp += tmp.flows_udp;
    private->flows_icmp += tmp.flows_icmp;
    private->flows_other += tmp.flows_other;

    // packets
    lnf_ret |= lnf_info(file, LNF_INFO_PACKETS, &tmp.pkts, sizeof (tmp.pkts));
    lnf_ret |= lnf_info(file, LNF_INFO_PACKETS_TCP, &tmp.pkts_tcp,
                        sizeof (tmp.pkts_tcp));
    lnf_ret |= lnf_info(file, LNF_INFO_PACKETS_UDP, &tmp.pkts_udp,
                        sizeof (tmp.pkts_udp));
    lnf_ret |= lnf_info(file, LNF_INFO_PACKETS_ICMP, &tmp.pkts_icmp,
                        sizeof (tmp.pkts_icmp));
    lnf_ret |= lnf_info(file, LNF_INFO_PACKETS_OTHER, &tmp.pkts_other,
                        sizeof (tmp.pkts_other));
    assert(lnf_ret = LNF_OK);

    if (tmp.pkts != tmp.pkts_tcp + tmp.pkts_udp + tmp.pkts_icmp
            + tmp.pkts_other) {
        PRINT_WARNING(E_LNF, 0, "metadata packet count mismatch "
                      "(total != TCP + UDP + ICMP + other)");
    }

    private->pkts += tmp.pkts;
    private->pkts_tcp += tmp.pkts_tcp;
    private->pkts_udp += tmp.pkts_udp;
    private->pkts_icmp += tmp.pkts_icmp;
    private->pkts_other += tmp.pkts_other;

    // bytes
    lnf_ret |= lnf_info(file, LNF_INFO_BYTES, &tmp.bytes, sizeof (tmp.bytes));
    lnf_ret |= lnf_info(file, LNF_INFO_BYTES_TCP, &tmp.bytes_tcp,
                        sizeof (tmp.bytes_tcp));
    lnf_ret |= lnf_info(file, LNF_INFO_BYTES_UDP, &tmp.bytes_udp,
                        sizeof (tmp.bytes_udp));
    lnf_ret |= lnf_info(file, LNF_INFO_BYTES_ICMP, &tmp.bytes_icmp,
                        sizeof (tmp.bytes_icmp));
    lnf_ret |= lnf_info(file, LNF_INFO_BYTES_OTHER, &tmp.bytes_other,
                        sizeof (tmp.bytes_other));
    assert(lnf_ret = LNF_OK);

    if (tmp.bytes != tmp.bytes_tcp + tmp.bytes_udp + tmp.bytes_icmp
            + tmp.bytes_other) {
        PRINT_WARNING(E_LNF, 0, "metadata bytes count mismatch "
                      "(total != TCP + UDP + ICMP + other)");
    }

    private->bytes += tmp.bytes;
    private->bytes_tcp += tmp.bytes_tcp;
    private->bytes_udp += tmp.bytes_udp;
    private->bytes_icmp += tmp.bytes_icmp;
    private->bytes_other += tmp.bytes_other;
}

/**
 * @brief TODO
 *
 * @param shared
 * @param private
 */
static void
metadata_summ_share(struct metadata_summ *shared,
                    const struct metadata_summ *private)
{
    assert(shared && private);

    #pragma omp atomic
    shared->flows += private->flows;
    #pragma omp atomic
    shared->flows_tcp += private->flows_tcp;
    #pragma omp atomic
    shared->flows_udp += private->flows_udp;
    #pragma omp atomic
    shared->flows_icmp += private->flows_icmp;
    #pragma omp atomic
    shared->flows_other += private->flows_other;

    #pragma omp atomic
    shared->pkts += private->pkts;
    #pragma omp atomic
    shared->pkts_tcp += private->pkts_tcp;
    #pragma omp atomic
    shared->pkts_udp += private->pkts_udp;
    #pragma omp atomic
    shared->pkts_icmp += private->pkts_icmp;
    #pragma omp atomic
    shared->pkts_other += private->pkts_other;

    #pragma omp atomic
    shared->bytes += private->bytes;
    #pragma omp atomic
    shared->bytes_tcp += private->bytes_tcp;
    #pragma omp atomic
    shared->bytes_udp += private->bytes_udp;
    #pragma omp atomic
    shared->bytes_icmp += private->bytes_icmp;
    #pragma omp atomic
    shared->bytes_other += private->bytes_other;
}


/**
 * @brief TODO
 *
 * Lack of the MPI_THREAD_MULTIPLE threading level implies the CS.
 *
 * @param data
 * @param data_size
 * @param req
 */
static void
wait_isend_cs(void *data, size_t data_size, MPI_Request *req)
{
    assert(data && data_size > 0 && req);

    #pragma omp critical (mpi)
    {
        MPI_Wait(req, MPI_STATUS_IGNORE);
        MPI_Isend(data, data_size, MPI_BYTE, ROOT_PROC, TAG_DATA,
                  MPI_COMM_WORLD, req);
    }
}

/**
 * @brief TODO
 *
 * Read all records from the file. No aggregation is performed, records are only
 * saved into the record buffer. When the buffer is full, it is sent towards the
 * master.
 *
 * @param s_ctx
 * @param t_ctx
 *
 * @return 
 */
static error_code_t
read_and_send_file(struct slave_ctx *s_ctx, struct thread_ctx *t_ctx)
{
    assert(s_ctx && t_ctx);

    // fill the fast fields array and calculate constant record size
    struct {
        int id;
        size_t size;
    } fast_fields[LNF_FLD_TERM_];//fields array compressed for faster access
    size_t fast_fields_cnt = 0;
    uint32_t rec_size = 0;
    for (size_t i = 0; i < LNF_FLD_TERM_; ++i) {
        if (args->fields[i].id == 0) {
            continue;  // the field is not used
        }
        fast_fields[fast_fields_cnt].id = i;
        rec_size += fast_fields[fast_fields_cnt].size = field_get_size(i);
        fast_fields_cnt++;
    }

    // loop through all records, HOT PATH!
    size_t file_rec_cntr = 0;
    size_t file_proc_rec_cntr = 0;
    size_t file_sent_bytes = 0;
    bool buff_idx = 0; //index to the currently used data buffer
    size_t buff_off = 0; //current data buffer offset
    size_t buff_rec_cntr = 0; //number of records in the current buffer
    MPI_Request request = MPI_REQUEST_NULL;
    int lnf_ret;
    while ((lnf_ret = lnf_read(t_ctx->file, t_ctx->lnf_rec)) == LNF_OK) {
        file_rec_cntr++;

        // try to match the filter (if there is one)
        if (s_ctx->filter && !lnf_filter_match(s_ctx->filter, t_ctx->lnf_rec)) {
            continue;
        }
        file_proc_rec_cntr++;

        // check if there is enough space in the buffer for the next record
        if (buff_off + rec_size + sizeof (rec_size) > XCHG_BUFF_SIZE) {
            // break if the record limit has been reached by ANOTHER thread
            #pragma omp flush
            if (s_ctx->rec_limit_reached) {
                buff_rec_cntr = 0;
                break;
            }

            wait_isend_cs(t_ctx->buff[buff_idx], buff_off, &request);
            file_sent_bytes += buff_off;

            // increment the thread-shared counter of processed records
            #pragma omp atomic
            s_ctx->proc_rec_cntr += buff_rec_cntr;

            // clear the buffer context variables and toggle the buffers
            buff_off = 0;
            buff_rec_cntr = 0;
            buff_idx = !buff_idx;

            // break if the record limit has been reached by THIS thread
            if (args->rec_limit && s_ctx->proc_rec_cntr >= args->rec_limit) {
                s_ctx->rec_limit_reached = true;
                #pragma omp flush
                break; //record limit reached by this thread
            }
        }

        // update the thread-private processed summary counters
        processed_summ_update(&t_ctx->processed_summ, t_ctx->lnf_rec);

        // write the 4 byte long record size before each record
        *(uint32_t *)(t_ctx->buff[buff_idx] + buff_off) = rec_size;
        buff_off += sizeof (rec_size);

        // loop through the fields in the record and fill the data buffer
        for (size_t i = 0; i < fast_fields_cnt; ++i) {
            lnf_rec_fget(t_ctx->lnf_rec, fast_fields[i].id,
                         t_ctx->buff[buff_idx] + buff_off);
            buff_off += fast_fields[i].size;
        }

        buff_rec_cntr++;
    }

    // send the remaining records if the record buffer is not empty
    if (buff_rec_cntr != 0) {
        wait_isend_cs(t_ctx->buff[buff_idx], buff_off, &request);
        file_sent_bytes += buff_off;

        // increment the thread-shared counter of processed records
        #pragma omp atomic
        s_ctx->proc_rec_cntr += buff_rec_cntr;
    }

    // either set the record limit reached flag or check if EOF was reached
    if (args->rec_limit && s_ctx->proc_rec_cntr >= args->rec_limit) {
        s_ctx->rec_limit_reached = true;
        #pragma omp flush
    } else if (lnf_ret != LNF_EOF) {
        PRINT_WARNING(E_LNF, lnf_ret, "failed to read the whole file");
    }

    // the buffers will be invalid after return, wait for the send to complete
    #pragma omp critical (mpi)
    {
        MPI_Wait(&request, MPI_STATUS_IGNORE);
    }

    PRINT_DEBUG("read %zu, processed %zu, sent %zu B", file_rec_cntr,
                file_proc_rec_cntr, file_sent_bytes);

    return E_OK;
}

/**
 * @brief TODO
 *
 * Read all records from the file. Aggreagation is performed (records are
 * written to the libnf memory, which is a hash table). The record limit is
 * ignored.
 *
 * @param s_ctx
 * @param t_ctx
 *
 * @return 
 */
static error_code_t
read_and_store_file(struct slave_ctx *s_ctx, struct thread_ctx *t_ctx)
{
    assert(s_ctx && t_ctx);

    error_code_t ecode = E_OK;

    // loop through all records, HOT PATH!
    int lnf_ret;
    size_t file_rec_cntr = 0;
    size_t file_proc_rec_cntr = 0;
    while ((lnf_ret = lnf_read(t_ctx->file, t_ctx->lnf_rec)) == LNF_OK) {
        file_rec_cntr++;

        // try to match the filter (if there is one)
        if (s_ctx->filter && !lnf_filter_match(s_ctx->filter, t_ctx->lnf_rec)) {
            continue;
        }
        file_proc_rec_cntr++;

        // update the thread-private processed summary counters
        processed_summ_update(&t_ctx->processed_summ, t_ctx->lnf_rec);

        // write the record into the libnf memory (a hash table)
        lnf_ret = lnf_mem_write(s_ctx->lnf_mem, t_ctx->lnf_rec);
        if (lnf_ret != LNF_OK) {
            ecode = E_LNF;
            PRINT_ERROR(ecode, lnf_ret, "lnf_mem_write()");
            break;
        }
    }
    // check if EOF was reached
    if (lnf_ret != LNF_EOF) {
        PRINT_WARNING(E_LNF, lnf_ret, "EOF wasn not reached");
    }

    PRINT_DEBUG("read %zu, processed %zu", file_rec_cntr, file_proc_rec_cntr);
    return ecode;
}


/**
 * @brief TODO
 *
 * Each record is prefixed with its length:
 * | xchg_rec_size_t rec_len | rec_len bytes sized record |
 *
 * @param lnf_mem
 * @param limit
 * @param buff
 * @param buff_size
 *
 * @return 
 */
static error_code_t
send_raw_mem(lnf_mem_t *const lnf_mem, size_t rec_limit, uint8_t *const buff[2],
             const size_t buff_size)
{
    assert(lnf_mem && buff && buff[0] && buff[1]
           && buff_size >= sizeof (xchg_rec_size_t) + LNF_MAX_RAW_LEN);

    error_code_t ecode = E_OK;

    // zero record limit means send all records
    if (rec_limit == 0) {
        rec_limit = SIZE_MAX;
    }

    // initialize the cursor to point to the first record in the memory
    lnf_mem_cursor_t *cursor;
    int lnf_ret = lnf_mem_first_c(lnf_mem, &cursor);

    // loop throught all records
    bool buff_idx = 0;        // currently used data buffer index
    size_t buff_off = 0;      // data buffer offset
    size_t buff_rec_cntr = 0; // number of records in current buffer
    size_t rec_cntr = 0;
    MPI_Request request = MPI_REQUEST_NULL;
    while (cursor && rec_limit > rec_cntr) {
        // read another record, write it into the record buffer
        // write the 4 byte long record size before the record
        xchg_rec_size_t *const rec_size_ptr =
            (xchg_rec_size_t *)(buff[buff_idx] + buff_off);
        char *const rec_data_ptr =
            (char *)rec_size_ptr + sizeof (xchg_rec_size_t);
        const size_t buff_remaining =
            buff_size - buff_off - sizeof (xchg_rec_size_t);

        lnf_ret = lnf_mem_read_raw_c(lnf_mem, cursor, rec_data_ptr,
                                     (int *)rec_size_ptr, buff_remaining);
        assert(lnf_ret != LNF_EOF);

        // was in the buffer enough space for the record?
        if (lnf_ret != LNF_ERR_NOMEM) {  // yes
            buff_off += sizeof (xchg_rec_size_t) + *rec_size_ptr;
            buff_rec_cntr++;
            rec_cntr++;

            // move the cursor to the next record
            lnf_ret = lnf_mem_next_c(lnf_mem, &cursor);
        } else {  // no, send the full buffer
            MPI_Wait(&request, MPI_STATUS_IGNORE);
            MPI_Isend(buff[buff_idx], buff_off, MPI_BYTE, ROOT_PROC, TAG_DATA,
                      MPI_COMM_WORLD, &request);

            // clear the buffer context variables and toggle the buffers
            buff_off = 0;
            buff_rec_cntr = 0;
            buff_idx = !buff_idx;
        }
    }
    if (rec_limit == SIZE_MAX && lnf_ret != LNF_EOF) {
        ecode = E_LNF;
        PRINT_ERROR(ecode, lnf_ret, "lnf_mem_next_c() or lnf_mem_first_c()");
    }

    // send the remaining records if the record buffer is not empty
    if (buff_rec_cntr != 0) {
        MPI_Wait(&request, MPI_STATUS_IGNORE);
        MPI_Isend(buff[buff_idx], buff_off, MPI_BYTE, ROOT_PROC, TAG_DATA,
                  MPI_COMM_WORLD, &request);
    }

    // the buffers will be invalid after return, wait for the send to complete
    MPI_Wait(&request, MPI_STATUS_IGNORE);

    PRINT_DEBUG("send_raw_mem: sent %zu record(s)", rec_cntr);
    return ecode;
}

static void
send_raw_mem_ng(lnf_mem_t *const lnf_mem, size_t rec_limit, int mpi_tag,
                uint8_t *const buff[2], const size_t buff_size)
{
    assert(lnf_mem && buff && buff[0] && buff[1]
           && buff_size >= sizeof (xchg_rec_size_t) + LNF_MAX_RAW_LEN);

    // zero record limit means send all records
    if (rec_limit == 0) {
        rec_limit = SIZE_MAX;
    }

    // initialize the cursor to point to the first record in the memory
    lnf_mem_cursor_t *cursor;
    int lnf_ret = lnf_mem_first_c(lnf_mem, &cursor);

    // loop throught all records
    bool buff_idx = 0;        // currently used data buffer index
    size_t buff_off = 0;      // data buffer offset
    size_t buff_rec_cntr = 0; // number of records in current buffer
    size_t rec_cntr = 0;
    MPI_Request request = MPI_REQUEST_NULL;
    while (cursor && rec_limit > rec_cntr) {
        // read another record, write it into the record buffer
        // write the 4 byte long record size before the record
        xchg_rec_size_t *const rec_size_ptr =
            (xchg_rec_size_t *)(buff[buff_idx] + buff_off);
        char *const rec_data_ptr =
            (char *)rec_size_ptr + sizeof (xchg_rec_size_t);
        const size_t buff_remaining =
            buff_size - buff_off - sizeof (xchg_rec_size_t);

        lnf_ret = lnf_mem_read_raw_c(lnf_mem, cursor, rec_data_ptr,
                                     (int *)rec_size_ptr, buff_remaining);
        assert(lnf_ret != LNF_EOF);

        // was in the buffer enough space for the record?
        if (lnf_ret != LNF_ERR_NOMEM) {  // yes
            buff_off += sizeof (xchg_rec_size_t) + *rec_size_ptr;
            buff_rec_cntr++;
            rec_cntr++;

            // move the cursor to the next record
            lnf_ret = lnf_mem_next_c(lnf_mem, &cursor);
        } else {  // no, send the full buffer
            MPI_Wait(&request, MPI_STATUS_IGNORE);
            MPI_Isend(buff[buff_idx], buff_off, MPI_BYTE, ROOT_PROC, mpi_tag,
                      MPI_COMM_WORLD, &request);

            // clear the buffer context variables and toggle the buffers
            buff_off = 0;
            buff_rec_cntr = 0;
            buff_idx = !buff_idx;
        }
    }
    if (rec_limit == SIZE_MAX && lnf_ret != LNF_EOF) {
        PRINT_ERROR(E_LNF, lnf_ret, "lnf_mem_next_c() or lnf_mem_first_c()");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // send the remaining records if the record buffer is not empty
    if (buff_rec_cntr != 0) {
        MPI_Wait(&request, MPI_STATUS_IGNORE);
        MPI_Isend(buff[buff_idx], buff_off, MPI_BYTE, ROOT_PROC, mpi_tag,
                  MPI_COMM_WORLD, &request);
    }

    // the buffers will be invalid after return, wait for the send to complete
    MPI_Wait(&request, MPI_STATUS_IGNORE);

    // send the termiantor -- a message of zero length
    MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, mpi_tag, MPI_COMM_WORLD);

    PRINT_DEBUG("send_raw_mem_ng: sent %zu record(s) with tag %d", rec_cntr,
                mpi_tag);
}


/**
 * @brief TODO
 *
 * @param files_cnt
 */
static void
progress_report_init(size_t files_cnt)
{
    MPI_Gather(&files_cnt, 1, MPI_UNSIGNED_LONG, NULL, 0, MPI_UNSIGNED_LONG,
               ROOT_PROC, MPI_COMM_WORLD);
}

/**
 * @brief TODO
 */
static void
progress_report_next(void)
{
    #pragma omp critical (mpi)
    {
        MPI_Request request = MPI_REQUEST_NULL;
        MPI_Isend(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_PROGRESS,
                MPI_COMM_WORLD, &request);
        MPI_Request_free(&request);
    }
}

/**
 * @defgroup slave_tput Slaves's side of the TPUT Top-N algorithm.
 * For more information see @ref master_tput.
 * @{
 */
/**
 * @brief Slave's TPUT phase 1: establish a lower bound on the true bottom.
 *
 * Each slave sends the top N items from its memory.
 *
 * @param[in] s_ctx Slave's all-purpose context.
 */
static void
tput_phase_1(struct slave_ctx *const s_ctx)
{
    assert(s_ctx);

    // send the top N items from the sorted list
    send_raw_mem_ng(s_ctx->lnf_mem, args->rec_limit, TAG_TPUT1, s_ctx->buff,
                    XCHG_BUFF_SIZE);

    PRINT_DEBUG("slave TPUT phase 1: done");
}

/**
 * @brief Slaves's TPUT phase 2: find number of records satisfying the threshold.
 *
 * Those are records where:
 *   - the sort field value >= threshold if the direction is descending, or
 *   - the sort field value <= threshold if the direction is ascending.
 *
 * @param[in] lnf_mem The libnf memory. Will not be modified.
 * @param[in] threshold Threshold received from the master.
 * @param[in] key A libnf field which acts as a sort key.
 * @param[in] direction A libnf sort direction.
 *
 * @return Number of records satisfying the threshold. Always 0 if the libnf
 * memory is empty.
 */
static uint64_t
tput_phase_2_find_threshold_cnt(lnf_mem_t *lnf_mem, const uint64_t threshold,
                                const int key, const int direction)
{
    assert(lnf_mem);

    // initialize the cursor to point to the first record
    lnf_mem_cursor_t *cursor;
    int lnf_ret = lnf_mem_first_c(lnf_mem, &cursor);
    assert((cursor && lnf_ret == LNF_OK) || (!cursor && lnf_ret == LNF_EOF));
    if (lnf_ret == LNF_EOF) {
        PRINT_DEBUG("slave TPUT phase 2: 0 records are satisfying the threshold");
        return 0;  // memory is empty, zero records are satisfactory
    }

    // initialize the libnf record
    lnf_rec_t *lnf_rec;
    lnf_ret = lnf_rec_init(&lnf_rec);
    assert(lnf_ret == LNF_OK);

    size_t rec_cntr = 0;
    do {
        // read a next record
        lnf_ret = lnf_mem_read_c(lnf_mem, cursor, lnf_rec);
        assert(lnf_ret == LNF_OK);

        // extract the value of the sort key from the record
        uint64_t value;
        lnf_ret = lnf_rec_fget(lnf_rec, key, &value);
        assert(lnf_ret == LNF_OK);
        if (direction == LNF_SORT_DESC && value >= threshold) {
            rec_cntr++;
        } else if (direction == LNF_SORT_ASC && value <= threshold) {
            rec_cntr++;
        } else {
            break;
        }
        // move to the next record
    } while (lnf_mem_next_c(lnf_mem, &cursor) == LNF_OK);

    lnf_rec_free(lnf_rec);

    PRINT_DEBUG("slave TPUT phase 2: %" PRIu64
                " records are satisfying the threshold", rec_cntr);
    return rec_cntr;
}

/**
 * @brief Slave's TPUT phase 2: prune away ineligible objects.
 *
 * After receiving the threshold from the master, slave sends to the master a
 * list of records satisfying the received threshold
 * (see @ref tput_phase_2_find_threshold_cnt).
 *
 * @param[in] s_ctx Slave's all-purpose context.
 */
static void
tput_phase_2(struct slave_ctx *const s_ctx)
{
    assert(s_ctx);

    uint64_t threshold;
    MPI_Bcast(&threshold, 1, MPI_UINT64_T, ROOT_PROC, MPI_COMM_WORLD);

    // find number of records satisfying the threshold
    assert(args->fields_sort_key);
    assert(args->fields_sort_dir == LNF_SORT_DESC
           || args->fields_sort_dir == LNF_SORT_ASC);
    const uint64_t threshold_cnt = tput_phase_2_find_threshold_cnt(
            s_ctx->lnf_mem, threshold, args->fields_sort_key,
            args->fields_sort_dir);

    // send all records satisfying the threshold
    send_raw_mem_ng(s_ctx->lnf_mem, threshold_cnt, TAG_TPUT2, s_ctx->buff,
                    XCHG_BUFF_SIZE);
    PRINT_DEBUG("slave TPUT phase 2: done");
}

/**
 * @brief Slave's TPUT phase 3: identify the top N objects.
 *
 * The slave receives a set of records S from the master. The slave then looks
 * up the record in its libnf memory. If it finds matching aggregation key, the
 * records is send to the master.
 *
 * @param[in] s_ctx Slave's all-purpose context.
 */
static void
tput_phase_3(struct slave_ctx *s_ctx)
{
    assert(s_ctx);

    error_code_t ecode = E_OK;
    char rec_buff[LNF_MAX_RAW_LEN];
    int rec_len;
    int lnf_ret;

    // receive number of records in the masters memory
    uint64_t received_rec_cnt;
    MPI_Bcast(&received_rec_cnt, 1, MPI_UINT64_T, ROOT_PROC, MPI_COMM_WORLD);

    // initialize libnf memory for found records only
    lnf_mem_t *found_records;
    ecode = libnf_mem_init(&found_records, args->fields, true);
    assert(ecode == E_OK);

    uint64_t found_rec_cntr = 0;
    for (size_t i = 0; i < received_rec_cnt; ++i) {
        // receive a key from the master
        MPI_Bcast(&rec_len, 1, MPI_INT, ROOT_PROC, MPI_COMM_WORLD);
        assert(rec_len > 0);
        MPI_Bcast(rec_buff, rec_len, MPI_BYTE, ROOT_PROC, MPI_COMM_WORLD);

        // lookup the received key in my libnf memory
        lnf_mem_cursor_t *cursor;
        lnf_ret = lnf_mem_lookup_raw_c(s_ctx->lnf_mem, rec_buff, rec_len,
                                       &cursor);
        assert((cursor && lnf_ret == LNF_OK) || (!cursor && lnf_ret == LNF_EOF));
        if (lnf_ret == LNF_OK) {  // key found
            found_rec_cntr++;
            lnf_ret |= lnf_mem_read_raw_c(s_ctx->lnf_mem, cursor, rec_buff,
                                          &rec_len, sizeof (rec_buff));
            lnf_ret |= lnf_mem_write_raw(found_records, rec_buff, rec_len);
            assert(lnf_ret == LNF_OK);
        }
    }
    PRINT_DEBUG("slave TPUT phase 3: received %" PRIu64 " records, found %"
                PRIu64 " records", received_rec_cnt, found_rec_cntr);

    send_raw_mem_ng(found_records, 0, TAG_TPUT3, s_ctx->buff, XCHG_BUFF_SIZE);
    libnf_mem_free(found_records);

    PRINT_DEBUG("slave TPUT phase 3: done");
}
/**
 * @}
 */  // slave_tput

/**
 * @brief TODO
 *
 * @param s_ctx
 *
 * @return 
 */
static error_code_t
postprocess(struct slave_ctx *const s_ctx)
{
    assert(s_ctx);

    error_code_t ecode = E_OK;

    switch (args->working_mode) {
    case MODE_LIST:
        // all records already sent during reading, send the terminator
        MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD);
        break;

    case MODE_SORT:
        if (args->rec_limit != 0) {
            ecode = send_raw_mem(s_ctx->lnf_mem, args->rec_limit, s_ctx->buff,
                                 XCHG_BUFF_SIZE);
        } // else all records already sent while reading
        // send the terminator
        MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD);
        break;

    case MODE_AGGR:
        if (args->use_tput) {
            // use the TPUT Top-N algorithm
            tput_phase_1(s_ctx);
            tput_phase_2(s_ctx);
            tput_phase_3(s_ctx);
        } else {
            // send all aggregated records
            ecode = send_raw_mem(s_ctx->lnf_mem, 0, s_ctx->buff,
                                 XCHG_BUFF_SIZE);
        }
        // send the terminator
        MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD);
        break;

    case MODE_META:
        // nothing to do, not even the terminator
        break;

    default:
        assert(!"unknown working mode");
    }

    PRINT_DEBUG("postprocessing done");

    return ecode;
}

/**
 * @brief TODO
 *
 * There are two data buffers for each thread. The first one filled and then
 * passed to nonblocking MPI send function. In the meantime, the second one is
 * filled. After both these operations are completed, buffers are switched and
 * the whole process repeats until all data are sent.
 *
 * @param s_ctx
 * @param paths
 * @param paths_cnt
 *
 * @return 
 */
static error_code_t
process_files(struct slave_ctx *s_ctx, char *paths[], size_t paths_cnt)
{
    error_code_t ecode = E_OK;
    struct thread_ctx t_ctx = { 0 };

    // initialize the libnf record, only once for each thread
    int lnf_ret = lnf_rec_init(&t_ctx.lnf_rec);
    if (lnf_ret != LNF_OK) {
        ecode = E_LNF;
        PRINT_ERROR(ecode, lnf_ret, "lnf_rec_init()");
        goto return_label;
    }

    /*
     * Use already alocated buffers for the OMP master thread. For every other
     * thread, allocate two new data buffers for the records storage.
     */
    #pragma omp master
    {
        t_ctx.buff[0] = s_ctx->buff[0];
        t_ctx.buff[1] = s_ctx->buff[1];
    }
    if (t_ctx.buff[0] != s_ctx->buff[0]) {
        t_ctx.buff[0] = malloc(XCHG_BUFF_SIZE * sizeof (**t_ctx.buff));
        t_ctx.buff[1] = malloc(XCHG_BUFF_SIZE * sizeof (**t_ctx.buff));
        if (!t_ctx.buff[0] || !t_ctx.buff[1]) {
            ecode = E_MEM;
            PRINT_ERROR(E_MEM, 0, "malloc()");
            goto free_lnf_rec;
        }
    }

    /*
     * Perform a parallel loop through all files.
     * schedule(dynamic): dynamic scheduler is best for this use case, see
     *   https://github.com/CESNET/fdistdump/issues/6
     * nowait: don't wait for the other threads and start merging memory
     *   immediately
     */
    #pragma omp for schedule(dynamic) nowait
    for (size_t i = 0; i < paths_cnt; ++i) {
        if (ecode != E_OK) {
            // error on one of the threads, skip all remaining files
            goto continue_label;
        }

        PRINT_DEBUG("going to process file `%s'", paths[i]);

        // open the flow file
        lnf_ret = lnf_open(&t_ctx.file, paths[i], LNF_READ, NULL);
        if (lnf_ret != LNF_OK) {
            PRINT_WARNING(E_LNF, lnf_ret, "unable to open flow file `%s\'",
                          paths[i]);
            goto continue_label;
        }

        // increment the private metadata summary counters
        metadata_summ_update(&t_ctx.metadata_summ, t_ctx.file);

#ifdef HAVE_LIBBFINDEX
        if (s_ctx->bfindex_root) {  // Bloom filter indexing is enabled
            char *bfindex_file_path = bfindex_flow_to_index_path(paths[i]);
            if (bfindex_file_path) {
                PRINT_DEBUG("bfindex: using bfindex file `%s'",
                            bfindex_file_path);
                const bool contains = bfindex_contains(s_ctx->bfindex_root,
                                                       bfindex_file_path);
                free(bfindex_file_path);
                if (contains) {
                    PRINT_INFO("bfindex: query returned ``required IP address(es) possibly in file''");
                } else {
                    PRINT_INFO("bfindex: query returned ``required IP address(es) definitely not in file''");
                    goto continue_label;
                }
            } else {
                PRINT_WARNING(E_BFINDEX, 0,
                        "unable to convert flow file name into bfindex file name");
            }
        }
#endif  // HAVE_LIBBFINDEX

        // process the file according to the working mode
        switch (args->working_mode) {
        case MODE_LIST:
        {
            #pragma omp flush  // to flush rec_limit_reached
            if (!s_ctx->rec_limit_reached) {
                ecode = read_and_send_file(s_ctx, &t_ctx);
            }
            break;
        }

        case MODE_SORT:
            if (args->rec_limit == 0) {
                // do not perform local sort, only send all records
                ecode = read_and_send_file(s_ctx, &t_ctx);
            } else {
                // perform local sort first
                ecode = read_and_store_file(s_ctx, &t_ctx);
            }
            break;

        case MODE_AGGR:
            ecode = read_and_store_file(s_ctx, &t_ctx);
            break;

        case MODE_META:
            // metadata already read
            break;
        default:
            assert(!"unknown working mode");
        }

        lnf_close(t_ctx.file);

continue_label:
        // report that another file has been processed
        progress_report_next();
    }  // end of the parallel loop through all files

    // atomic addition of the thread-private counters into the thread-shared
    processed_summ_share(&s_ctx->processed_summ, &t_ctx.processed_summ);
    metadata_summ_share(&s_ctx->metadata_summ, &t_ctx.metadata_summ);

    // free the non-OMP-master-thread record storage buffers
    if (t_ctx.buff[0] != s_ctx->buff[0]) {
        free(t_ctx.buff[1]);
        free(t_ctx.buff[0]);
    }

    // merge thread-specific hash tables into thread-shared one
    if (s_ctx->lnf_mem) {
        lnf_mem_merge_threads(s_ctx->lnf_mem);
    }

free_lnf_rec:
    lnf_rec_free(t_ctx.lnf_rec);
return_label:
    return ecode;
}


/**
 * @brief TODO
 *
 * @param filter
 * @param filter_str
 *
 * @return 
 */
static error_code_t
init_filter(lnf_filter_t **filter, char *filter_str)
{
    assert(filter && filter_str && strlen(filter_str) != 0);

    int lnf_ret = lnf_filter_init_v2(filter, filter_str);
    if (lnf_ret != LNF_OK) {
        PRINT_ERROR(E_LNF, lnf_ret, "cannot initialise filter `%s'", filter_str);
        return E_LNF;
    }

    return E_OK;
}

#ifdef HAVE_LIBBFINDEX
/**
 * @brief TODO
 *
 * @param filter
 *
 * @return NULL is OK and error/warning message was printed in bfindex_init()
 */
static struct bfindex_node *
init_bfindex(lnf_filter_t *filter)
{
    assert(filter);

    const ff_t *const filter_tree = lnf_filter_ffilter_ptr(filter);
    assert(filter_tree && filter_tree->root);
    return bfindex_init(filter_tree->root);  // return NULL is OK
}
#endif  // HAVE_LIBBFINDEX

/**
 * @brief TODO
 *
 * @param s_ctx
 *
 * @return 
 */
static error_code_t
init_record_storage(struct slave_ctx *s_ctx)
{
    assert(s_ctx);

    error_code_t ecode = E_OK;

    switch (args->working_mode) {
    case MODE_LIST:
        // no storage required, nothing to initialize
        return E_OK;

    case MODE_SORT:
        /*
         * In queries without record limit we don't need the memory because
         * local sort would be useless. (TODO: that is not completely true,
         * merging of sorted lists of master would be faster)
         * With record limit, however, we can sort all records on each slave and
         * then send only first rec_limit records.
         */
        if (args->rec_limit == 0) {
            // no storage required, nothing to initialize
            break;
        }

        // initialize the libnf sorting memory and set its parameters
        ecode = libnf_mem_init(&s_ctx->lnf_mem, args->fields, true);
        if (ecode != E_OK) {
            return ecode;
        }
        break;

    case MODE_AGGR:
        // initialize the libnf aggregation memory and set its parameters
        ecode = libnf_mem_init(&s_ctx->lnf_mem, args->fields, false);
        break;

    case MODE_META:
        // no storage required, nothing to initialize
        return E_OK;

    default:
        assert(!"unknown working mode");
    }

    return ecode;
}

/**
 * @brief TODO
 *
 * @param s_ctx
 */
static void
slave_free(struct slave_ctx *s_ctx)
{
    if (s_ctx->filter) {
        lnf_filter_free(s_ctx->filter);
    }
    if (s_ctx->lnf_mem) {
        libnf_mem_free(s_ctx->lnf_mem);
    }
#ifdef HAVE_LIBBFINDEX
    if (s_ctx->bfindex_root){
        bfindex_free(s_ctx->bfindex_root);
    }
#endif  // HAVE_LIBBFINDEX
}


/*
 * Public functions.
 */
/**
 * @brief Slave's process entry point.
 *
 * Entry point to the code executed only by the slave processes (usually with
 * ranks > 0).
 *
 * @param[in] world_size MPI_COMM_WORLD size.
 * @param[in] args Parsed command-line arguments.
 *
 * @return Error code.
 */
error_code_t
slave_main(int world_size, const struct cmdline_args *args_local)
{
    assert(world_size > 1 && args_local);

    error_code_t ecode = E_OK;
    args = args_local;  // share the command-line arguments by a global variable

    struct slave_ctx s_ctx;
    memset(&s_ctx, 0, sizeof (s_ctx));
    s_ctx.slave_cnt = world_size - 1; // all nodes without master

    // allocate two data buffers for records storage
    s_ctx.buff[0] = malloc(XCHG_BUFF_SIZE * sizeof (**s_ctx.buff));
    s_ctx.buff[1] = malloc(XCHG_BUFF_SIZE * sizeof (**s_ctx.buff));
    if (s_ctx.buff[0] == NULL || s_ctx.buff[1] == NULL) {
        ecode = E_MEM;
        PRINT_ERROR(E_MEM, 0, "malloc()");
        goto finalize;
    }

    // initialize the filter and the Bloom filter index, if possible
    if (args->filter_str) {
        ecode = init_filter(&s_ctx.filter, args->filter_str);
        if (ecode != E_OK) {
            goto free_buffers;
        }

#ifdef HAVE_LIBBFINDEX
        if (args->use_bfindex) {
            s_ctx.bfindex_root = init_bfindex(s_ctx.filter);
            if (s_ctx.bfindex_root) {
                PRINT_INFO("Bloom filter indexes enabled");
            } else {
                PRINT_INFO("Bloom filter indexes disabled involuntarily");
            }
        } else {
            PRINT_INFO("Bloom filter indexes disabled voluntarily");
        }
#endif  // HAVE_LIBBFINDEX
    }

    // perform allocations and initializations of record storage, if required
    ecode = init_record_storage(&s_ctx);
    if (ecode != E_OK) {
        goto free_buffers;
    }

    // generate paths to the specific flow files
    size_t paths_cnt = 0;
    char **paths = path_array_gen(args->paths, args->paths_cnt,
                                  args->time_begin, args->time_end,
            &paths_cnt);
    if (!paths) {
        ecode = E_PATH;
        goto free_buffers;
    }

    // report number of files to be processed
    progress_report_init(paths_cnt);

#ifdef _OPENMP
    // spawn at most files-count threads
    if (paths_cnt < (size_t)omp_get_max_threads()) {
        omp_set_num_threads(paths_cnt);
    }
#endif //_OPENMP

    #pragma omp parallel reduction(max:ecode)
    {
        ecode = process_files(&s_ctx, paths, paths_cnt);
    } // impicit barrier
    path_array_free(paths, paths_cnt);
    if (ecode != E_OK) {
        goto free_buffers;
    }

    /*
     * In case of aggregation or sorting, records were saved into the libnf
     * memory and we need to postprocess and send them to the master.
     */
    ecode = postprocess(&s_ctx);

    // reduce statistic values to the master
    MPI_Reduce(&s_ctx.processed_summ, NULL, STRUCT_PROCESSED_SUMM_ELEMENTS,
               MPI_UINT64_T, MPI_SUM, ROOT_PROC, MPI_COMM_WORLD);
    MPI_Reduce(&s_ctx.metadata_summ, NULL, STRUCT_METADATA_SUMM_ELEMENTS,
               MPI_UINT64_T, MPI_SUM, ROOT_PROC, MPI_COMM_WORLD);

free_buffers:
    free(s_ctx.buff[1]);
    free(s_ctx.buff[0]);

finalize:
    slave_free(&s_ctx);

    return ecode;
}
