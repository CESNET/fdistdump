/** Master process functionality.
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

#include "master.h"

#include <assert.h>             // for assert
#include <errno.h>              // for errno
#include <inttypes.h>           // for fixed-width integer types
#include <math.h>               // for ceil
#include <stdbool.h>            // for false, bool, true
#include <stdio.h>              // for fprintf, fclose, fflush, fopen, rewind
#include <stdlib.h>             // for free, calloc, malloc
#include <string.h>             // for strcmp, strerror, size_t, NULL

#include <libnf.h>              // for LNF_OK, lnf_mem_t, lnf_mem_first_c
#include <mpi.h>                // for MPI_Bcast, MPI_Irecv, MPI_Reduce, MPI...

#include "arg_parse.h"          // for cmdline_args
#include "common.h"             // for libnf_mem_free, libnf_mem_init, ...
#include "output.h"             // for print_mem, output_setup, ...
#include "print.h"              // for PRINT_DEBUG, ERROR_IF, PRINT_WARNING


/*
 * Global variables.
 */
static const struct cmdline_args *args;


/*
 * Data types declarations.
 */
struct master_ctx {  // thread-shared context
    uint8_t *rec_buff[2];  // two record buffers for IO/communication overlap
    uint64_t slave_threads_cnt;  // number threads on all slaves
};

struct mem_write_callback_data {
    lnf_mem_t *lnf_mem;
    lnf_rec_t *lnf_rec;

    struct {
        int id;
        size_t size;
    } fields[LNF_FLD_TERM_];  // fields array compressed for faster access
    size_t fields_cnt;        // number of fields present in the fields array
};

typedef error_code_t (*recv_callback_t)(uint8_t *data, xchg_rec_size_t data_len,
                                        void *user);


/*
 * Static functions.
 */
static struct master_ctx *
master_ctx_init(const uint64_t slave_threads_cnt)
{
    assert(slave_threads_cnt > 0);

    struct master_ctx *const m_ctx = calloc(1, sizeof (*m_ctx));
    ERROR_IF(!m_ctx, E_MEM, "master context structure allocation failed");

    m_ctx->slave_threads_cnt = slave_threads_cnt;

    // allocate the record buffers
    for (uint8_t i = 0; i < 2; ++i) {
        m_ctx->rec_buff[i] =
            malloc(XCHG_BUFF_SIZE * sizeof (*m_ctx->rec_buff[i]));
        ERROR_IF(!m_ctx->rec_buff[i], E_MEM,
                 "master record buffer allocation failed");
    }

    return m_ctx;
}

static void
master_ctx_free(struct master_ctx *const m_ctx)
{
    assert(m_ctx);

    for (uint8_t i = 0; i < 2; ++i) {
        free(m_ctx->rec_buff[i]);
    }
    free(m_ctx);
}


static error_code_t
mem_write_raw_callback(uint8_t *data, xchg_rec_size_t data_len, void *user)
{
    int lnf_ret = lnf_mem_write_raw((lnf_mem_t *)user, (char *)data, data_len);
    if (lnf_ret != LNF_OK) {
        PRINT_ERROR(E_LNF, lnf_ret, "lnf_mem_write_raw()");
        return E_LNF;
    }

    return E_OK;
}

static error_code_t
print_rec_callback(uint8_t *data, xchg_rec_size_t data_len, void *user)
{
        (void)data_len;  // unused
        (void)user;  // unused

        print_rec(data);
        return E_OK;
}


/**
 * @defgroup progress_bar_master TODO
 * @{
 */
struct progress_bar_ctx {
    progress_bar_type_t type;
    uint64_t sources_cnt;
    uint64_t *files_cnt;      // sources_cnt sized
    uint64_t *files_cnt_goal;  // sources_cnt sized
    uint64_t files_cnt_sum;
    uint64_t files_cnt_goal_sum;
    FILE *out_stream;
};

/**
 * @brief TODO
 */
