/**
 * \file master.c
 * \brief
 * \author Jan Wrona, <wrona@cesnet.cz>
 * \author Pavel Krobot, <Pavel.Krobot@cesnet.cz>
 * \date 2015
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
 *
 */

#include "common.h"
#include "master.h"
#include "output.h"

#include <string.h> //strlen()
#include <stdbool.h>
#include <assert.h>
#include <errno.h>

#include <mpi.h>
#include <libnf.h>


/* Global variables. */
extern MPI_Datatype mpi_struct_shared_task_ctx;
extern int secondary_errno;


struct master_task_ctx {
        /* Master and slave shared task context. */
        struct shared_task_ctx shared;

        /* Master specific task context. */
        size_t slave_cnt;
        struct output_params output_params;
};

struct mem_write_callback_data {
        lnf_mem_t *mem;
        lnf_rec_t *rec;

        struct {
                int id;
                size_t size;
        } fields[LNF_FLD_TERM_]; //fields array compressed for faster access
        size_t fields_cnt; //number of fields present in fields array
};

static struct progress_bar_ctx {
        progress_bar_type_t type;
        size_t slave_cnt;
        size_t *files_slave_cur; //slave_cnt size
        size_t *files_slave_sum; //slave_cnt size
        size_t files_cur;
        size_t files_sum;
        FILE *out_stream;
} progress_bar_ctx;


typedef error_code_t(*recv_callback_t)(uint8_t *data, size_t data_len,
                void *user);


static void construct_master_task_ctx(struct master_task_ctx *mtc,
                const struct cmdline_args *args, int world_size)
{
        /* Fill shared content. */
        mtc->shared.working_mode = args->working_mode;
        memcpy(mtc->shared.fields, args->fields,
                        MEMBER_SIZE(struct master_task_ctx, shared.fields));

        mtc->shared.path_str_len =
                (args->path_str == NULL) ? 0 : strlen(args->path_str);

        mtc->shared.filter_str_len =
                (args->filter_str == NULL) ? 0 : strlen(args->filter_str);

        mtc->shared.rec_limit = args->rec_limit;

        mtc->shared.time_begin = args->time_begin;
        mtc->shared.time_end = args->time_end;

        mtc->shared.use_fast_topn = args->use_fast_topn;


        /* Fill master specific content. */
        mtc->slave_cnt = world_size - 1; //all nodes without master
        mtc->output_params = args->output_params;
}


static error_code_t mem_write_callback(uint8_t *data, size_t data_len,
                void *user)
{
        (void)data_len;
        size_t off = 0;


        struct mem_write_callback_data *mwcd =
                (struct mem_write_callback_data *)user;

        for (size_t i = 0; i < mwcd->fields_cnt; ++i) {
                secondary_errno = lnf_rec_fset(mwcd->rec, mwcd->fields[i].id,
                                data + off);
                if (secondary_errno != LNF_OK) {
                        print_err(E_LNF, secondary_errno, "lnf_rec_fset()");
                        return E_LNF;
                }

                off += mwcd->fields[i].size;
        }

        secondary_errno = lnf_mem_write(mwcd->mem, mwcd->rec);
        if (secondary_errno != LNF_OK) {
                print_err(E_LNF, secondary_errno, "lnf_mem_write()");
                return E_LNF;
        }

        return E_OK;
}

static error_code_t mem_write_raw_callback(uint8_t *data, size_t data_len,
                void *user)
{
        secondary_errno = lnf_mem_write_raw((lnf_mem_t *)user, (char *)data,
                        data_len);
        if (secondary_errno != LNF_OK) {
                print_err(E_LNF, secondary_errno, "lnf_mem_write_raw()");
                return E_LNF;
        }

        return E_OK;
}

static error_code_t print_rec_callback(uint8_t *data, size_t data_len,
                void *user)
{
        (void)data_len;
        (void)user;

        print_rec(data);

        return E_OK;
}


