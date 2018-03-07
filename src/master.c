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

#include "common.h"
#include "master.h"
#include "output.h"
#include "print.h"

#include <string.h> //strlen()
#include <stdbool.h>
#include <assert.h>
#include <errno.h>

#include <mpi.h>
#include <libnf.h>


/*
 * Global variables.
 */
static const struct cmdline_args *args;

static struct progress_bar_ctx {
    progress_bar_type_t type;
    size_t slave_cnt;
    size_t *files_slave_cur;  // slave_cnt sized
    size_t *files_slave_sum;  // slave_cnt sized
    size_t files_cur;
    size_t files_sum;
    FILE *out_stream;
} progress_bar_ctx;


/*
 * Data types declarations.
 */
struct mem_write_callback_data {
    lnf_mem_t *mem;
    lnf_rec_t *rec;

    struct {
        int id;
        size_t size;
    } fields[LNF_FLD_TERM_];  // fields array compressed for faster access
    size_t fields_cnt;        // number of fields present in the fields array
};

typedef error_code_t (*recv_callback_t)(uint8_t *data, size_t data_len,
                                        void *user);


/*
 * Static functions.
 */
static error_code_t
mem_write_callback(uint8_t *data, size_t data_len, void *user)
{
    (void)data_len;  // unused

    struct mem_write_callback_data *mwcd =
        (struct mem_write_callback_data *)user;

    size_t offset = 0;
    for (size_t i = 0; i < mwcd->fields_cnt; ++i) {
        int lnf_ret = lnf_rec_fset(mwcd->rec, mwcd->fields[i].id,
                                   data + offset);
        if (lnf_ret != LNF_OK) {
            PRINT_ERROR(E_LNF, lnf_ret, "lnf_rec_fset()");
            return E_LNF;
        }
        offset += mwcd->fields[i].size;
    }

    int lnf_ret = lnf_mem_write(mwcd->mem, mwcd->rec);
    if (lnf_ret != LNF_OK) {
        PRINT_ERROR(E_LNF, lnf_ret, "lnf_mem_write()");
        return E_LNF;
    }

    return E_OK;
}

static error_code_t
mem_write_raw_callback(uint8_t *data, size_t data_len, void *user)
{
    int lnf_ret = lnf_mem_write_raw((lnf_mem_t *)user, (char *)data, data_len);
    if (lnf_ret != LNF_OK) {
        PRINT_ERROR(E_LNF, lnf_ret, "lnf_mem_write_raw()");
        return E_LNF;
    }

    return E_OK;
}

static error_code_t
print_rec_callback(uint8_t *data, size_t data_len, void *user)
{
        (void)data_len;  // unused
        (void)user;  // unused

        print_rec(data);
        return E_OK;
}


/**
 * @brief TODO
 */
