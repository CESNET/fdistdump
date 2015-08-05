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
#include "arg_parse.h"
#include "comm/communication.h"

#include <stdbool.h>
#include <assert.h>

#include <libnf.h>


struct mem_insert_callback_data {
        lnf_mem_t *mem;
        lnf_rec_t *rec;
};


static int fast_topn_bcast_all(lnf_mem_t *mem)
{
        int ret, rec_len;
        char rec_buff[LNF_MAX_RAW_LEN]; //TODO: send mutliple records
        lnf_mem_cursor_t *read_cursor;

        ret = lnf_mem_first_c(mem, &read_cursor);
        if (ret != LNF_OK) {
                print_err("LNF - lnf_mem_first_c()");
                return E_LNF;
        }
    //        int x = 0;
        /* Broadcast all records. */
        while (true) {
                ret = lnf_mem_read_raw_c(mem, read_cursor, rec_buff, &rec_len,
                                LNF_MAX_RAW_LEN);
                assert(ret != LNF_EOF);
                if (ret != LNF_OK) {
                        print_err("LNF - lnf_mem_read_raw_c()");
                        return E_LNF;
                }
                ret = comm_bcast_bytes(rec_len, rec_buff);
                if (ret != E_OK) {
                        print_err("Sending error");
                        return ret;
                }

                ret = lnf_mem_next_c(mem, &read_cursor);
                if (ret == LNF_EOF) {
                        break; //all records sent
                } else if (ret != LNF_OK) {
                        print_err("LNF - lnf_mem_next_c()");
                        return E_LNF;
                }
        }

        /* Phase 2 done, notify slaves by zero record length. */
        ret = comm_bcast_zero_msg();
        if (ret != E_OK) {
                print_err("Sending error");
                return ret;
        }

        return E_OK;
}

static int mem_write_callback(char *data, size_t data_len, void *user)
{
        (void)data_len;
        int ret;
        struct mem_insert_callback_data *micd =
                (struct mem_insert_callback_data *)user;

        ret = lnf_rec_fset(micd->rec, LNF_FLD_BREC1, data);
        if (ret != LNF_OK) {
                print_err("LNF - lnf_rec_fset()");
                return E_LNF;
        }
        ret = lnf_mem_write(micd->mem, micd->rec);
        if (ret != LNF_OK) {
                print_err("LNF - lnf_mem_write()");
                return E_LNF;
        }

        return E_OK;
}

static int mem_write_raw_callback(char *data, size_t data_len, void *user)
{
        int ret;

        ret = lnf_mem_write_raw((lnf_mem_t *)user, data, data_len);
        if (ret != LNF_OK) {
                print_err("LNF - lnf_mem_write_raw() %X", ret);
                return E_LNF;
        }

        return E_OK;
}

static int print_brec_callback(char *data, size_t data_len, void *user)
{
        (void)data_len;
        (void)user;

        return print_brec((const lnf_brec1_t *)data);
}

static int mode_rec_main(master_context_t *m_ctx, task_setup_t *t_setup,
                         size_t slave_cnt)
{
        recv_callback_t print_rec_cb = print_brec_callback;
        return comm_irecv_loop(m_ctx, slave_cnt, t_setup->s.rec_limit,
                               print_rec_cb, NULL);
}

static int mode_ord_main(master_context_t *m_ctx, task_setup_t *t_setup,
                         size_t slave_cnt)
{
        int ret, err = E_OK;
        struct mem_insert_callback_data callback_data = {0};

        /* Initialize aggregation memory. */
        ret = lnf_mem_init(&callback_data.mem);
        if (ret != LNF_OK) {
                print_err("LNF - lnf_mem_init()");
                callback_data.mem = NULL;
                err = E_LNF;
                goto cleanup;
        }
        /* Initialize empty LNF record for writing. */
        ret = lnf_rec_init(&callback_data.rec);
        if (ret != LNF_OK) {
                print_err("LNF - lnf_rec_init()");
                callback_data.rec = NULL;
                err = E_LNF;
                goto cleanup;
        }

        /* Switch memory to linked list (better for sorting). */
        ret = lnf_mem_setopt(callback_data.mem, LNF_OPT_LISTMODE, NULL, 0);
        if (ret != LNF_OK) {
                print_err("LNF - lnf_mem_setopt()");
                err = E_LNF;
                goto cleanup;
        }

        /* Set memory parameters. */
        ret = mem_setup(callback_data.mem, t_setup->s.agg_params,
                        t_setup->s.agg_params_cnt);
        if (ret != E_OK) {
                err = E_LNF;
                goto cleanup;
        }

        /* Fill memory with records. */
        if (t_setup->s.rec_limit != 0) { //fast ordering, minimum of records exchanged
                printf("Fast ordering mode.\n");
                recv_callback_t mem_write_raw_cb = mem_write_raw_callback;
                ret = comm_recv_loop(m_ctx, slave_cnt, 0, mem_write_raw_cb,
                                callback_data.mem);
                if (ret != E_OK) {
                        err = ret;
                        goto cleanup;
                }
        } else { //slow ordering, all records exchanged
                printf("Slow ordering mode.\n");
                recv_callback_t mem_write_cb = mem_write_callback;
                ret = comm_irecv_loop(m_ctx, slave_cnt, 0, mem_write_cb,
                                &callback_data);
                if (ret != E_OK) {
                        err = ret;
                        goto cleanup;
                }
        }

        /* Print all records in memory. */
        ret = mem_print(callback_data.mem, t_setup->s.rec_limit);
        if (ret != E_OK) {
                return ret;
        }

cleanup:
        if (callback_data.rec != NULL) {
                lnf_rec_free(callback_data.rec);
        }
        if (callback_data.mem != NULL) {
                lnf_mem_free(callback_data.mem);
        }

        return err;
}