/* Progress bar print. */
static void progress_bar_print(void)
{
        struct progress_bar_ctx *pbc = &progress_bar_ctx; //global variable
        double total_percentage;
        double slave_percentage[pbc->slave_cnt];


        /* Calculate percentage progress for each slave. */
        for (size_t i = 0; i < pbc->slave_cnt; ++i) {
                assert(pbc->files_slave_cur[i] <= pbc->files_slave_sum[i]);
                if (pbc->files_slave_sum[i] == 0) {
                        slave_percentage[i] = 100.0;
                } else {
                        slave_percentage[i] = (double)pbc->files_slave_cur[i] /
                                pbc->files_slave_sum[i] * 100.0;
                }
        }

        /* Calculate total percentage progress. */
        assert(pbc->files_cur <= pbc->files_sum);
        if (pbc->files_sum == 0) {
                total_percentage = 100.0;
        } else {
                total_percentage = (double)pbc->files_cur /
                        pbc->files_sum * 100.0;
        }

        /* Diverge for each progress bar type. */
        switch (pbc->type) {
        case PROGRESS_BAR_TOTAL:
                fprintf(pbc->out_stream, "[reading files] %zu/%zu (%.0f %%)",
                                pbc->files_cur, pbc->files_sum,
                                total_percentage);
                break;

        case PROGRESS_BAR_PERSLAVE:
                fprintf(pbc->out_stream, "[reading files] total: %zu/%zu "
                                "(%.0f %%)", pbc->files_cur, pbc->files_sum,
                                total_percentage);

                for (size_t i = 0; i < pbc->slave_cnt; ++i) {
                        fprintf(pbc->out_stream, " | %zu: %zu/%zu (%.0f %%)",
                                        i + 1, pbc->files_slave_cur[i],
                                        pbc->files_slave_sum[i],
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

        default:
                assert(!"unknown progress bar type");
                break;
        }

        /* Different behavior for streams and for files. */
        if (pbc->out_stream == stdout || pbc->out_stream == stderr) { //stream
                if (pbc->files_cur == pbc->files_sum) {
                        putc('\n', pbc->out_stream); //done, break line
                } else {
                        putc('\r', pbc->out_stream); //not done, return carriage
                }
        } else { //file
                putc('\n', pbc->out_stream); //proper text file termination
                rewind(pbc->out_stream);
        }

        fflush(pbc->out_stream);
}

/* Progress bar initialization: gather file count from each slave etc. */
static error_code_t progress_bar_init(progress_bar_type_t type, char *dest,
                size_t slave_cnt)
{
        struct progress_bar_ctx *pbc = &progress_bar_ctx; //global variable
        size_t zero = 0; //sendbuf for MPI_Gather
        size_t files_sum[slave_cnt + 1]; //recvbuf for MPI_Gather


        /* Allocate memory to keep context. */
        pbc->files_slave_cur = calloc(slave_cnt, sizeof (size_t));
        pbc->files_slave_sum = calloc(slave_cnt, sizeof (size_t));
        if (pbc->files_slave_cur == NULL || pbc->files_slave_sum == NULL) {
                secondary_errno = 0;
                print_err(E_MEM, secondary_errno, "malloc()");
                return E_MEM;
        }

        pbc->type = type; //store progress bar type
        pbc->slave_cnt = slave_cnt; //store slave count

        if (dest == NULL || strcmp(dest, "stderr") == 0) {
                pbc->out_stream = stderr; //default is stderr
        } else if (strcmp(dest, "stdout") == 0) {
                pbc->out_stream = stdout;
        } else { //destination is file
                pbc->out_stream = fopen(dest, "w");
                if (pbc->out_stream == NULL) {
                        print_warn(E_ARG, 0, "invalid progress bar destination "
                                        "\"%s\": %s", dest, strerror(errno));
                        pbc->type = PROGRESS_BAR_NONE; //disable progress bar
                }
        }

        /* Receive number of files to be processed. */
        MPI_Gather(&zero, 1, MPI_UNSIGNED_LONG, files_sum, 1, MPI_UNSIGNED_LONG,
                        ROOT_PROC, MPI_COMM_WORLD);

        for (size_t i = 0; i < slave_cnt; ++i) {
                pbc->files_slave_sum[i] = files_sum[i + 1];
                pbc->files_sum += pbc->files_slave_sum[i];
        }

        /* Initial progress bar print. */
        if (pbc->type != PROGRESS_BAR_NONE) {
                progress_bar_print();
        }


        return E_OK;
}

/* Progress bar refresh. */
static bool progress_bar_refresh(int source)
{
        struct progress_bar_ctx *pbc = &progress_bar_ctx; //global variable

        pbc->files_slave_cur[source - 1]++;
        pbc->files_cur++;

        if (pbc->type != PROGRESS_BAR_NONE) {
                progress_bar_print();
        }

        return pbc->files_cur == pbc->files_sum;
}

/* Progress bar cleanup. */
static void progress_bar_finish(void)
{
        struct progress_bar_ctx *pbc = &progress_bar_ctx; //global variable


        free(pbc->files_slave_cur);
        free(pbc->files_slave_sum);

        /* Close output stream only if it's file. */
        if (pbc->out_stream != stdout && pbc->out_stream != stderr &&
                        pbc->out_stream != NULL &&
                        fclose(pbc->out_stream) == EOF) {
                print_warn(E_INTERNAL, 0, "progress bar: %s", strerror(errno));
        }
}


static error_code_t fast_topn_bcast_all(lnf_mem_t *mem)
{
        int rec_len = 0;
        uint8_t rec_buff[LNF_MAX_RAW_LEN]; //TODO: send mutliple records
        lnf_mem_cursor_t *read_cursor;

        //TODO: handle error, if no records received
        secondary_errno = lnf_mem_first_c(mem, &read_cursor);
        if (secondary_errno == LNF_EOF) {
                goto send_terminator; //no records in memory, no problem
        } else if (secondary_errno != LNF_OK) {
                print_err(E_LNF, secondary_errno, "lnf_mem_first_c()");
                return E_LNF;
        }

        /* Broadcast all records. */
        while (true) {
                secondary_errno = lnf_mem_read_raw_c(mem, read_cursor,
                                (char *)rec_buff, &rec_len, LNF_MAX_RAW_LEN);
                assert(secondary_errno != LNF_EOF);
                if (secondary_errno != LNF_OK) {
                        print_err(E_LNF, secondary_errno,
                                        "lnf_mem_read_raw_c()");
                        return E_LNF;
                }

                MPI_Bcast(&rec_len, 1, MPI_INT, ROOT_PROC, MPI_COMM_WORLD);
                MPI_Bcast(rec_buff, rec_len, MPI_BYTE, ROOT_PROC,
                                MPI_COMM_WORLD);

                secondary_errno = lnf_mem_next_c(mem, &read_cursor);
                if (secondary_errno == LNF_EOF) {
                        break; //all records successfully sent
                } else if (secondary_errno != LNF_OK) {
                        print_err(E_LNF, secondary_errno, "lnf_mem_next_c()");
                        return E_LNF;
                }
        }

send_terminator:
        /* Phase 2 done, notify slaves by zero record length. */
        rec_len = 0;
        MPI_Bcast(&rec_len, 1, MPI_INT, ROOT_PROC, MPI_COMM_WORLD);

        return E_OK;
}


static error_code_t recv_loop(size_t slave_cnt, size_t rec_limit,
                recv_callback_t recv_callback, void *user)
{
        error_code_t primary_errno = E_OK;
        int rec_len = 0;
        uint8_t rec_buff[LNF_MAX_RAW_LEN]; //TODO: receive mutliple records
        MPI_Status status;
        size_t rec_cntr = 0;
        size_t active_slaves = slave_cnt; //every slave is active
        bool limit_exceeded = false;

        /* Data receiving loop. */
        while (active_slaves) {
                /* Receive message from any slave. */
                MPI_Recv(rec_buff, LNF_MAX_RAW_LEN, MPI_BYTE, MPI_ANY_SOURCE,
                                TAG_DATA, MPI_COMM_WORLD, &status);

                /* Determine actual size of the received message. */
                MPI_Get_count(&status, MPI_BYTE, &rec_len);
                if (rec_len == 0) {
                        active_slaves--;
                        continue; //empty message -> slave finished
                }

                if (limit_exceeded) {
                        continue; //do not process but continue receiving
                }

                /* Call callback function for each received record. */
                primary_errno = recv_callback(rec_buff, rec_len, user);
                if (primary_errno != E_OK) {
                        break; //don't receive next message
                }

                if (++rec_cntr == rec_limit) {
                        limit_exceeded = true;
                }
        }

        return primary_errno;
}

static error_code_t irecv_loop(size_t slave_cnt, size_t rec_limit,
                recv_callback_t recv_callback, void *user)
{
        error_code_t primary_errno = E_OK;

        uint8_t *buff_mem; //big chunk of memory for all data buffers
        uint8_t *buff[slave_cnt][2]; //pointers to the buff_mem
        bool buff_idx[slave_cnt]; //indexes to the currently used data buffers

        MPI_Request requests[slave_cnt + 1]; //plus one for progress
        MPI_Status status;

        size_t rec_cntr = 0; //processed records
        size_t byte_cntr = 0; //received bytes
        bool limit_exceeded = false;


        /* Allocate two receive buffers for each slave as a continuous memory.*/
        buff_mem = malloc(2 * XCHG_BUFF_SIZE * slave_cnt * sizeof (*buff_mem));
        if (buff_mem == NULL) {
                secondary_errno = 0;
                print_err(E_MEM, secondary_errno, "malloc()");
                return E_MEM;
        }

        /*
         * There are two buffers for each slave. The first one is passed to
         * nonblocking MPI receive function, the second one is processed at the
         * same time. After both these operations are completed, buffers are
         * switched. Buffer switching (toggling) is independent for each slave,
         * that's why array buff_idx[slave_cnt] is needed.
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
        for (size_t i = 0; i < slave_cnt; ++i) {
                requests[i] = MPI_REQUEST_NULL;

                buff[i][0] = buff_mem + (i * 2 * XCHG_BUFF_SIZE);
                buff[i][1] = buff[i][0] + XCHG_BUFF_SIZE;
        }
        memset(buff_idx, 0, slave_cnt * sizeof (buff_idx[0]));

        /* Start first individual nonblocking data receive from every slave. */
        for (size_t i = 0; i < slave_cnt; ++i) {
                uint8_t *free_buff = buff[i][buff_idx[i]]; //shortcut

                MPI_Irecv(free_buff, XCHG_BUFF_SIZE, MPI_BYTE, i + 1, TAG_DATA,
                                MPI_COMM_WORLD, &requests[i]);
        }

        /* Start first nonblocking progress report receive from any slave. */
        if (progress_bar_ctx.files_sum > 0) {
                MPI_Irecv(NULL, 0, MPI_BYTE, MPI_ANY_SOURCE, TAG_PROGRESS,
                                MPI_COMM_WORLD, &requests[slave_cnt]);
        } else {
                requests[slave_cnt] = MPI_REQUEST_NULL;
        }

        /* Data receiving loop. */
        while (true) {
                int msg_size;
                int slave_idx;
                uint8_t *rec_ptr; //pointer to record in data buffer
                uint8_t *msg_end; //record boundary

                /* Wait for data or status report from any slave. */
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
                MPI_Get_count(&status, MPI_BYTE, &msg_size);
                if (msg_size == 0) {
                        continue; //empty message -> slave finished
                }
                byte_cntr += msg_size;

                rec_ptr = buff[slave_idx][buff_idx[slave_idx]]; //first record
                msg_end = rec_ptr + msg_size; //end of the last record
                buff_idx[slave_idx] = !buff_idx[slave_idx]; //toggle buffers

                /* Start receiving next message into free buffer. */
                MPI_Irecv(buff[slave_idx][buff_idx[slave_idx]], XCHG_BUFF_SIZE,
                                MPI_BYTE, status.MPI_SOURCE, TAG_DATA,
                                MPI_COMM_WORLD, &requests[slave_idx]);

                if (limit_exceeded) {
                        continue; //do not process but continue receiving
                }

                /*
                 * Call callback function for each record in received message.
                 * Each record is prefixed with 4 bytes long record size.
                 */
                while (rec_ptr < msg_end) {
                        uint32_t rec_size = *(uint32_t *)(rec_ptr);

                        rec_ptr += sizeof (rec_size); //shift to record data

                        primary_errno = recv_callback(rec_ptr, rec_size, user);
                        if (primary_errno != E_OK) {
                                goto free_db_mem;
                        }

                        rec_ptr += rec_size; //shift to next record

                        if (++rec_cntr == rec_limit) {
                                limit_exceeded = true;
                                break;
                        }
                }
        }

free_db_mem:
        free(buff_mem);

        print_debug("<irecv_loop> processed %zu records, received %zu B",
                        rec_cntr, byte_cntr);
        return primary_errno;
}


static void progress_bar_loop(void)
{
        MPI_Status status;


        for (size_t i = 0; i < progress_bar_ctx.files_sum; ++i) {
                MPI_Recv(NULL, 0, MPI_BYTE, MPI_ANY_SOURCE, TAG_PROGRESS,
                                MPI_COMM_WORLD, &status);
                progress_bar_refresh(status.MPI_SOURCE);
        }
}


static error_code_t mode_list_main(const struct master_task_ctx *mtc)
{
        error_code_t primary_errno;


        primary_errno = irecv_loop(mtc->slave_cnt, mtc->shared.rec_limit,
                        print_rec_callback, NULL);


        return primary_errno;
}


static error_code_t mode_sort_main(const struct master_task_ctx *mtc)
{
        error_code_t primary_errno = E_OK;
        struct mem_write_callback_data mwcd = {0};


        /* Fill fields array. */
        for (size_t i = 0; i < LNF_FLD_TERM_; ++i) {
                if (mtc->shared.fields[i].id == 0) {
                        continue; //field is not present
                }
                mwcd.fields[mwcd.fields_cnt].id = i;
                mwcd.fields[mwcd.fields_cnt].size = field_get_size(i);
                mwcd.fields_cnt++;
        }


        /* Initialize aggregation memory and set memory parameters. */
        primary_errno = init_aggr_mem(&mwcd.mem, mtc->shared.fields);
        if (primary_errno != E_OK) {
                return primary_errno;
        }
        /* Switch memory to linked list - disable aggregation. */
        lnf_mem_setopt(mwcd.mem, LNF_OPT_LISTMODE, NULL, 0);


        /* Initialize empty LNF record for writing. */
        secondary_errno = lnf_rec_init(&mwcd.rec);
        if (secondary_errno != LNF_OK) {
                primary_errno = E_LNF;
                print_err(primary_errno, secondary_errno, "lnf_rec_init()");
                goto free_aggr_mem;
        }


        /* Fill memory with records. */
        if (mtc->shared.rec_limit == 0) { //slow ordering, xchg all records
                primary_errno = irecv_loop(mtc->slave_cnt, 0,
                                mem_write_callback, &mwcd);
                if (primary_errno != E_OK) {
                        goto free_lnf_rec;
                }
        } else { //fast ordering, minimum of records exchanged
                primary_errno = irecv_loop(mtc->slave_cnt, 0,
                                mem_write_raw_callback, mwcd.mem);
                if (primary_errno != E_OK) {
                        goto free_lnf_rec;
                }
        }

        /* Print all records in memory. */
        primary_errno = print_mem(mwcd.mem,mtc->shared.rec_limit);

free_lnf_rec:
        lnf_rec_free(mwcd.rec);
free_aggr_mem:
        free_aggr_mem(mwcd.mem);


        return primary_errno;
}


static error_code_t mode_aggr_main(const struct master_task_ctx *mtc)
{
        error_code_t primary_errno = E_OK;
        lnf_mem_t *aggr_mem;


        /* Initialize aggregation memory and set memory parameters. */
        primary_errno = init_aggr_mem(&aggr_mem, mtc->shared.fields);
        if (primary_errno != E_OK) {
                return primary_errno;
        }

        primary_errno = irecv_loop(mtc->slave_cnt, 0, mem_write_raw_callback,
                        aggr_mem);
        if (primary_errno != E_OK) {
                goto free_aggr_mem;
        }

        if (mtc->shared.use_fast_topn) {
                //TODO: if file doesn't exist on slave, this will cause deadlock
                primary_errno = fast_topn_bcast_all(aggr_mem);
                if (primary_errno != E_OK) {
                        goto free_aggr_mem;
                }

                /* Reset memory - all records will be received again. */
                //TODO: optimalization - add records to memory, don't reset
                free_aggr_mem(aggr_mem);
                primary_errno = init_aggr_mem(&aggr_mem, mtc->shared.fields);
                if (primary_errno != E_OK) {
                        return primary_errno;
                }

                primary_errno = recv_loop(mtc->slave_cnt, 0,
                                mem_write_raw_callback, aggr_mem);
                if (primary_errno != E_OK) {
                        goto free_aggr_mem;
                }
        }

        /* Print all records in memory. */
        primary_errno = print_mem(aggr_mem, mtc->shared.rec_limit);

free_aggr_mem:
        free_aggr_mem(aggr_mem);


        return primary_errno;
}


error_code_t master(int world_size, const struct cmdline_args *args)
{
        error_code_t primary_errno = E_OK;
        struct master_task_ctx mtc; //master task context
        struct processed_summ processed_summ = {0}; //processed data statistics
        struct metadata_summ metadata_summ = {0}; //metadata statistics
        double duration = -MPI_Wtime(); //start time measurement


        memset(&mtc, 0, sizeof (mtc));

        /* Construct master_task_ctx struct from command-line arguments. */
        construct_master_task_ctx(&mtc, args, world_size);
        output_setup(mtc.output_params, mtc.shared.fields);


        /* Broadcast task context, path string and optional filter string. */
        MPI_Bcast(&mtc.shared, 1, mpi_struct_shared_task_ctx, ROOT_PROC,
                        MPI_COMM_WORLD);

        MPI_Bcast(args->path_str, mtc.shared.path_str_len, MPI_CHAR, ROOT_PROC,
                        MPI_COMM_WORLD);

        if (mtc.shared.filter_str_len > 0) {
                MPI_Bcast(args->filter_str, mtc.shared.filter_str_len,
                                MPI_CHAR, ROOT_PROC, MPI_COMM_WORLD);
        }


        primary_errno = progress_bar_init(args->progress_bar_type,
                        args->progress_bar_dest, mtc.slave_cnt);
        if (primary_errno != E_OK) {
                goto finalize;
        }


        /* Send, receive, process. */
        switch (mtc.shared.working_mode) {
        case MODE_LIST:
                primary_errno = mode_list_main(&mtc);
                break;

        case MODE_SORT:
                primary_errno = mode_sort_main(&mtc);
                break;

        case MODE_AGGR:
                primary_errno = mode_aggr_main(&mtc);
                break;

        case MODE_META:
                /* Receive only the progress. */
                progress_bar_loop();
                break;

        default:
                assert(!"unknown working mode");
        }


        progress_bar_finish();

        /* Reduce statistics from each slave. */
        MPI_Reduce(MPI_IN_PLACE, &processed_summ, 3, MPI_UINT64_T, MPI_SUM,
                        ROOT_PROC, MPI_COMM_WORLD);
        MPI_Reduce(MPI_IN_PLACE, &metadata_summ, 15, MPI_UINT64_T, MPI_SUM,
                        ROOT_PROC, MPI_COMM_WORLD);

        duration += MPI_Wtime(); //end time measurement

        //TODO: when using list mode and record limit, processed records summary
        //      doesn't match with actualy printed records
        print_processed_summ(&processed_summ, duration);
        print_metadata_summ(&metadata_summ);


finalize:
        return primary_errno;
}