static void
progress_bar_print(const struct progress_bar_ctx *const pb_ctx)
{
    assert(pb_ctx);
    assert(pb_ctx->files_cnt_sum <= pb_ctx->files_cnt_goal_sum);

    // calculate a percentage progress for each source
    double source_percentage[pb_ctx->sources_cnt];
    for (uint64_t i = 0; i < pb_ctx->sources_cnt; ++i) {
        assert(pb_ctx->files_cnt[i] <= pb_ctx->files_cnt_goal[i]);

        if (pb_ctx->files_cnt_goal[i] == 0) {
            source_percentage[i] = 100.0;
        } else {
            source_percentage[i] = (double)pb_ctx->files_cnt[i]
                / pb_ctx->files_cnt_goal[i] * 100.0;
        }
    }

    // calculate a total percentage progress
    double total_percentage;
    if (pb_ctx->files_cnt_goal_sum == 0) {
        total_percentage = 100.0;
    } else {
        total_percentage =
            (double)pb_ctx->files_cnt_sum / pb_ctx->files_cnt_goal_sum * 100.0;
    }

    // diverge for each progress bar type
#define PROGRESS_BAR_FMT "%" PRIu64 "/%" PRIu64 " (%.0f %%)"
    switch (pb_ctx->type) {
    case PROGRESS_BAR_TOTAL:
        fprintf(pb_ctx->out_stream, "reading files: " PROGRESS_BAR_FMT,
                pb_ctx->files_cnt_sum, pb_ctx->files_cnt_goal_sum,
                total_percentage);
        break;

    case PROGRESS_BAR_PERSLAVE:
        fprintf(pb_ctx->out_stream, "reading files: total: " PROGRESS_BAR_FMT,
                pb_ctx->files_cnt_sum, pb_ctx->files_cnt_goal_sum,
                total_percentage);

        for (uint64_t i = 0; i < pb_ctx->sources_cnt; ++i) {
            fprintf(pb_ctx->out_stream, " | %" PRIu64 ": " PROGRESS_BAR_FMT,
                    i + 1, pb_ctx->files_cnt[i], pb_ctx->files_cnt_goal[i],
                    source_percentage[i]);
        }
        break;

    case PROGRESS_BAR_JSON:
        fprintf(pb_ctx->out_stream, "{\"total\":%.0f", total_percentage);
        for (uint64_t i = 0; i < pb_ctx->sources_cnt; ++i) {
            fprintf(pb_ctx->out_stream, ",\"slave%" PRIu64 "\":%.0f", i + 1,
                    source_percentage[i]);
        }
        putc('}', pb_ctx->out_stream);
        break;

    case PROGRESS_BAR_NONE: // fall through
    case PROGRESS_BAR_UNSET:
        assert(!"illegal progress bar type");
    default:
        assert(!"unknown progress bar type");
        break;
    }

    // handle different behavior for streams and for files
    if (pb_ctx->out_stream == stdout || pb_ctx->out_stream == stderr) {  // stream
        if (pb_ctx->files_cnt_sum == pb_ctx->files_cnt_goal_sum) {
            fprintf(pb_ctx->out_stream, " DONE\n");  // finished, break line
        } else {
            fprintf(pb_ctx->out_stream, " ...\r");  // not finished, return carriage
        }
    } else {  // file
        putc('\n', pb_ctx->out_stream);  // proper text file termination
        rewind(pb_ctx->out_stream);
    }

    fflush(pb_ctx->out_stream);
}

/**
 * @brief TODO
 *
 * This function is not thread-safe without MPI_THREAD_MULTIPLE.
 */