static void
progress_bar_print(void)
{
    struct progress_bar_ctx *pbc = &progress_bar_ctx; //global variable

    // calculate the percentage progress for each slave
    double slave_percentage[pbc->slave_cnt];
    for (size_t i = 0; i < pbc->slave_cnt; ++i) {
        assert(pbc->files_slave_cur[i] <= pbc->files_slave_sum[i]);
        if (pbc->files_slave_sum[i] == 0) {
            slave_percentage[i] = 100.0;
        } else {
            slave_percentage[i] = (double)pbc->files_slave_cur[i]
                / pbc->files_slave_sum[i] * 100.0;
        }
    }

    // calculate the total percentage progress
    assert(pbc->files_cur <= pbc->files_sum);
    double total_percentage;
    if (pbc->files_sum == 0) {
        total_percentage = 100.0;
    } else {
        total_percentage = (double)pbc->files_cur / pbc->files_sum * 100.0;
    }

    // diverge for each progress bar type
    switch (pbc->type) {
    case PROGRESS_BAR_TOTAL:
        fprintf(pbc->out_stream, "reading files: %zu/%zu (%.0f %%)",
                pbc->files_cur, pbc->files_sum, total_percentage);
        break;

    case PROGRESS_BAR_PERSLAVE:
        fprintf(pbc->out_stream, ":reading files: total: %zu/%zu (%.0f %%)",
                pbc->files_cur, pbc->files_sum, total_percentage);

        for (size_t i = 0; i < pbc->slave_cnt; ++i) {
            fprintf(pbc->out_stream, " | %zu: %zu/%zu (%.0f %%)", i + 1,
                    pbc->files_slave_cur[i], pbc->files_slave_sum[i],
                    slave_percentage[i]);
        }
        break;

    case PROGRESS_BAR_JSON:
        fprintf(pbc->out_stream, "{\"total\":%.0f", total_percentage);
        for (size_t i = 0; i < pbc->slave_cnt; ++i) {
            fprintf(pbc->out_stream, ",\"slave%zu\":%.0f", i + 1,
                    slave_percentage[i]);
        }
        putc('}', pbc->out_stream);
        break;

    case PROGRESS_BAR_NONE: // fall through
    case PROGRESS_BAR_UNSET:
        assert(!"illegal progress bar type");
    default:
        assert(!"unknown progress bar type");
        break;
    }

    // handle different behavior for streams and for files
    if (pbc->out_stream == stdout || pbc->out_stream == stderr) {  // stream
        if (pbc->files_cur == pbc->files_sum) {
            putc('\n', pbc->out_stream);  // finished, break line
        } else {
            putc('\r', pbc->out_stream);  // not finished, return carriage
        }
    } else {  // file
        putc('\n', pbc->out_stream);  // proper text file termination
        rewind(pbc->out_stream);
    }

    fflush(pbc->out_stream);
}

/**
 * @brief TODO
 *
 * Progress bar initialization: gather file count from each slave etc.
 *
 * @param type
 * @param dest
 * @param slave_cnt
 *
 * @return 
 */
static error_code_t
progress_bar_init(progress_bar_type_t type, char *dest, size_t slave_cnt)
{
    assert(slave_cnt > 0);

    struct progress_bar_ctx *pbc = &progress_bar_ctx; //global variable

    pbc->type = type;  // store progress bar type
    pbc->slave_cnt = slave_cnt;  // store slave count

    // allocate memory to keep a context
    pbc->files_slave_cur = calloc(pbc->slave_cnt,
                                  sizeof (*pbc->files_slave_cur));
    pbc->files_slave_sum = calloc(pbc->slave_cnt,
                                  sizeof (*pbc->files_slave_sum));
    if (!pbc->files_slave_cur || !pbc->files_slave_sum) {
        PRINT_ERROR(E_MEM, 0, "calloc()");
        return E_MEM;
    }

    if (!dest || strcmp(dest, "stderr") == 0) {
        pbc->out_stream = stderr;  // stderr is the default output stream
    } else if (strcmp(dest, "stdout") == 0) {
        pbc->out_stream = stdout;
    } else {  // destination is a file
        pbc->out_stream = fopen(dest, "w");
        if (!pbc->out_stream) {
            PRINT_WARNING(E_ARG, 0, "invalid progress bar destination `%s\': "
                          "%s", dest, strerror(errno));
            pbc->type = PROGRESS_BAR_NONE;  // disable progress bar
        }
    }

    // gather the number of files to be processed by each slave
    size_t tmp_zero = 0;  // sendbuf for MPI_Gather
    size_t files_sum[slave_cnt + 1];  // recvbuf for MPI_Gather
    MPI_Gather(&tmp_zero, 1, MPI_UNSIGNED_LONG, files_sum, 1, MPI_UNSIGNED_LONG,
               ROOT_PROC, MPI_COMM_WORLD);

    // compute a sum of number of files to be processed
    for (size_t i = 0; i < slave_cnt; ++i) {
        pbc->files_slave_sum[i] = files_sum[i + 1];
        pbc->files_sum += pbc->files_slave_sum[i];
    }

    // print the progress bar for the first time
    if (pbc->type != PROGRESS_BAR_NONE) {
        progress_bar_print();
    }

    return E_OK;
}

/**
 * @brief TODO
 *
 * @param source
 *
 * @return 
 */
