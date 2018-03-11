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
    lnf_filter_t *lnf_filter; // libnf compiled filter expression
                              // TODO: can thread-private filter be faster?
#ifdef HAVE_LIBBFINDEX
    struct bfindex_node *bfindex_root;  // indexing IP address tree root
                                        // (created from the the libnf filter)
#endif  // HAVE_LIBBFINDEX
    uint64_t proc_rec_cntr;  // processed record counter
    bool rec_limit_reached; // true if rec_limit records has been read
    struct processed_summ processed_summ;  // summary of processed records
    struct metadata_summ metadata_summ;    // summary of flow files metadata

    uint64_t tput_threshold;
    char tput_rec_buff[LNF_MAX_RAW_LEN];
    int tput_rec_len;
    uint64_t tput_rec_cnt;
};

struct thread_ctx {  // thread-private context
    lnf_mem_t *lnf_mem;  // libnf memory used for record storage
    lnf_file_t *lnf_file;  // libnf file
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
 * @param lnf_filter
 * @param filter_str
 *
 * @return 
 */
static error_code_t
init_filter(lnf_filter_t **lnf_filter, char *filter_str)
{
    assert(lnf_filter && filter_str && strlen(filter_str) != 0);

    int lnf_ret = lnf_filter_init_v2(lnf_filter, filter_str);
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
 * @param lnf_filter
 *
 * @return NULL is OK and error/warning message was printed in bfindex_init()
 */
static struct bfindex_node *
init_bfindex(lnf_filter_t *lnf_filter)
{
    assert(lnf_filter);

    const ff_t *const filter_tree = lnf_filter_ffilter_ptr(lnf_filter);
    assert(filter_tree && filter_tree->root);
    return bfindex_init(filter_tree->root);  // return NULL is OK
}
#endif  // HAVE_LIBBFINDEX

static void
slave_ctx_init(struct slave_ctx *const s_ctx)
{
    assert(s_ctx);

    // initialize the filter and the Bloom filter index, if possible
    if (args->filter_str) {
        init_filter(&s_ctx->lnf_filter, args->filter_str);

#ifdef HAVE_LIBBFINDEX
        if (args->use_bfindex) {
            s_ctx->bfindex_root = init_bfindex(s_ctx->lnf_filter);
            if (s_ctx->bfindex_root) {
                PRINT_INFO("Bloom filter indexes enabled");
            } else {
                PRINT_INFO("Bloom filter indexes disabled involuntarily");
            }
        } else {
            PRINT_INFO("Bloom filter indexes disabled voluntarily");
        }
#endif  // HAVE_LIBBFINDEX
    }
}

static void
slave_ctx_free(struct slave_ctx *const s_ctx)
{
    assert(s_ctx);

    if (s_ctx->lnf_filter) {
        lnf_filter_free(s_ctx->lnf_filter);
    }
#ifdef HAVE_LIBBFINDEX
    if (s_ctx->bfindex_root){
        bfindex_free(s_ctx->bfindex_root);
    }
#endif  // HAVE_LIBBFINDEX
}

static void
thread_ctx_init(struct thread_ctx *const t_ctx)
{
    assert(t_ctx);

    // initialize the libnf record, only once for each thread
    int lnf_ret = lnf_rec_init(&t_ctx->lnf_rec);
    if (lnf_ret != LNF_OK) {
        PRINT_ERROR(E_LNF, lnf_ret, "lnf_rec_init()");
        MPI_Abort(MPI_COMM_WORLD, E_LNF);
    }

    // allocate two new data buffers for the records storage.
    t_ctx->buff[0] = malloc(XCHG_BUFF_SIZE * sizeof (*t_ctx->buff[0]));
    t_ctx->buff[1] = malloc(XCHG_BUFF_SIZE * sizeof (*t_ctx->buff[1]));
    ERROR_IF(!t_ctx->buff[0] || !t_ctx->buff[1], E_MEM,
             "thread record buffer allocation failed");

    // perform allocations and initializations of the libnf memory record
    // storage, if required
    switch (args->working_mode) {
    case MODE_LIST:
        // no storage required, everything will be sent while reading
        break;

    case MODE_SORT:
        // initialize the libnf sorting memory and set its parameters
        libnf_mem_init(&t_ctx->lnf_mem, args->fields, true);
        break;

    case MODE_AGGR:
        // initialize the libnf aggregation memory and set its parameters
        libnf_mem_init(&t_ctx->lnf_mem, args->fields, false);
        break;

    case MODE_META:
        // no storage required
        break;

    default:
        assert(!"unknown working mode");
    }
}

static void
thread_ctx_free(struct thread_ctx *const t_ctx)
{
    assert(t_ctx);

    lnf_rec_free(t_ctx->lnf_rec);

    // free the thread-local record storage buffers
    free(t_ctx->buff[0]);
    free(t_ctx->buff[1]);

    if (t_ctx->lnf_mem) {
        libnf_mem_free(t_ctx->lnf_mem);
    }
}

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
 * @param lnf_file
 */
static void
metadata_summ_update(struct metadata_summ *private, lnf_file_t *lnf_file)
{
    assert(private && lnf_file);

    struct metadata_summ tmp;
    int lnf_ret;

    // flows
    lnf_ret |= lnf_info(lnf_file, LNF_INFO_FLOWS, &tmp.flows,
                        sizeof (tmp.flows));
    lnf_ret |= lnf_info(lnf_file, LNF_INFO_FLOWS_TCP, &tmp.flows_tcp,
                        sizeof (tmp.flows_tcp));
    lnf_ret |= lnf_info(lnf_file, LNF_INFO_FLOWS_UDP, &tmp.flows_udp,
                        sizeof (tmp.flows_udp));
    lnf_ret |= lnf_info(lnf_file, LNF_INFO_FLOWS_ICMP, &tmp.flows_icmp,
                        sizeof (tmp.flows_icmp));
    lnf_ret |= lnf_info(lnf_file, LNF_INFO_FLOWS_OTHER, &tmp.flows_other,
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
    lnf_ret |= lnf_info(lnf_file, LNF_INFO_PACKETS, &tmp.pkts,
                        sizeof (tmp.pkts));
    lnf_ret |= lnf_info(lnf_file, LNF_INFO_PACKETS_TCP, &tmp.pkts_tcp,
                        sizeof (tmp.pkts_tcp));
    lnf_ret |= lnf_info(lnf_file, LNF_INFO_PACKETS_UDP, &tmp.pkts_udp,
                        sizeof (tmp.pkts_udp));
    lnf_ret |= lnf_info(lnf_file, LNF_INFO_PACKETS_ICMP, &tmp.pkts_icmp,
                        sizeof (tmp.pkts_icmp));
    lnf_ret |= lnf_info(lnf_file, LNF_INFO_PACKETS_OTHER, &tmp.pkts_other,
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
    lnf_ret |= lnf_info(lnf_file, LNF_INFO_BYTES, &tmp.bytes,
                        sizeof (tmp.bytes));
    lnf_ret |= lnf_info(lnf_file, LNF_INFO_BYTES_TCP, &tmp.bytes_tcp,
                        sizeof (tmp.bytes_tcp));
    lnf_ret |= lnf_info(lnf_file, LNF_INFO_BYTES_UDP, &tmp.bytes_udp,
                        sizeof (tmp.bytes_udp));
    lnf_ret |= lnf_info(lnf_file, LNF_INFO_BYTES_ICMP, &tmp.bytes_icmp,
                        sizeof (tmp.bytes_icmp));
    lnf_ret |= lnf_info(lnf_file, LNF_INFO_BYTES_OTHER, &tmp.bytes_other,
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
 * Read all records from the file. No aggregation is performed, records are only
 * saved into the record buffer. When the buffer is full, it is sent towards the
 * master.
 */
static void
ff_read_and_send(const char *ff_path, struct slave_ctx *s_ctx,
                   struct thread_ctx *t_ctx, int mpi_tag)
{
    assert(ff_path && s_ctx && t_ctx);

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
    bool buff_idx = 0; //index to the currently used data buffer
    size_t buff_off = 0; //current data buffer offset
    size_t buff_rec_cntr = 0; //number of records in the current buffer
    MPI_Request request = MPI_REQUEST_NULL;
    int lnf_ret;
    while ((lnf_ret = lnf_read(t_ctx->lnf_file, t_ctx->lnf_rec)) == LNF_OK) {
        file_rec_cntr++;

        // try to match the filter (if there is one)
        if (s_ctx->lnf_filter && !lnf_filter_match(s_ctx->lnf_filter, t_ctx->lnf_rec)) {
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

            MPI_Wait(&request, MPI_STATUS_IGNORE);
            MPI_Isend(t_ctx->buff[buff_idx], buff_off, MPI_BYTE, ROOT_PROC,
                      mpi_tag, MPI_COMM_WORLD, &request);

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
        MPI_Wait(&request, MPI_STATUS_IGNORE);
        MPI_Isend(t_ctx->buff[buff_idx], buff_off, MPI_BYTE, ROOT_PROC, mpi_tag,
                  MPI_COMM_WORLD, &request);

        // increment the thread-shared counter of processed records
        #pragma omp atomic
        s_ctx->proc_rec_cntr += buff_rec_cntr;
    }

    // either set the record limit reached flag or check if EOF was reached
    if (args->rec_limit && s_ctx->proc_rec_cntr >= args->rec_limit) {
        s_ctx->rec_limit_reached = true;
        #pragma omp flush
    } else if (lnf_ret != LNF_EOF) {
        PRINT_WARNING(E_LNF, lnf_ret, "`%s': EOF was not reached", ff_path);
    }

    // the buffers will be invalid after return, wait for the send to complete
    MPI_Wait(&request, MPI_STATUS_IGNORE);

    PRINT_DEBUG("`%s': read %zu records, processed %zu records", ff_path,
                file_rec_cntr, file_proc_rec_cntr);
}

/**
 * @brief TODO
 *
 * Read all records from the file. Aggreagation is performed (records are
 * written to the libnf memory, which is a hash table). The record limit is
 * ignored.
 */
static void
ff_read_and_store(const char *ff_path, struct slave_ctx *s_ctx,
                   struct thread_ctx *t_ctx)
{
    assert(ff_path && s_ctx && t_ctx);

    // loop through all records, HOT PATH!
    int lnf_ret;
    size_t file_rec_cntr = 0;
    size_t file_proc_rec_cntr = 0;
    while ((lnf_ret = lnf_read(t_ctx->lnf_file, t_ctx->lnf_rec)) == LNF_OK) {
        file_rec_cntr++;

        // try to match the filter (if there is one)
        if (s_ctx->lnf_filter && !lnf_filter_match(s_ctx->lnf_filter,
                                                   t_ctx->lnf_rec)) {
            continue;
        }
        file_proc_rec_cntr++;

        // update the thread-private processed summary counters
        processed_summ_update(&t_ctx->processed_summ, t_ctx->lnf_rec);

        // write the record into the libnf memory (a hash table)
        lnf_ret = lnf_mem_write(t_ctx->lnf_mem, t_ctx->lnf_rec);
        ERROR_IF(lnf_ret != LNF_OK, E_LNF, "`%s': lnf_mem_write()", ff_path);
    }
    WARN_IF(lnf_ret != LNF_EOF, E_LNF, "`%s': EOF was not reached", ff_path);

    PRINT_DEBUG("`%s': read %zu records, processed %zu records", ff_path,
                file_rec_cntr, file_proc_rec_cntr);
}


static void
send_terminator(const int mpi_tag)
{
    MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, mpi_tag, MPI_COMM_WORLD);
}

/**
 * @brief TODO
 *
 * Terminator is included.
 *
 * This function is not thread-safe without MPI_THREAD_MULTIPLE.
 *
 * @param lnf_mem
 * @param rec_limit
 * @param mpi_tag
 * @param buff
 * @param buff_size
 */
static void
send_raw_mem(lnf_mem_t *const lnf_mem, size_t rec_limit, int mpi_tag,
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

    send_terminator(mpi_tag);
    PRINT_DEBUG("send_raw_mem: sent %zu record(s) with tag %d", rec_cntr,
                mpi_tag);
}


/**
 * @brief TODO
 *
 * This function is not thread-safe without MPI_THREAD_MULTIPLE.
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
 *
 * This function is not thread-safe without MPI_THREAD_MULTIPLE.
 *
 */
static void
progress_report_next(void)
{
    MPI_Request request = MPI_REQUEST_NULL;
    MPI_Isend(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_PROGRESS, MPI_COMM_WORLD,
              &request);
    MPI_Request_free(&request);
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
 * @param[in] s_ctx Thread-shared context.
 * @param[in] t_ctx Thread-local context.
 */
static void
tput_phase_1(struct slave_ctx *const s_ctx, struct thread_ctx *const t_ctx)
{
    assert(s_ctx && t_ctx);

    // send the top N items from the sorted list
    send_raw_mem(t_ctx->lnf_mem, args->rec_limit, TAG_TPUT1, t_ctx->buff,
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
 * This function is not thread-safe without MPI_THREAD_MULTIPLE.
 *
 * @param[in] s_ctx Thread-shared context.
 * @param[in] t_ctx Thread-local context.
 */
static void
tput_phase_2(struct slave_ctx *const s_ctx, struct thread_ctx *const t_ctx)
{
    assert(s_ctx && t_ctx);

    #pragma omp single
    {
        MPI_Bcast(&s_ctx->tput_threshold, 1, MPI_UINT64_T, ROOT_PROC,
                  MPI_COMM_WORLD);
        PRINT_DEBUG("have threshold %" PRIu64, s_ctx->tput_threshold);
    }  // implicit barrier and flush

    // find number of records satisfying the threshold
    assert(args->fields_sort_key);
    assert(args->fields_sort_dir == LNF_SORT_DESC
           || args->fields_sort_dir == LNF_SORT_ASC);
    const uint64_t threshold_cnt = tput_phase_2_find_threshold_cnt(
            t_ctx->lnf_mem, s_ctx->tput_threshold, args->fields_sort_key,
            args->fields_sort_dir);

    // send all records satisfying the threshold
    send_raw_mem(t_ctx->lnf_mem, threshold_cnt, TAG_TPUT2, t_ctx->buff,
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
 * This function is not thread-safe without MPI_THREAD_MULTIPLE.
 *
 * @param[in] s_ctx Thread-shared context.
 * @param[in] t_ctx Thread-local context.
 */
static void
tput_phase_3(struct slave_ctx *s_ctx, struct thread_ctx *const t_ctx)
{
    assert(s_ctx && t_ctx);

    #pragma omp single
    {
        // receive number of records in the masters memory
        MPI_Bcast(&s_ctx->tput_rec_cnt, 1, MPI_UINT64_T, ROOT_PROC,
                  MPI_COMM_WORLD);
    }  // implicit barrier and flush

    // initialize libnf memory for found records only
    lnf_mem_t *found_records;
    libnf_mem_init(&found_records, args->fields, true);

    uint64_t found_rec_cntr = 0;
    for (uint64_t i = 0; i < s_ctx->tput_rec_cnt; ++i) {
        int lnf_ret;
        #pragma omp single
        {
            // receive a key from the master
            MPI_Bcast(&s_ctx->tput_rec_len, 1, MPI_INT, ROOT_PROC,
                      MPI_COMM_WORLD);
            assert(IN_RANGE_INCL(s_ctx->tput_rec_len, 1,
                                 (int)sizeof (s_ctx->tput_rec_buff) + 1));
            MPI_Bcast(s_ctx->tput_rec_buff, s_ctx->tput_rec_len, MPI_BYTE,
                      ROOT_PROC, MPI_COMM_WORLD);
        }  // implicit barrier and flush

        // lookup the received key in my libnf memory
        lnf_mem_cursor_t *cursor;
        lnf_ret = lnf_mem_lookup_raw_c(t_ctx->lnf_mem, s_ctx->tput_rec_buff,
                                       s_ctx->tput_rec_len, &cursor);
        #pragma omp barrier  // wait for each thred to finish the lookup
        assert((cursor && lnf_ret == LNF_OK) || (!cursor && lnf_ret == LNF_EOF));
        if (lnf_ret == LNF_OK) {  // key found
            found_rec_cntr++;

            char rec_buff[LNF_MAX_RAW_LEN];
            int rec_len;
            lnf_ret |= lnf_mem_read_raw_c(t_ctx->lnf_mem, cursor, rec_buff,
                                          &rec_len, sizeof (rec_buff));
            lnf_ret |= lnf_mem_write_raw(found_records, rec_buff, rec_len);
            assert(lnf_ret == LNF_OK);
        }
    }
    PRINT_DEBUG("slave TPUT phase 3: received %" PRIu64 " records, found %"
                PRIu64 " records", s_ctx->tput_rec_cnt, found_rec_cntr);

    send_raw_mem(found_records, 0, TAG_TPUT3, t_ctx->buff, XCHG_BUFF_SIZE);

    libnf_mem_free(found_records);
    PRINT_DEBUG("slave TPUT phase 3: done");
}
/**
 * @}
 */  // slave_tput


/**
 * @brief TODO
 *
 * There are two data buffers for each thread. The first one filled and then
 * passed to nonblocking MPI send function. In the meantime, the second one is
 * filled. After both these operations are completed, buffers are switched and
 * the whole process repeats until all data are sent.
 *
 */
static void
process_file_mt(struct slave_ctx *const s_ctx, struct thread_ctx *const t_ctx,
                const char *const ff_path)
{
    PRINT_DEBUG("`%s': processing...", ff_path);

    // open the flow file
    // TODO: open and update metadata counters before or after bfindex?
    int lnf_ret = lnf_open(&t_ctx->lnf_file, ff_path, LNF_READ, NULL);
    if (lnf_ret != LNF_OK) {
        PRINT_WARNING(E_LNF, lnf_ret, "`%s\': unable to open flow file",
                      ff_path);
        t_ctx->lnf_file = NULL;
        goto return_label;
    }

    // read and update the thread-private metadata summary counters
    metadata_summ_update(&t_ctx->metadata_summ, t_ctx->lnf_file);

#ifdef HAVE_LIBBFINDEX
    if (s_ctx->bfindex_root) {  // Bloom filter indexing is enabled
        char *bfindex_file_path = bfindex_flow_to_index_path(ff_path);
        if (bfindex_file_path) {
            PRINT_DEBUG("`%s': using bfindex file `%s'", ff_path,
                        bfindex_file_path);
            const bool contains = bfindex_contains(s_ctx->bfindex_root,
                                                   bfindex_file_path);
            free(bfindex_file_path);
            if (contains) {
                PRINT_INFO("`%s': bfindex query returned "
                           "``required IP address(es) possibly in file''",
                           ff_path);
            } else {
                PRINT_INFO("`%s': bfindex query returned "
                           "``required IP address(es) definitely not in file''",
                           ff_path);
                goto return_label;
            }
        } else {
            PRINT_WARNING(E_BFINDEX, 0, "`%s': "
                          "unable to convert flow file name into bfindex file name",
                          ff_path);
        }
    }
#endif  // HAVE_LIBBFINDEX

    // process the file according to the working mode
    switch (args->working_mode) {
    case MODE_LIST:
    {
        #pragma omp flush  // to flush rec_limit_reached
        if (!s_ctx->rec_limit_reached) {
            ff_read_and_send(ff_path, s_ctx, t_ctx, TAG_LIST);
        }
        break;
    }

    case MODE_SORT:
        // store records into the thread-local libnf memory (linked list)
        ff_read_and_store(ff_path, s_ctx, t_ctx);
        break;

    case MODE_AGGR:
        // aggregate records into the thread-local libnf memory (hash table)
        ff_read_and_store(ff_path, s_ctx, t_ctx);
        break;

    case MODE_META:
        // metadata already read
        break;
    default:
        assert(!"unknown working mode");
    }

return_label:
    if (t_ctx->lnf_file) {
        lnf_close(t_ctx->lnf_file);
    }

    PRINT_DEBUG("`%s': done", ff_path);
    return;
}

static void
postprocess_mt(struct slave_ctx *const s_ctx, struct thread_ctx *const t_ctx)
{
    assert(s_ctx && t_ctx);

    switch (args->working_mode) {
    case MODE_LIST:
        // all records already sent during reading
        send_terminator(TAG_LIST);
        break;

    case MODE_SORT:
        // merge thread-specific hash tables into thread-shared one
        PRINT_DEBUG("sorting records in thread-local libnf memory...");
        libnf_mem_sort(t_ctx->lnf_mem);
        PRINT_DEBUG("sorting records in thread-local libnf memory done");
        send_raw_mem(t_ctx->lnf_mem, args->rec_limit, TAG_SORT, t_ctx->buff,
                     XCHG_BUFF_SIZE);
        break;

    case MODE_AGGR:
        if (args->use_tput) {
            assert(args->rec_limit);
            // use the TPUT Top-N algorithm
            tput_phase_1(s_ctx, t_ctx);
            tput_phase_2(s_ctx, t_ctx);
            tput_phase_3(s_ctx, t_ctx);
        } else {
            // send all records
            send_raw_mem(t_ctx->lnf_mem, 0, TAG_AGGR, t_ctx->buff,
                         XCHG_BUFF_SIZE);
        }
        break;

    case MODE_META:
        // nothing to do, not even the terminator
        break;

    default:
        assert(!"unknown working mode");
    }

    PRINT_DEBUG("postprocess_mt done");
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
 * This function is not thread-safe without MPI_THREAD_MULTIPLE.
 *
 * @param[in] args Parsed command-line arguments.
 */
void
slave_main(const struct cmdline_args *args_local)
{
    assert(args_local);

    args = args_local;  // share the command-line arguments by a global variable

    struct slave_ctx s_ctx = { 0 };
    slave_ctx_init(&s_ctx);

    // generate paths to the specific flow files
    uint64_t ff_paths_cnt = 0;
    char **ff_paths = path_array_gen(args->paths, args->paths_cnt,
                                     args->time_begin, args->time_end,
            &ff_paths_cnt);
    assert(ff_paths);
    PRINT_DEBUG("going to process %" PRIu64 " flow file(s)", ff_paths_cnt);

    // report number of files to be processed
    progress_report_init(ff_paths_cnt);

    int num_threads = 1;  // one threads if OpenMP is not used
#ifdef _OPENMP
    {
        // use at most files-count threads
        int max_threads = omp_get_max_threads();
        assert(max_threads > 0);
        if (ff_paths_cnt < (uint64_t)max_threads) {
            num_threads = ff_paths_cnt;
            omp_set_num_threads(num_threads);
        } else {
            num_threads = max_threads;
        }
    }
#endif  //_OPENMP
    PRINT_DEBUG("using %d thread(s)", num_threads);
    // send a number of used threads
    MPI_Reduce(&num_threads, NULL, 1, MPI_INT, MPI_SUM, ROOT_PROC,
               MPI_COMM_WORLD);

    #pragma omp parallel
    {
        struct thread_ctx t_ctx = { 0 };
        thread_ctx_init(&t_ctx);

        /*
         * Perform a parallel loop through all files.
         * schedule(dynamic): dynamic scheduler is best for this use case, see
         *   https://github.com/CESNET/fdistdump/issues/6
         * nowait: don't wait for the other threads and start merging memory
         *   immediately
         */
        uint64_t file_cntr = 0;
        #pragma omp for schedule(dynamic) nowait
        for (size_t i = 0; i < ff_paths_cnt; ++i) {
            const char *const ff_path = ff_paths[i];

            // process the flow file
            process_file_mt(&s_ctx, &t_ctx, ff_path);
            file_cntr++;

            // report that another flow file has been processed
            progress_report_next();

        }  // end of the parallel loop through all files, no barrier
        PRINT_DEBUG("thread processed %" PRIu64 " flow file(s)", file_cntr);

        // atomic update of the thread-shared counters
        processed_summ_share(&s_ctx.processed_summ, &t_ctx.processed_summ);
        metadata_summ_share(&s_ctx.metadata_summ, &t_ctx.metadata_summ);

        // postprocessing required in the parallel section
        postprocess_mt(&s_ctx, &t_ctx);

        thread_ctx_free(&t_ctx);
    }  // impicit barrier

    // path array is no longer needed
    path_array_free(ff_paths, ff_paths_cnt);

    // reduce statistic values to the master
    MPI_Reduce(&s_ctx.processed_summ, NULL, STRUCT_PROCESSED_SUMM_ELEMENTS,
               MPI_UINT64_T, MPI_SUM, ROOT_PROC, MPI_COMM_WORLD);
    MPI_Reduce(&s_ctx.metadata_summ, NULL, STRUCT_METADATA_SUMM_ELEMENTS,
               MPI_UINT64_T, MPI_SUM, ROOT_PROC, MPI_COMM_WORLD);

    slave_ctx_free(&s_ctx);
}