static void
progress_bar_thread(progress_bar_type_t type, char *dest)
{
    PRINT_DEBUG("launching master's progress bar thread");

    ////////////////////////////////////////////////////////////////////////////
    // initialization
    struct progress_bar_ctx pb_ctx = { 0 };
    pb_ctx.type = type;
    int comm_size;
    MPI_Comm_size(mpi_comm_progress_bar, &comm_size);
    assert(comm_size > 0);
    pb_ctx.sources_cnt = comm_size - 1;  // sources are only slaves

    // allocate memory to keep a context
    pb_ctx.files_cnt = calloc(pb_ctx.sources_cnt, sizeof (*pb_ctx.files_cnt));
    pb_ctx.files_cnt_goal = calloc(pb_ctx.sources_cnt, sizeof (*pb_ctx.files_cnt_goal));
    ERROR_IF(!pb_ctx.files_cnt || !pb_ctx.files_cnt_goal, E_MEM,
             "progress bar memory allocation failed");

    if (!dest || strcmp(dest, "stderr") == 0) {
        pb_ctx.out_stream = stderr;  // stderr is the default output stream
    } else if (strcmp(dest, "stdout") == 0) {
        pb_ctx.out_stream = stdout;
    } else {  // destination is a file
        pb_ctx.out_stream = fopen(dest, "w");
        if (!pb_ctx.out_stream) {
            PRINT_WARNING(E_ARG, 0, "invalid progress bar destination `%s\': "
                          "%s", dest, strerror(errno));
            pb_ctx.type = PROGRESS_BAR_NONE;  // disable progress bar
        }
    }

    // gather the number of files to be processed by each source (goals)
    uint64_t tmp_zero = 0;  // sendbuf for MPI_Gather
    uint64_t files_cnt_goal_sum_tmp[pb_ctx.sources_cnt + 1];  // recvbuf
    MPI_Gather(&tmp_zero, 1, MPI_UINT64_T, files_cnt_goal_sum_tmp, 1,
               MPI_UINT64_T, ROOT_PROC, mpi_comm_progress_bar);

    // compute a sum of number of files to be processed (goals sum)
    for (uint64_t i = 0; i < pb_ctx.sources_cnt; ++i) {
        pb_ctx.files_cnt_goal[i] = files_cnt_goal_sum_tmp[i + 1];
        pb_ctx.files_cnt_goal_sum += pb_ctx.files_cnt_goal[i];
    }

    // print the progress bar for the first time
    if (pb_ctx.type != PROGRESS_BAR_NONE) {
        progress_bar_print(&pb_ctx);
    }

    ////////////////////////////////////////////////////////////////////////////
    // receiving and printing loop
    for (size_t i = 0; i < pb_ctx.files_cnt_goal_sum; ++i) {
        MPI_Status status;
        MPI_Recv(NULL, 0, MPI_BYTE, MPI_ANY_SOURCE, TAG_PROGRESS,
                mpi_comm_progress_bar, &status);
        assert(status.MPI_SOURCE > 0);
        assert(status.MPI_TAG == TAG_PROGRESS);

        const uint64_t source = status.MPI_SOURCE - 1;  // first source is n. 1
        pb_ctx.files_cnt[source]++;
        pb_ctx.files_cnt_sum++;

        if (pb_ctx.type != PROGRESS_BAR_NONE) {
            progress_bar_print(&pb_ctx);
        }
    }

    ////////////////////////////////////////////////////////////////////////////
    // destruction
    free(pb_ctx.files_cnt);
    free(pb_ctx.files_cnt_goal);

    // close the output stream only if it's a file
    if (pb_ctx.out_stream && pb_ctx.out_stream != stdout
            && pb_ctx.out_stream != stderr
            && fclose(pb_ctx.out_stream) == EOF)
    {
        PRINT_WARNING(E_INTERNAL, 0, "progress bar: %s", strerror(errno));
    }
}
/**
 * @}
 */  // progress_bar_master


/**
 * @brief TODO
 *
 * This function is not thread-safe without MPI_THREAD_MULTIPLE.
 */
