/**
 * \file main.c
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
#include "slave.h"
#include "comm/communication.h"

#include <stdio.h>
#include <stdlib.h>

#include <libnf.h>

extern inline void comm_interface_init (void);

int main (int argc, char **argv)
{
        //WATCH OUT: has to be called before any other communication function
        comm_interface_init();

        int ret;
        global_context_t global_ctx;

        //Global initialization, need to be done by both, master and slave
        ret = comm_init_global(argc, argv, &global_ctx);
        if (ret != E_OK) {
                comm_fin_global( &global_ctx);
                print_err("error in global initialization");
                return EXIT_FAILURE;
        }

        #ifdef FDD_SPLIT_BINARY_MASTER
        ret = master(argc, argv, &global_ctx);
        #elifdef FDD_SPLIT_BINARY_SLAVE
        ret = slave(argc, argv, &global_ctx);
        #else
        /* Split master and slave code. */
        if (global_ctx.side == FDD_MASTER) {
                ret = master(argc, argv, &global_ctx);
        } else {
                ret = slave(argc, argv, &global_ctx);
        }
        #endif //FDD_SPLIT_BINARY_MASTER
        if (ret != E_OK) {
                comm_fin_global(&global_ctx);
                return EXIT_FAILURE;
        }

        //Global finalization, need to be done by both, master and slave
        ret = comm_fin_global(&global_ctx);
        if (ret != E_OK) {
                print_err("error in global finalization");
                return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
}
