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

#include "master.h"
#include "common.h"
#include "output.h"

#include <string.h> //strlen()
#include <stdbool.h>
#include <assert.h>

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


typedef error_code_t(*recv_callback_t)(uint8_t *data, size_t data_len,
                void *user);


static void construct_master_task_ctx(struct master_task_ctx *mtc,
                const struct cmdline_args *args, int world_size)
{
        /* Fill shared content. */
        mtc->shared.working_mode = args->working_mode;
        memcpy(mtc->shared.fields, args->fields,
                        MEMBER_SIZE(struct master_task_ctx, shared.fields));

        mtc->shared.filter_str_len =
                (args->filter_str == NULL) ? 0 : strlen(args->filter_str);
        mtc->shared.path_str_len =
                (args->path_str == NULL) ? 0 : strlen(args->path_str);

        mtc->shared.rec_limit = args->rec_limit;

        mtc->shared.interval_begin = args->interval_begin;
        mtc->shared.interval_end = args->interval_end;

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

        uint8_t *db_mem; //big chunk of memory
        uint8_t *db[slave_cnt][2]; //pointers to db_mem
        bool db_idx[slave_cnt]; //index to currently used data_buff

        MPI_Request requests[slave_cnt];
        MPI_Status status;

        size_t rec_cntr = 0; //processed records
        size_t byte_cntr = 0; //received bytes
        bool limit_exceeded = false;


        /* Allocate two receive buffers for each slave as continuous memory. */
        db_mem = malloc(2 * XCHG_BUFF_SIZE * slave_cnt * sizeof (*db_mem));
        if (db_mem == NULL) {
                secondary_errno = 0;
                print_err(E_MEM, secondary_errno, "malloc()");
                return E_MEM;
        }

        /*
         * There are two buffers for each slave. The first one is passed to
         * nonblocking MPI receive function, the second one is processed at the
         * same time. After both these operations are completed, buffers are
         * switched. Buffer switching (toggling) is independent for each slave,
         * that's why array db_idx[slave_cnt] is needed.
         * db_mem is partitioned in db in the following manner:
         *
         * <--------- XCHG_BUFF_SIZE -------> <-------- XCHG_BUFF_SIZE -------->
         * ---------------------------------------------------------------------
         * |            db[0][0]             |             db[0][1]            |
         * ---------------------------------------------------------------------
         * |            db[1][0]             |             db[1][1]            |
         * ---------------------------------------------------------------------
         * .                                                                   .
         * .                                                                   .
         * .                                                                   .
         * ---------------------------------------------------------------------
         * |      db[slave_cnt - 1][0]       |      db[slave_cnt - 1][1]       |
         * ---------------------------------------------------------------------
         */
        for (size_t i = 0; i < slave_cnt; ++i) {
                requests[i] = MPI_REQUEST_NULL;

                db[i][0] = db_mem + (i * 2 * XCHG_BUFF_SIZE);
                db[i][1] = db[i][0] + XCHG_BUFF_SIZE;
        }
        memset(db_idx, 0, slave_cnt * sizeof (db_idx[0]));

        /* Start first individual nonblocking data receive from every slave. */
        for (size_t i = 0; i < slave_cnt; ++i) {
                uint8_t *free_buff = db[i][db_idx[i]]; //shortcut

                MPI_Irecv(free_buff, XCHG_BUFF_SIZE, MPI_BYTE, i + 1, TAG_DATA,
                                MPI_COMM_WORLD, &requests[i]);
        }

        /* Data receiving loop. */
        while (true) {
                int msg_size;
                int slave_idx;
                uint8_t *rec_ptr; //pointer to record in data buffer
                uint8_t *msg_end; //record boundary

                /* Wait for message from any slave. */
                MPI_Waitany(slave_cnt, requests, &slave_idx, &status);
                if (slave_idx == MPI_UNDEFINED) { //no active slaves anymore
                        break;
                }

                /* Determine actual size of received message. */
                MPI_Get_count(&status, MPI_BYTE, &msg_size);
                if (msg_size == 0) {
                        continue; //empty message -> slave finished
                }
                byte_cntr += msg_size;

                rec_ptr = db[slave_idx][db_idx[slave_idx]]; //first record
                msg_end = rec_ptr + msg_size; //end of the last record
                db_idx[slave_idx] = !db_idx[slave_idx]; //toggle buffers

                /* Start receiving next message into free buffer. */
                MPI_Irecv(db[slave_idx][db_idx[slave_idx]], XCHG_BUFF_SIZE,
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
        free(db_mem);

        print_debug("<irecv_loop> processed %zu records, received %zu bytes",
                        rec_cntr, byte_cntr);
        return primary_errno;
}


static void stats_recv(struct stats *s, size_t slave_cnt)
{
        struct stats received;

        /* Wait for statistics from every slave. */
        for (size_t i = 0; i < slave_cnt; ++i) {
                MPI_Recv(&received, 3, MPI_UINT64_T, MPI_ANY_SOURCE, TAG_STATS,
                                MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                s->flows += received.flows;
                s->pkts += received.pkts;
                s->bytes += received.bytes;
        }
}


static error_code_t mode_list_main(const struct master_task_ctx *mtc)
{
        error_code_t primary_errno;
        struct stats stats = {0};

        primary_errno = irecv_loop(mtc->slave_cnt, mtc->shared.rec_limit,
                        print_rec_callback, NULL);

        /* Receive statistics from every slave, print them. */
        //TODO: stats with record limit are incorrect
        stats_recv(&stats, mtc->slave_cnt);
        print_stats(&stats);

        return primary_errno;
}


static error_code_t mode_sort_main(const struct master_task_ctx *mtc)
{
        error_code_t primary_errno = E_OK;
        struct mem_write_callback_data mwcd = {0};
        struct stats stats = {0};


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
                primary_errno = recv_loop(mtc->slave_cnt, 0,
                                mem_write_raw_callback, mwcd.mem);
                if (primary_errno != E_OK) {
                        goto free_lnf_rec;
                }
        }

        /* Receive statistics from every slave, print them. */
        stats_recv(&stats, mtc->slave_cnt);

        /* Print all records in memory. */
        primary_errno = print_mem(mwcd.mem,mtc->shared.rec_limit);
        print_stats(&stats);

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
        struct stats stats = {0};

        /* Initialize aggregation memory and set memory parameters. */
        primary_errno = init_aggr_mem(&aggr_mem, mtc->shared.fields);
        if (primary_errno != E_OK) {
                return primary_errno;
        }

        primary_errno = recv_loop(mtc->slave_cnt, 0, mem_write_raw_callback,
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

        /* Receive statistics from every slave, print them. */
        stats_recv(&stats, mtc->slave_cnt);

        /* Print all records in memory. */
        primary_errno = print_mem(aggr_mem, mtc->shared.rec_limit);
        print_stats(&stats);

free_aggr_mem:
        free_aggr_mem(aggr_mem);

        return primary_errno;
}


void progress_bar(progress_bar_t type, size_t slave_cnt)
{
        size_t files_slave_cur[slave_cnt];
        size_t files_slave_sum[slave_cnt];
        size_t files_all_sum = 0;

        MPI_Status status;


        //TODO: MPI methods are not thread safe
        /* Receive number of files to be processed. */
        MPI_Gather(MPI_IN_PLACE, 0, NULL, files_slave_sum - 1, 1,
                        MPI_UNSIGNED_LONG, ROOT_PROC, MPI_COMM_WORLD);
        for (size_t i = 0; i < slave_cnt; ++i) {
                files_all_sum += files_slave_sum[i];
                files_slave_cur[i] = 0;
        }


        if (type != PROGRESS_BAR_NONE) {
                print_progress_bar(files_slave_cur, files_slave_sum, slave_cnt,
                                type);
        }

        for (size_t i = 0; i < files_all_sum; ++i) {
                /* Receive processed file report. */
                MPI_Recv(NULL, 0, MPI_BYTE, MPI_ANY_SOURCE, TAG_PROGRESS,
                                MPI_COMM_WORLD, &status);
                files_slave_cur[status.MPI_SOURCE - 1]++;

                if (type == PROGRESS_BAR_NONE) {
                        break;
                }

                print_progress_bar(files_slave_cur, files_slave_sum, slave_cnt,
                                type);
        }
}


error_code_t master(int world_size, const struct cmdline_args *args)
{
        error_code_t primary_errno = E_OK;
        struct master_task_ctx mtc;

        memset(&mtc, 0, sizeof (mtc));

        /* Construct master_task_ctx struct from command-line arguments. */
        construct_master_task_ctx(&mtc, args, world_size);
        output_setup(mtc.output_params, mtc.shared.fields);


        /* Broadcast task context, optional filter string and path string. */
        MPI_Bcast(&mtc.shared, 1, mpi_struct_shared_task_ctx, ROOT_PROC,
                        MPI_COMM_WORLD);

        if (mtc.shared.filter_str_len > 0) {
                MPI_Bcast(args->filter_str, mtc.shared.filter_str_len,
                                MPI_CHAR, ROOT_PROC, MPI_COMM_WORLD);
        }
        if (mtc.shared.path_str_len > 0) {
                MPI_Bcast(args->path_str, mtc.shared.path_str_len, MPI_CHAR,
                                ROOT_PROC, MPI_COMM_WORLD);
        }


        #pragma omp parallel sections num_threads(2)
        {
                #pragma omp section
                {
                        if (mtc.shared.working_mode != MODE_PASS) {
                                progress_bar(args->progress_bar, mtc.slave_cnt);
                        }
                }

                #pragma omp section
                {
                        /* Send, receive, process. */
                        switch (mtc.shared.working_mode) {
                                case MODE_PASS:
                                        //only termination message will be
                                        //received from each slave
                                case MODE_LIST:
                                        primary_errno = mode_list_main(&mtc);
                                        break;

                                case MODE_SORT:
                                        primary_errno = mode_sort_main(&mtc);
                                        break;

                                case MODE_AGGR:
                                        primary_errno = mode_aggr_main(&mtc);
                                        break;

                                default:
                                        assert(!"unknown working mode");
                        }
                }
        }

        return primary_errno;
}