static void
recv_loop(struct master_ctx *const m_ctx, const uint64_t source_cnt,
          const uint64_t rec_limit, const int mpi_tag,
          const recv_callback_t recv_callback, void *const callback_data)
{
    assert(m_ctx && source_cnt > 0 && recv_callback);

    // start a first nonblocking receive from any source
    bool buff_idx = 0;
    MPI_Request request;
    MPI_Irecv(m_ctx->rec_buff[buff_idx], XCHG_BUFF_SIZE, MPI_BYTE,
              MPI_ANY_SOURCE, mpi_tag, mpi_comm_main, &request);

    // receiving loop
    uint64_t rec_cntr = 0;  // processed records counter
    uint64_t msg_cntr = 0;  // received messages counter
    bool limit_exceeded = false;
    uint64_t active_sources = source_cnt;
    PRINT_DEBUG("recv_loop: receiving from %" PRIu64 " source(s)",
                active_sources);
    while (active_sources) {
        // wait for a message from any source
        MPI_Status status;
        MPI_Wait(&request, &status);
        assert(status.MPI_TAG == mpi_tag);

        // determine actual size of received message
        int msg_size;
        MPI_Get_count(&status, MPI_BYTE, &msg_size);
        assert(msg_size >= 0);

        if (msg_size == 0) {  // empty message is a terminator
            active_sources--;
            PRINT_DEBUG("recv_loop: received termination, %" PRIu64
                        " source(s) remaining", active_sources);

            // start receiving next message into the same buffer
            MPI_Irecv(m_ctx->rec_buff[buff_idx], XCHG_BUFF_SIZE, MPI_BYTE,
                      MPI_ANY_SOURCE, mpi_tag, mpi_comm_main, &request);
            continue;  // do not receive any more messages from this source
        }
        msg_cntr++;  // do not include terminators in the counter

        // rec_ptr is a pointer to record in the record buffer
        uint8_t *rec_ptr = m_ctx->rec_buff[buff_idx];
        // msg_end is a pointer to the end of the last record
        const uint8_t *const msg_end = rec_ptr + msg_size;

        // toggle buffers and start receiving next message into the free buffer
        buff_idx = !buff_idx;
        MPI_Irecv(m_ctx->rec_buff[buff_idx], XCHG_BUFF_SIZE, MPI_BYTE,
                  MPI_ANY_SOURCE, mpi_tag, mpi_comm_main, &request);

        if (limit_exceeded) {
            continue;  // do not process further, but continue receiving
        }

        /*
         * Call the callback function for each record in the received message.
         * Each record is prefixed with a 4 bytes long record size.
         */
        while (rec_ptr < msg_end) {
            const xchg_rec_size_t rec_size = *(xchg_rec_size_t *)(rec_ptr);

            rec_ptr += sizeof (rec_size);  // shift the pointer to the record
            const error_code_t ecode = recv_callback(rec_ptr, rec_size,
                                                     callback_data);
            ERROR_IF(ecode != E_OK, ecode, "recv_callback() failed");

            rec_ptr += rec_size; // shift the pointer to the next record
            if (++rec_cntr == rec_limit) {
                limit_exceeded = true;
                break;
            }
        }
    }

    PRINT_DEBUG("recv_loop: received %" PRIu64 " message(s) with tag %d "
                "containing %" PRIu64 " records", msg_cntr, mpi_tag, rec_cntr);
}


/**
 * @defgroup master_tput Master's side of the TPUT Top-N algorithm.
 *
 * This group contains an implementation of an algorithm to answer Top-N queries
 * (e.g., find the N objects with the highest aggregate values) in a distributed
 * network. It requires aggregation + record limit + sorting by one of traffic
 * volume fields (data octets, packets, out bytes, out packets and aggregated
 * flows). It supports both descending and ascending order directions.
 *
 * Naive methods for answering these queries would require to send data about
 * all records to the master. Since the number of records can be high, it could
 * be expensive to a) transfer all there records, and b) aggregate and sort them
 * on the master node.
 *
 * This implementation uses a modified TPUT algorithm presented in "Efficient
 * Top-K Query Calculation in Distributed Networks" by Pei Cao and Zhe Wang. Our
 * implementation does not prune as many records as possible, but we do not
 * target on such large cluststers, so it doesnt matter that much.
 *
 * @{
 */
/**
 * @brief Master's TPUT phase 1: find the so called bottom.
 *
 * The libnf memory should contain from 0 to source_cnt * N sorted and
 * aggregated records (partial sums). This function finds the bottom, which is:
 *   - 0 if there are no records,
 *   - the value of the last partial sum if there are less then N records,
 *   - the value of the Nth partial sum if there are N or more records (this is
 *     the most common case).
 *
 * @param[in] lnf_mem The libnf memory. Will not be modified.
 *
 * @return Value of the phase 1 bottom.
 */