static bool
progress_bar_refresh(int source)
{
    assert(source > 0);

    struct progress_bar_ctx *pbc = &progress_bar_ctx;  // global variable

    pbc->files_slave_cur[source - 1]++;
    pbc->files_cur++;

    if (pbc->type != PROGRESS_BAR_NONE) {
        progress_bar_print();
    }

    return pbc->files_cur == pbc->files_sum;
}

/**
 * @brief TODO
 */
static void
progress_bar_loop(void)
{
    for (size_t i = 0; i < progress_bar_ctx.files_sum; ++i) {
        MPI_Status status;
        MPI_Recv(NULL, 0, MPI_BYTE, MPI_ANY_SOURCE, TAG_PROGRESS,
                 MPI_COMM_WORLD, &status);
        progress_bar_refresh(status.MPI_SOURCE);
    }
}

/**
 * @brief TODO
 */
static void
progress_bar_finish(void)
{
    struct progress_bar_ctx *pbc = &progress_bar_ctx;  // global variable

    free(pbc->files_slave_cur);
    free(pbc->files_slave_sum);

    // close the output stream only if it's a file
    if (pbc->out_stream && pbc->out_stream != stdout
            && pbc->out_stream != stderr && fclose(pbc->out_stream) == EOF)
    {
        PRINT_WARNING(E_INTERNAL, 0, "progress bar: %s", strerror(errno));
    }
}


/**
 * @brief TODO
 *
 * @param mem
 *
 * @return 
 */
static error_code_t
fast_topn_bcast_all(lnf_mem_t *mem)
{
    assert(mem);

    // TODO: handle error, if no records received
    lnf_mem_cursor_t *read_cursor;
    int lnf_ret = lnf_mem_first_c(mem, &read_cursor);
    if (lnf_ret == LNF_EOF) {
        goto send_terminator;  // no records in memory, terminate
    } else if (lnf_ret != LNF_OK) {
        PRINT_ERROR(E_LNF, lnf_ret, "lnf_mem_first_c()");
        return E_LNF;
    }

    // read and broadcast all records
    while (true) {
        uint8_t rec_buff[LNF_MAX_RAW_LEN]; // TODO: send mutliple records
        int rec_len = 0;
        lnf_ret = lnf_mem_read_raw_c(mem, read_cursor,
                                     (char *)rec_buff, &rec_len,
                                     LNF_MAX_RAW_LEN);
        assert(lnf_ret != LNF_EOF);
        if (lnf_ret != LNF_OK) {
            PRINT_ERROR(E_LNF, lnf_ret, "lnf_mem_read_raw_c()");
            return E_LNF;
        }

        MPI_Bcast(&rec_len, 1, MPI_INT, ROOT_PROC, MPI_COMM_WORLD);
        MPI_Bcast(rec_buff, rec_len, MPI_BYTE, ROOT_PROC, MPI_COMM_WORLD);

        lnf_ret = lnf_mem_next_c(mem, &read_cursor);
        if (lnf_ret == LNF_EOF) {
            break;  // all records read and sent
        } else if (lnf_ret != LNF_OK) {
            PRINT_ERROR(E_LNF, lnf_ret, "lnf_mem_next_c()");
            return E_LNF;
        }
    }

send_terminator: ;  // semicolon for an empty labeled statement
    // phase 2 done, notify slaves by the message of zero length
    int tmp_rec_len = 0;
    MPI_Bcast(&tmp_rec_len, 1, MPI_INT, ROOT_PROC, MPI_COMM_WORLD);

    return E_OK;
}

/**
 * @brief TODO
 *
 * @param slave_cnt
 * @param rec_limit
 * @param recv_callback
 * @param user
 *
 * @return 
 */