static int mode_agg_main(master_context_t *m_ctx, task_setup_t *t_setup,
                         size_t slave_cnt)
{
        int ret, err = E_OK;
        lnf_mem_t *mem;
        recv_callback_t mem_write_raw_cb = mem_write_raw_callback;

        /* Initialize aggregation memory. */
        ret = lnf_mem_init(&mem);
        if (ret != LNF_OK) {
                print_err("LNF - lnf_mem_init()");
                mem = NULL;
                err = E_LNF;
                goto cleanup;
        }
        ret = mem_setup(mem, t_setup->s.agg_params, t_setup->s.agg_params_cnt);
        if (ret != E_OK) {
                err = E_LNF;
                goto cleanup;
        }

        ret = comm_recv_loop(m_ctx, slave_cnt, 0, mem_write_raw_cb, mem);
        if (ret != E_OK) {
                err = ret;
                goto cleanup;
        }

        if (t_setup->s.use_fast_topn) {
                ret = fast_topn_bcast_all(mem);
                if (ret != E_OK) {
                        err = ret;
                        goto cleanup;
                }
                /* Reset memory - all records will be received again. */
                //TODO: optimalization - add records to memory, don't reset
                lnf_mem_free(mem);
                ret = lnf_mem_init(&mem);
                if (ret != LNF_OK) {
                        print_err("LNF - lnf_mem_init()");
                        mem = NULL;
                        err = E_LNF;
                        goto cleanup;
                }

                ret = mem_setup(mem, t_setup->s.agg_params,
                                t_setup->s.agg_params_cnt);
                if (ret != E_OK) {
                        err = ret;
                        goto cleanup;
                }
                ret = comm_recv_loop(m_ctx, slave_cnt, 0, mem_write_raw_cb,
                                     mem);
                if (ret != E_OK) {
                        err = ret;
                        goto cleanup;
                }
        }

        ret = mem_print(mem, t_setup->s.rec_limit);
        if (ret != E_OK) {
                err = ret;
                goto cleanup;
        }

cleanup:
        if (mem != NULL) {
                lnf_mem_free(mem);
        }

        return err;
}



int master(int argc, char **argv, global_context_t *g_ctx)
{
        int ret, err = E_OK;

        task_setup_t t_setup = {{0}};
        clear_task_setup (&t_setup.s);

        master_params_t *master_params;
        master_context_t *m_ctx;

        ret = comm_create_master_params(&master_params);
        if (ret != E_OK){
                err = ret;
                ///TODO MPI_Abort(MPI_COMM_WORLD, EXIT_SUCCESS); i u E_HELP???
                goto master_done;
        }

        ret = arg_parse(argc, argv, &t_setup, master_params);
        if (ret != E_OK){
                err = ret;
                ///TODO MPI_Abort(MPI_COMM_WORLD, EXIT_SUCCESS); i u E_HELP???
                goto master_done;
        }
        t_setup.s.slave_cnt = g_ctx->slave_cnt;

        /* Create master specific communication context */
        ret = comm_init_master_ctx (&m_ctx, master_params, g_ctx->slave_cnt);
        if (ret != E_OK){
                err = ret;
                goto master_done;
        }

        /* Broadcast task setup to all slave nodes, filter string (if set) and
           path string (if set).*/
        ret = comm_bcast_task_setup(&t_setup);
        if (ret != E_OK){
                    err = ret;
                goto master_done;
        }

        /* Send, receive, process. */
        switch (t_setup.s.working_mode) {
        case MODE_REC:
                ret = mode_rec_main(m_ctx, &t_setup, g_ctx->slave_cnt);
                break;
        case MODE_ORD:
                ret = mode_ord_main(m_ctx, &t_setup, g_ctx->slave_cnt);
                break;
        case MODE_AGG:
                ret = mode_agg_main(m_ctx, &t_setup, g_ctx->slave_cnt);
                break;
        default:
                assert(!"unknown working mode");
                break;
        }

master_done:
        comm_destroy_master_ctx(&m_ctx, master_params, g_ctx->slave_cnt);
        comm_free_master_params(master_params);

        if (err != E_OK) {
                printf("MASTER: returning with error\n");
        }

        return err;
}