static uint64_t
tput_phase_1_find_bottom(lnf_mem_t *const lnf_mem)
{
    assert(lnf_mem && args->rec_limit);

    // set the cursor to point to the Nth record (or the last record if there
    // are less then N records)
    lnf_mem_cursor_t *cursor;
    int lnf_ret = lnf_mem_first_c(lnf_mem, &cursor);
    assert((cursor && lnf_ret == LNF_OK) || (!cursor && lnf_ret == LNF_EOF));
    if (lnf_ret == LNF_EOF) {
        PRINT_DEBUG("master TPUT phase 1: bottom = 0, position = 0");
        return 0;  // memory is empty, return zero
    }
    lnf_mem_cursor_t *cursor_nth_or_last = NULL;
    uint64_t cursor_position = 0;
    while (cursor && cursor_position < args->rec_limit) {
        cursor_nth_or_last = cursor;  // cursor is not NULL

        lnf_ret = lnf_mem_next_c(lnf_mem, &cursor);
        assert((cursor && lnf_ret == LNF_OK) || (!cursor && lnf_ret == LNF_EOF));
        cursor_position++;
    }

    // initialize the libnf record
    lnf_rec_t *lnf_rec;
    lnf_ret = lnf_rec_init(&lnf_rec);
    assert(lnf_ret == LNF_OK);

    // read the Nth (or the last) record
    lnf_ret = lnf_mem_read_c(lnf_mem, cursor_nth_or_last, lnf_rec);
    assert(lnf_ret == LNF_OK);

    // extract the value of the sort key from the record
    uint64_t bottom;
    assert(args->fields_sort_key);
    lnf_ret = lnf_rec_fget(lnf_rec, args->fields_sort_key, &bottom);
    assert(lnf_ret == LNF_OK);

    lnf_rec_free(lnf_rec);

    PRINT_DEBUG("master TPUT phase 1: bottom = %" PRIu64 ", position = %"
                PRIu64, bottom, cursor_position);
    return bottom;
}

/**
 * @brief Master's TPUT phase 1: establish a lower bound on the true bottom.
 *
 * After receiving the data from all sources, the master calculates the partial
 * sums of the objects. It then looks at the N highest partial sums, and takes
 * the Nth one as the lower bound aka ``phase 1 bottom''.
 *
 * @param[in] m_ctx Master context.
 * @param[out] lnf_mem Empty libnf memory.
 *
 * @return Value of the phase 1 bottom.
 */
static uint64_t
tput_phase_1(struct master_ctx *const m_ctx, lnf_mem_t *const lnf_mem)
{
    assert(m_ctx && lnf_mem);
    const uint64_t lnf_mem_rec_cnt = libnf_mem_rec_cnt(lnf_mem);
    assert(lnf_mem_rec_cnt == 0);

    // receive the top N items from each source and aggregate (aggregation will
    // calculate the partial sums)
    recv_loop(m_ctx, m_ctx->slave_threads_cnt, 0, TAG_TPUT1,
              mem_write_raw_callback, lnf_mem);

    const uint64_t bottom = tput_phase_1_find_bottom(lnf_mem);

    PRINT_DEBUG("master TPUT phase 1: done");
    return bottom;
}

/**
 * @brief Master's TPUT phase 2: prune away ineligible objects.
 *
 * The master now sets a threshold = (phase 1 bottom / source_cnt), and sends it
 * to all sources.
 * The master the receives all records (from all sources) satisfying the
 * threshold. At the end of this round-trip, the master has seen records in the
 * true Top-N set. This is a set S.
 *
 * This function is not thread-safe without MPI_THREAD_MULTIPLE.
 *
 * @param[in] m_ctx Master context.
 * @param[out] lnf_mem Pointer to the libnf memory. Will be cleared and filled
 *                     with phase 2 top N records.
 * @param[in] phase_1_bottom Value of the phase 1 bottom.
 */
static void
tput_phase_2(struct master_ctx *const m_ctx, lnf_mem_t **const lnf_mem,
             const uint64_t phase_1_bottom)
{
    assert(m_ctx && lnf_mem && *lnf_mem);

    // clear the libnf memory
    libnf_mem_free(*lnf_mem);
    error_code_t ecode = libnf_mem_init(lnf_mem, args->fields, false);
    assert(ecode == E_OK);

    // calculate threshold from the phase 1 bottom and broadcast it
    uint64_t threshold = ceil((double)phase_1_bottom / m_ctx->slave_threads_cnt);
    MPI_Bcast(&threshold, 1, MPI_UINT64_T, ROOT_PROC, mpi_comm_main);
    PRINT_DEBUG("master TPUT phase 2: broadcasted threshold = %" PRIu64,
                threshold);

    // receive all records satisfying the threshold
    recv_loop(m_ctx, m_ctx->slave_threads_cnt, 0, TAG_TPUT2,
              mem_write_raw_callback, *lnf_mem);

    PRINT_DEBUG("master TPUT phase 2: done");
}

