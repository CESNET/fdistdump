/**
 * \file print.c
 * \brief Implementation of functions for printing records and fields.
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

#include "print.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>

#include <arpa/inet.h> //inet_ntop()

#define FIELDS_SIZE (LNF_FLD_TERM_ + 1) //currently 256 bits


extern int secondary_errno;


static char * timestamp_to_str(uint64_t ts)
{
        static char str[MAX_STR_LEN];
        time_t sec = ts / 1000;
        uint64_t msec = ts % 1000;
        struct tm *sec_tm = gmtime(&sec);
        size_t offset;

        offset = strftime(str, sizeof (str), "%F %T", sec_tm);
        snprintf(str + offset, sizeof (str) - offset, ".%lu", msec);

        return str;
}

/** \brief Convert libnf IP address to string.
 *
 * Distinguish IPv4 vs IPv6 address and use inet_ntop() to convert binary
 * representation to string.
 *
 * \param[in] addr Binary IP address representation.
 * \return String IP address representation. Static memory.
 */
static char * mylnf_addr_to_str(lnf_ip_t addr)
{
        static char str[INET6_ADDRSTRLEN];
        const char *ret;

        if (IN6_IS_ADDR_V4COMPAT(addr.data)) { //IPv4 compatibile
                ret = inet_ntop(AF_INET, &addr.data[3], str, INET_ADDRSTRLEN);
        } else { //IPv6
                ret = inet_ntop(AF_INET6, &addr, str, INET6_ADDRSTRLEN);
        }

        assert(ret != NULL);

        return str;
}

/** \brief Convert libnf MAC address to string.
 *
 * \param[in] mac Binary MAC address representation.
 * \return String MAC address representation. Static memory.
 */
static char * mylnf_mac_to_str(lnf_mac_t mac)
{
        static char str[18];

        snprintf(str, 18, "%02x:%02x:%02x:%02x:%02x:%02x", mac.data[0],
                        mac.data[1], mac.data[2], mac.data[3], mac.data[4],
                        mac.data[5]);

        return str;
}

char * mylnf_brec_to_str(lnf_brec1_t brec)
{
        static char str[MAX_STR_LEN];
        size_t offset = 0;

        offset += snprintf(str + offset, MAX_STR_LEN - offset, "%-27s",
                        timestamp_to_str(brec.first));
        offset += snprintf(str + offset, MAX_STR_LEN - offset, "%-27s",
                        timestamp_to_str(brec.last));

        offset += snprintf(str + offset, MAX_STR_LEN - offset, "%5" PRIu8,
                        brec.prot);

        offset += snprintf(str + offset, MAX_STR_LEN - offset, "%17s:%-7"
                        PRIu16, mylnf_addr_to_str(brec.srcaddr), brec.srcport);
        offset += snprintf(str + offset, MAX_STR_LEN - offset, "%17s:%-7"
                        PRIu16, mylnf_addr_to_str(brec.dstaddr), brec.dstport);

        offset += snprintf(str + offset, MAX_STR_LEN - offset,
                        "%13" PRIu64 "%13" PRIu64 "%13" PRIu64,
                        brec.bytes, brec.pkts, brec.flows);

#if 0
        if (ret == NULL) {
                print_err(E_INTERNAL, 0, "addr_to_str()");
                return E_INTERNAL;
        }

        printf("%lu -> %lu\t", brec->first, brec->last);
        printf("%15s:%-5hu -> %15s:%-5hu\t", srcaddr_str, brec->srcport,
                        dstaddr_str, brec->dstport);
        printf("%lu\t%lu\t%lu\n", brec->pkts, brec->bytes, brec->flows);

        return E_OK;
#endif

        return str;
}




struct fields {
        uint8_t present[FIELDS_SIZE / (8 * sizeof (uint8_t))];
        size_t count;
        size_t cursor;
};

void fields_init(struct fields *f)
{
        assert(f != NULL);
        memset(f, 0, sizeof (struct fields));
}