static error_code_t
recv_loop(size_t slave_cnt, size_t rec_limit, recv_callback_t recv_callback,
          void *user)
{
    assert(slave_cnt > 0 && recv_callback);

    error_code_t ecode = E_OK;

    // data receiving loop
    size_t rec_cntr = 0;
    bool limit_exceeded = false;
    size_t active_slaves = slave_cnt;  // every slave starts as active
    while (active_slaves) {
        // receive a message from any slave
        uint8_t rec_buff[LNF_MAX_RAW_LEN];  // TODO: receive mutliple records
        MPI_Status status;
        MPI_Recv(rec_buff, LNF_MAX_RAW_LEN, MPI_BYTE, MPI_ANY_SOURCE, TAG_DATA,
                 MPI_COMM_WORLD, &status);

        // determine actual size of the received message
        int rec_len = 0;
        MPI_Get_count(&status, MPI_BYTE, &rec_len);
        if (rec_len == 0) {
            active_slaves--;
            continue; // empty message means the slave has finished
        }

        if (limit_exceeded) {
            continue;  // do not process further but continue receiving
        }

        // call the callback function for each received record
        ecode = recv_callback(rec_buff, rec_len, user);
        if (ecode != E_OK) {
            break;
        }

        if (++rec_cntr == rec_limit) {
            limit_exceeded = true;
        }
    }

    return ecode;
}

/**
 * @brief TODO
 *
 * @param slave_cnt
 * @param rec_limit
 * @param recv_callback
 * @param user
 *
 * @return 
 */
static error_code_t
irecv_loop(size_t slave_cnt, size_t rec_limit, recv_callback_t recv_callback,
           void *user)
{
    assert(slave_cnt > 0 && recv_callback);

    error_code_t ecode = E_OK;


    /*
     * There are two receive buffers for each slave. Each buffer is a part of a
     * bich chunk of a continuous memory.
     * The first buffer is passed to the nonblocking MPI receive function while
     * the second one is being processed. After both these operations are
     * completed, buffers are switched. Buffer switching (toggling) is
     * independent for each slave, that's why array buff_idx[slave_cnt] is
     * needed.
     *
     * buff_mem is partitioned in buff in the following manner:
     *
     * <--------- XCHG_BUFF_SIZE -------> <-------- XCHG_BUFF_SIZE -------->
     * ---------------------------------------------------------------------
     * |           buff[0][0]            |            buff[0][1]           |
     * --------------------------------------------------------------------
     * |           buff[1][0]            |            buff[1][1]           |
     * ---------------------------------------------------------------------
     * .                                                                   .
     * .                                                                   .
     * .                                                                   .
     * ---------------------------------------------------------------------
     * |     buff[slave_cnt - 1][0]      |     buff[slave_cnt - 1][1]      |
     * ---------------------------------------------------------------------
     */
    uint8_t *const buff_mem = malloc(2 * XCHG_BUFF_SIZE * slave_cnt
                                     * sizeof (*buff_mem));
    if (!buff_mem) {
        PRINT_ERROR(E_MEM, 0, "malloc()");
        return E_MEM;
    }

    uint8_t *buff[slave_cnt][2];  // pointers to the buff_mem
    MPI_Request requests[slave_cnt + 1];  // each slave plus one for progress
    for (size_t i = 0; i < slave_cnt; ++i) {
        requests[i] = MPI_REQUEST_NULL;

        buff[i][0] = buff_mem + (i * 2 * XCHG_BUFF_SIZE);
        buff[i][1] = buff[i][0] + XCHG_BUFF_SIZE;
    }

    bool buff_idx[slave_cnt]; //indexes to the currently used data buffers
    memset(buff_idx, 0, slave_cnt * sizeof (buff_idx[0]));


    // start a first individual nonblocking data receive from each slave
    for (size_t i = 0; i < slave_cnt; ++i) {
        uint8_t *free_buff = buff[i][buff_idx[i]];
        MPI_Irecv(free_buff, XCHG_BUFF_SIZE, MPI_BYTE, i + 1, TAG_DATA,
                  MPI_COMM_WORLD, &requests[i]);
    }
    // start a first nonblocking progress report receive from any slave
    if (progress_bar_ctx.files_sum > 0) {
        MPI_Irecv(NULL, 0, MPI_BYTE, MPI_ANY_SOURCE, TAG_PROGRESS,
                  MPI_COMM_WORLD, &requests[slave_cnt]);
    } else {
        requests[slave_cnt] = MPI_REQUEST_NULL;
    }

    // data receiving loop
    size_t rec_cntr = 0;  // processed records
    size_t byte_cntr = 0;  // received bytes
    bool limit_exceeded = false;
    while (true) {

        /* Wait for data or status report from any slave. */
        int slave_idx;
        MPI_Status status;
        MPI_Waitany(slave_cnt + 1, requests, &slave_idx, &status);

        if (slave_idx == MPI_UNDEFINED) { //no active slaves anymore
            break;
        }

        if (status.MPI_TAG == TAG_PROGRESS) {
            bool finished = progress_bar_refresh(status.MPI_SOURCE);

            if (!finished) { //expext next progress report
                MPI_Irecv(NULL, 0, MPI_BYTE, MPI_ANY_SOURCE,
                        TAG_PROGRESS, MPI_COMM_WORLD,
                        &requests[slave_cnt]);
            }

            continue;
        }

        assert(status.MPI_TAG == TAG_DATA);

        /* Determine actual size of received message. */
        int msg_size;
        MPI_Get_count(&status, MPI_BYTE, &msg_size);
        if (msg_size == 0) {
            continue; //empty message -> slave finished
        }
        byte_cntr += msg_size;

        // rec_ptr is a pointer to record in the data buffer
        uint8_t *rec_ptr = buff[slave_idx][buff_idx[slave_idx]];
        // msg_end is a pointer to the end of the last record
        const uint8_t *const msg_end = rec_ptr + msg_size;

        // toggle buffers and start receiving next message into the free one
        buff_idx[slave_idx] = !buff_idx[slave_idx];
        MPI_Irecv(buff[slave_idx][buff_idx[slave_idx]], XCHG_BUFF_SIZE,
                  MPI_BYTE, status.MPI_SOURCE, TAG_DATA, MPI_COMM_WORLD,
                  &requests[slave_idx]);

        if (limit_exceeded) {
            continue;  // do not process further, but continue receiving
        }

        /*
         * Call the callback function for each record in the received message.
         * Each record is prefixed with a 4 bytes long record size.
         */
        while (rec_ptr < msg_end) {
            const uint32_t rec_size = *(uint32_t *)(rec_ptr);

            rec_ptr += sizeof (rec_size); // shift the pointer to the data
            ecode = recv_callback(rec_ptr, rec_size, user);
            if (ecode != E_OK) {
                goto free_db_mem;
            }

            rec_ptr += rec_size; // shift the pointer to the next record
            if (++rec_cntr == rec_limit) {
                limit_exceeded = true;
                break;
            }
        }
    }

free_db_mem:
    free(buff_mem);

    PRINT_DEBUG("processed %zu records, received %zu B", rec_cntr, byte_cntr);
    return ecode;
}