/**
 * @brief Master's TPUT phase 3: identify the top N objects.
 *
 * The master sends the set S to all nodes. Slaves return matching records. The
 * master then then calculate the exact sum of objects in S, and select the top
 * N objects from the set. Those objects are the true top N objects.
 *
 * This function is not thread-safe without MPI_THREAD_MULTIPLE.
 *
 * @param[in] m_ctx Master context.
 * @param[in,out] lnf_mem Pointer to the libnf memory with phase 2 top N
 *                        records. Will be cleared and filled with the true top
 *                        N records.
 */
static void
tput_phase_3(struct master_ctx *const m_ctx, lnf_mem_t **const lnf_mem)
{
    assert(m_ctx && lnf_mem && *lnf_mem);

    // query and broadcast the number of records in the memory
    uint64_t rec_cnt = libnf_mem_rec_cnt(*lnf_mem);
    MPI_Bcast(&rec_cnt, 1, MPI_UINT64_T, ROOT_PROC, mpi_comm_main);

    // read and broadcast all records
    lnf_mem_cursor_t *cursor;
    int lnf_ret = lnf_mem_first_c(*lnf_mem, &cursor);
    assert((cursor && lnf_ret == LNF_OK) || (!cursor && lnf_ret == LNF_EOF));
    for (size_t i = 0; i < rec_cnt; ++i) {
        char rec_buff[LNF_MAX_RAW_LEN];
        int rec_len = 0;
        lnf_ret = lnf_mem_read_raw_c(*lnf_mem, cursor, rec_buff, &rec_len,
                                     sizeof (rec_buff));
        assert(rec_len > 0 && lnf_ret == LNF_OK);

        MPI_Bcast(&rec_len, 1, MPI_INT, ROOT_PROC, mpi_comm_main);
        MPI_Bcast(rec_buff, rec_len, MPI_BYTE, ROOT_PROC, mpi_comm_main);

        lnf_ret = lnf_mem_next_c(*lnf_mem, &cursor);
        assert((cursor && lnf_ret == LNF_OK) || (!cursor && lnf_ret == LNF_EOF));
    }
    PRINT_DEBUG("master TPUT phase 3: broadcasted %" PRIu64 " records",
                rec_cnt);

    // clear the libnf memory
    libnf_mem_free(*lnf_mem);
    error_code_t ecode = libnf_mem_init(lnf_mem, args->fields, false);
    assert(ecode == E_OK);

    recv_loop(m_ctx, m_ctx->slave_threads_cnt, 0, TAG_TPUT3,
              mem_write_raw_callback, *lnf_mem);
    PRINT_DEBUG("master TPUT phase 3: done");
}
/**
 * @}
 */  // slave_tput


/**
 * @brief TODO
 */
static void
list_main(struct master_ctx *const m_ctx)
{
    assert(m_ctx);

    recv_loop(m_ctx, m_ctx->slave_threads_cnt, args->rec_limit, TAG_LIST,
              print_rec_callback, NULL);
}

/**
 * @brief TODO
 */
static void
sort_main(struct master_ctx *const m_ctx)
{
    assert(m_ctx);

    struct mem_write_callback_data mwcd = { 0 };

    // fill the compressed fields array
    for (size_t i = 0; i < LNF_FLD_TERM_; ++i) {
        if (args->fields[i].id == 0) {
            continue;  // field is not present
        }
        mwcd.fields[mwcd.fields_cnt].id = i;
        mwcd.fields[mwcd.fields_cnt].size = field_get_size(i);
        mwcd.fields_cnt++;
    }

    // initialize the libnf sorting memory and set its parameters
    libnf_mem_init(&mwcd.lnf_mem, args->fields, true);

    // initialize empty libnf record for writing
    int lnf_ret = lnf_rec_init(&mwcd.lnf_rec);
    ERROR_IF(lnf_ret != LNF_OK, E_LNF, "lnf_rec_init()");

    // fill the libnf linked list memory with records received from the slaves
    recv_loop(m_ctx, m_ctx->slave_threads_cnt, 0, TAG_SORT,
              mem_write_raw_callback, mwcd.lnf_mem);

    // sort record in the libnf memory (not needed)
    PRINT_DEBUG("sorting records in master's libnf memory...");
    libnf_mem_sort(mwcd.lnf_mem);
    PRINT_DEBUG("sorting records in master's libnf memory done");

    // print all records in the libnf linked list memory
    print_mem(mwcd.lnf_mem, args->rec_limit);

    lnf_rec_free(mwcd.lnf_rec);
    libnf_mem_free(mwcd.lnf_mem);
}