static void fields_add_new(struct fields *f, int new_field)
{
        size_t arr_idx;
        size_t bit_idx;

        assert(f != NULL);

        if (new_field <= LNF_FLD_ZERO_ || new_field >= LNF_FLD_TERM_) {
                return;
        }

        arr_idx = new_field / (8 * MEMBER_SIZE(struct fields, present[0]));
        bit_idx = new_field % (8 * MEMBER_SIZE(struct fields, present[0]));
        BIT_SET(f->present[arr_idx], bit_idx);
        f->count++;
}

static int fields_iter_next(struct fields *f)
{
        assert(f != NULL);

        for (size_t i = f->cursor; i < FIELDS_SIZE; ++i) {
                size_t arr_idx = i /
                        (8 * MEMBER_SIZE(struct fields, present[0]));
                size_t bit_idx = i %
                        (8 * MEMBER_SIZE(struct fields, present[0]));

                if (BIT_TEST(f->present[arr_idx], bit_idx)) {
                        f->cursor = i + 1;
                        return i;
                }
        }

        return -1;
}

static void fields_iter_reset(struct fields *f)
{
        assert(f != NULL);
        f->cursor = 0;
}

static size_t fields_get_count(const struct fields *f)
{
        assert(f != NULL);
        return f->count;
}


static char * field_get_name(int field)
{
        static char fld_name_buff[LNF_INFO_BUFSIZE];

        if (field <= LNF_FLD_ZERO_ || field >= LNF_FLD_TERM_) {
                return NULL;
        }

        lnf_fld_info(field, LNF_FLD_INFO_NAME, fld_name_buff, LNF_INFO_BUFSIZE);

        return fld_name_buff;
}

static int field_get_type(int field)
{
        int type;

        if (field <= LNF_FLD_ZERO_ || field >= LNF_FLD_TERM_) {
                return -1;
        }

        lnf_fld_info(field, LNF_FLD_INFO_TYPE, &type, sizeof (type));

        return type;
}

static size_t field_get_size(int field)
{
        int type = field_get_type(field);

        if (type == -1) {
                return 0;
        }

        switch (type) {
        case LNF_UINT8:
                return sizeof (uint8_t);

        case LNF_UINT16:
                return sizeof (uint16_t);

        case LNF_UINT32:
                return sizeof (uint32_t);

        case LNF_UINT64:
                return sizeof (uint64_t);

        case LNF_DOUBLE:
                return sizeof (double);

        case LNF_ADDR:
                return sizeof (lnf_ip_t);

        case LNF_MAC:
                return sizeof (lnf_mac_t);

        case LNF_BASIC_RECORD1:
                return sizeof (lnf_brec1_t);

        case LNF_NONE:
        case LNF_STRING:
        case LNF_MPLS:
                assert(!"unimplemented LNF data type");

        default:
                assert(!"unknown LNF data type");
        }
}

static char * field_to_str(int field, char *data)
{
        static char str[MAX_STR_LEN];
        int type = field_get_type(field);

        if (type == -1) {
                return NULL;
        }

        /* Timestamps are LNF_UINT64, but different string format. */
        if (field == LNF_FLD_FIRST || field == LNF_FLD_LAST ||
                        field == LNF_FLD_RECEIVED) {
                snprintf(str, MAX_STR_LEN, timestamp_to_str(*(uint64_t *)data));

                return str;
        }

        switch (type) {
        case LNF_UINT8:
                snprintf(str, MAX_STR_LEN, "%" PRIu8, *(uint8_t *)data);
                break;

        case LNF_UINT16:
                snprintf(str, MAX_STR_LEN, "%" PRIu16, *(uint16_t *)data);
                break;

        case LNF_UINT32:
                snprintf(str, MAX_STR_LEN, "%" PRIu32, *(uint32_t *)data);
                break;

        case LNF_UINT64:
                snprintf(str, MAX_STR_LEN, "%" PRIu64, *(uint64_t *)data);
                break;

        case LNF_DOUBLE:
                snprintf(str, MAX_STR_LEN, "%f", *(double *)data);
                break;

        case LNF_ADDR:
                snprintf(str, MAX_STR_LEN,
                                mylnf_addr_to_str(*(lnf_ip_t *)data));
                break;

        case LNF_MAC:
                snprintf(str, MAX_STR_LEN,
                                mylnf_mac_to_str(*(lnf_mac_t *)data));
                break;

        case LNF_BASIC_RECORD1:
                snprintf(str, MAX_STR_LEN,
                                mylnf_brec_to_str(*(lnf_brec1_t *)data));
                break;

        case LNF_NONE:
        case LNF_STRING:
        case LNF_MPLS:
                assert(!"unimplemented LNF data type");

        default:
                assert(!"unknown LNF data type");
        }

        return str;
}


