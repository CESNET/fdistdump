/**
 * \file arg_parse.h
 * \brief
 * \author Jan Wrona, <wrona@cesnet.cz>
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

#ifndef ARG_PARSE_H
#define ARG_PARSE_H

#include "common.h"
#include "comm/communication.h"


#define AGG_SEPARATOR "," //srcport,srcip
#define STAT_SEPARATOR "/" //statistic/order
#define INTERVAL_SEPARATOR "#" //begin#end

#define DEFAULT_STAT_ORD "flows"
#define DEFAULT_STAT_LIMIT 10


/** \brief Parse command line arguments and fill task setup and master params.
 *
 * If all arguments are successfully parsed and stored, E_OK is returned.
 * If help was required, help string is printed and E_HELP is returned.
 * On error (invalid options or arguments, ...), error string is printed and
 * E_ARG is returned.
 *
 * \param[in] argc Argument count.
 * \param[in] argv Command line argument strings.
  * \param[out] t_setup Structure with task setup filled according to program
               arguments.
 * \param[out] m_par Structure for master specific parameters (depends on used
                     communication implementation).
 * \return Error code. E_OK, E_HELP or E_ARG.
 */
int arg_parse(int argc, char **argv, task_setup_t *t_setup,
              master_params_t *m_par);

#ifdef FDD_SPLIT_BINARY_SLAVE
/** \brief Parse command line arguments for slave part and fill slave params.
 *
 * If all arguments are successfully parsed and stored, E_OK is returned.
 * If help was required, help string is printed and E_HELP is returned.
 * On error (invalid options or arguments, ...), error string is printed and
 * E_ARG is returned.
 *
 * \param[in] argc Argument count.
 * \param[in] argv Command line argument strings.
  * \param[out] t_setup Structure with task setup filled according to program
               arguments.
 * \param[out] s_par Structure for slave specific parameters (depends on used
                     communication implementation).
 * \return Error code. E_OK, E_HELP or E_ARG.
 */
int arg_parse_slave(int argc, char **argv, slave_params_t *s_par);
#endif // FDD_SPLIT_BINARY_SLAVE

#endif //ARG_PARSE_H