/**
 * @defgroup master_tput Master's side of the Top-N TPUT algorithm.
 * @{
 */
//error_code_t
//tput_phase_1(void)
//{
//}
/**
 * @}
 */  // slave_tput


static error_code_t
list_main(size_t slave_cnt)
{
    assert(slave_cnt > 0);
    return irecv_loop(slave_cnt, args->rec_limit, print_rec_callback, NULL);
}

static error_code_t
sort_main(size_t slave_cnt)
{
    assert(slave_cnt > 0);

    error_code_t ecode = E_OK;
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
    ecode = libnf_mem_init(&mwcd.mem, args->fields, true);
    if (ecode != E_OK) {
        return ecode;
    }

    // initialize empty libnf record for writing
    int lnf_ret = lnf_rec_init(&mwcd.rec);
    if (lnf_ret != LNF_OK) {
        ecode = E_LNF;
        PRINT_ERROR(ecode, lnf_ret, "lnf_rec_init()");
        goto free_libnf_mem;
    }

    // fill the libnf linked list memory with records received from the slaves
    if (!args->rec_limit) {
        // no output limit, have to receive all records from all slaves
        // callback cannot use raw records because slaves do not read raw
        // records
        ecode = irecv_loop(slave_cnt, 0, mem_write_callback, &mwcd);
        if (ecode != E_OK) {
            goto free_lnf_rec;
        }
    } else {
        // output limit set, have to receive at most limit-number of records
        // from each slave
        ecode = irecv_loop(slave_cnt, 0, mem_write_raw_callback,
                           mwcd.mem);
        if (ecode != E_OK) {
            goto free_lnf_rec;
        }
    }

    // print all records in the libnf linked list memory
    ecode = print_mem(mwcd.mem, args->rec_limit);

free_lnf_rec:
    lnf_rec_free(mwcd.rec);
free_libnf_mem:
    libnf_mem_free(mwcd.mem);

    return ecode;
}