error_code_t print_aggr_mem(lnf_mem_t *mem, size_t limit,
                const struct agg_param *ap, size_t ap_cnt)
{
        error_code_t primary_errno = E_OK;
        size_t rec_cntr = 0;
        lnf_mem_cursor_t *cursor;
        lnf_rec_t *rec;
        struct fields fields;
        int field;
        size_t field_max_size = 0;
        size_t max_data_str_len[LNF_FLD_TERM_] = {0};

        secondary_errno = lnf_rec_init(&rec);
        if (secondary_errno != LNF_OK) {
                print_err(E_LNF, secondary_errno, "lnf_rec_init()");
                return E_LNF;
        }


        /* Default aggragation fields: first, last, flows, packets, bytes. */
        fields_init(&fields);
        fields_add_new(&fields, LNF_FLD_FIRST);
        fields_add_new(&fields, LNF_FLD_LAST);
        fields_add_new(&fields, LNF_FLD_AGGR_FLOWS);
        fields_add_new(&fields, LNF_FLD_DPKTS);
        fields_add_new(&fields, LNF_FLD_DOCTETS);
        for (size_t i = 0; i < ap_cnt; ++i, ++ap) {
                fields_add_new(&fields, ap->field);
        }


        /* Find out maximum data type size of present fields. */
        while ((field = fields_iter_next(&fields)) != -1) {
                size_t field_size = field_get_size(field);

                field_max_size = MAX(field_max_size, field_size);
        }
        fields_iter_reset(&fields);


        /* Find out maximum length of each field data converted to string. */
        while ((field = fields_iter_next(&fields)) != -1) {
                size_t header_str_len = strlen(field_get_name(field));

                max_data_str_len[field] = MAX(max_data_str_len[field],
                                header_str_len);
        }
        fields_iter_reset(&fields);

        lnf_mem_first_c(mem, &cursor);
        while (cursor != NULL) {
                char buff[field_max_size];

                lnf_mem_read_c(mem, cursor, rec);

                while ((field = fields_iter_next(&fields)) != -1) {
                        size_t data_str_len;

                        assert(lnf_rec_fget(rec, field, buff) == LNF_OK);
                        data_str_len = strlen(field_to_str(field, buff));
                        max_data_str_len[field] = MAX(max_data_str_len[field],
                                        data_str_len);
                }
                fields_iter_reset(&fields);

                if (++rec_cntr == limit) {
                        break;
                }

                lnf_mem_next_c(mem, &cursor);
        }
        rec_cntr = 0;


        /* Actual printing: header. */
        while ((field = fields_iter_next(&fields)) != -1) {
                size_t field_size = field_get_size(field);

                printf("%-*s", max_data_str_len[field] + PRINT_SPACING,
                                field_get_name(field));
                field_max_size = MAX(field_max_size, field_size);
        }
        putchar('\n');
        fields_iter_reset(&fields);

        /* Field data. */
        lnf_mem_first_c(mem, &cursor);
        while (cursor != NULL) {
                char buff[field_max_size];

                lnf_mem_read_c(mem, cursor, rec);

                while ((field = fields_iter_next(&fields)) != -1) {
                        assert(lnf_rec_fget(rec, field, buff) == LNF_OK);
                        printf("%-*s", max_data_str_len[field] + PRINT_SPACING,
                                        field_to_str(field, buff));
                }
                putchar('\n');
                fields_iter_reset(&fields);

                if (++rec_cntr == limit) {
                        goto free_lnf_rec;
                }

                lnf_mem_next_c(mem, &cursor);
        }

free_lnf_rec:
        lnf_rec_free(rec);

        return primary_errno;
}