/**
 * @brief TODO
 */
static void
aggr_main(struct master_ctx *const m_ctx)
{
    assert(m_ctx);

    // initialize aggregation memory and set its parameters
    lnf_mem_t *lnf_mem;
    libnf_mem_init(&lnf_mem, args->fields, false);

    if (args->use_tput) {
        // use the TPUT Top-N algorithm
        const uint64_t phase_1_bottom = tput_phase_1(m_ctx, lnf_mem);
        tput_phase_2(m_ctx, &lnf_mem, phase_1_bottom);
        tput_phase_3(m_ctx, &lnf_mem);
    } else {
        // fill the libnf hash table memory with records received from the slaves
        recv_loop(m_ctx, m_ctx->slave_threads_cnt, 0, TAG_AGGR,
                  mem_write_raw_callback, lnf_mem);
    }

    // print all records the lnf hash table memory
    print_mem(lnf_mem, args->rec_limit);

    libnf_mem_free(lnf_mem);
}

/**
 * @brief TODO
 */
static void
master_main_thread(void)
{
    PRINT_DEBUG("launching master's main thread");

    // get a sum of number of threads used on all slaves
    int slave_threads_cnt = 0;
    MPI_Reduce(MPI_IN_PLACE, &slave_threads_cnt, 1, MPI_INT, MPI_SUM, ROOT_PROC,
               mpi_comm_main);
    assert(slave_threads_cnt > 0);

    // initialize a master_ctx structure
    struct master_ctx *const m_ctx =
        master_ctx_init((uint64_t)slave_threads_cnt);
    PRINT_DEBUG("using %d slave thread(s) in total", m_ctx->slave_threads_cnt);

    output_setup(args->output_params, args->fields);

    // send, receive, process according to the specified working mode
    switch (args->working_mode) {
    case MODE_LIST:
        list_main(m_ctx);
        break;
    case MODE_SORT:
        sort_main(m_ctx);
        break;
    case MODE_AGGR:
        aggr_main(m_ctx);
        break;
    case MODE_META:
        // receive only the progress
        break;
    default:
        assert(!"unknown working mode");
    }

    master_ctx_free(m_ctx);
}

/*
 * Public functions.
 */
/**
 * @brief Master's process entry point.
 *
 * Entry point to the code executed only by the master process (usually with
 * rank 0).
 *
 * This function is not thread-safe without MPI_THREAD_MULTIPLE.
 *
 * @param[in] args Parsed command-line arguments.
 */
void
master_main(const struct cmdline_args *args_local)
{
    assert(args_local);

    double duration = -MPI_Wtime();  // start the time measurement
    args = args_local;  // share the command-line arguments by a global variable

    // spawn one thread for each section
    #pragma omp parallel sections num_threads(2)
    {
        // master's main thread
        #pragma omp section
        {
            assert(mpi_comm_main != MPI_COMM_NULL);
            master_main_thread();
        }

        // progress bar handling section (thread)
        #pragma omp section
        {
            assert(mpi_comm_progress_bar != MPI_COMM_NULL);
            progress_bar_thread(args->progress_bar_type,
                                args->progress_bar_dest);
        }
    }

    // reduce statistic values from each slave
    struct processed_summ processed_summ = { 0 };  // processed data statistics
    MPI_Reduce(MPI_IN_PLACE, &processed_summ, STRUCT_PROCESSED_SUMM_ELEMENTS,
               MPI_UINT64_T, MPI_SUM, ROOT_PROC, mpi_comm_main);
    struct metadata_summ metadata_summ = { 0 }; // metadata statistics
    MPI_Reduce(MPI_IN_PLACE, &metadata_summ, STRUCT_METADATA_SUMM_ELEMENTS,
               MPI_UINT64_T, MPI_SUM, ROOT_PROC, mpi_comm_main);

    duration += MPI_Wtime();  // stop the time measurement

    print_processed_summ(&processed_summ, duration);
    print_metadata_summ(&metadata_summ);
}