static error_code_t
aggr_main(size_t slave_cnt)
{
    assert(slave_cnt > 0);

    error_code_t ecode = E_OK;

    // initialize aggregation memory and set its parameters
    lnf_mem_t *lnf_mem;
    ecode = libnf_mem_init(&lnf_mem, args->fields, false);
    if (ecode != E_OK) {
        return ecode;
    }

    // fill the libnf hash table memory with records received from the slaves
    ecode = irecv_loop(slave_cnt, 0, mem_write_raw_callback, lnf_mem);
    if (ecode != E_OK) {
        goto free_libnf_mem;
    }

    if (args->use_fast_topn) {
        // TODO: if file doesn't exist on slave, this will cause deadlock
        ecode = fast_topn_bcast_all(lnf_mem);
        if (ecode != E_OK) {
            goto free_libnf_mem;
        }

        // reset memory - all records will be received again
        // TODO: optimalization - add records to memory, don't reset
        libnf_mem_free(lnf_mem);
        ecode = libnf_mem_init(&lnf_mem, args->fields, false);
        if (ecode != E_OK) {
            return ecode;
        }

        ecode = recv_loop(slave_cnt, 0, mem_write_raw_callback, lnf_mem);
        if (ecode != E_OK) {
            goto free_libnf_mem;
        }
    }

    // print all records the lnf hash table memory
    ecode = print_mem(lnf_mem, args->rec_limit);

free_libnf_mem:
    libnf_mem_free(lnf_mem);

    return ecode;
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
 * @param[in] world_size MPI_COMM_WORLD size.
 * @param[in] args Parsed command-line arguments.
 *
 * @return Error code.
 */
error_code_t
master_main(int world_size, const struct cmdline_args *args_local)
{
    assert(world_size > 1 && args_local);

    error_code_t ecode = E_OK;
    double duration = -MPI_Wtime();  // start the time measurement
    size_t slave_cnt = world_size - 1; // all nodes without master
    args = args_local;  // share the command-line arguments by a global variable

    output_setup(args->output_params, args->fields);

    ecode = progress_bar_init(args->progress_bar_type, args->progress_bar_dest,
                              slave_cnt);
    if (ecode != E_OK) {
        goto finalize;
    }

    // send, receive, process according to the specified working mode
    switch (args->working_mode) {
    case MODE_LIST:
        ecode = list_main(slave_cnt);
        break;
    case MODE_SORT:
        ecode = sort_main(slave_cnt);
        break;
    case MODE_AGGR:
        ecode = aggr_main(slave_cnt);
        break;
    case MODE_META:
        // receive only the progress
        progress_bar_loop();
        break;
    default:
        assert(!"unknown working mode");
    }

    progress_bar_finish();

    // reduce statistic values from each slave
    struct processed_summ processed_summ = { 0 };  // processed data statistics
    MPI_Reduce(MPI_IN_PLACE, &processed_summ, STRUCT_PROCESSED_SUMM_ELEMENTS,
               MPI_UINT64_T, MPI_SUM, ROOT_PROC, MPI_COMM_WORLD);
    struct metadata_summ metadata_summ = { 0 }; // metadata statistics
    MPI_Reduce(MPI_IN_PLACE, &metadata_summ, STRUCT_METADATA_SUMM_ELEMENTS,
               MPI_UINT64_T, MPI_SUM, ROOT_PROC, MPI_COMM_WORLD);

    duration += MPI_Wtime();  // finish the time measurement

    print_processed_summ(&processed_summ, duration);
    print_metadata_summ(&metadata_summ);


finalize:
    return ecode;
}
