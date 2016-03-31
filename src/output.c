/**
 * \file output.c
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

#include "output.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>

#include <arpa/inet.h> //inet_ntop(), ntohl()

#define PRETTY_PRINT_SEP " "
#define PRETTY_PRINT_COL_WIDTH 5
#define COL_WIDTH_RESERVE 3
#define CSV_SEP ','
#define TCP_FLAG_UNSET_CHAR '.'

#if MAX_STR_LEN < INET6_ADDRSTRLEN
#error "MAX_STR_LEN < INET6_ADDRSTRLEN"
#endif


extern int secondary_errno;

static char global_str[MAX_STR_LEN];
static struct output_params output_params; //output parameters
static struct {
        int id;
        size_t size;
} fields[LNF_FLD_TERM_]; //fields array compressed for faster access
static size_t fields_cnt = 0; //number of fields present in fields array

static const char *ip_proto_str_table[] = {
        [0] = "HOPOPT",
        [1] = "ICMP",
        [2] = "IGMP",
        [3] = "GGP",
        [4] = "IPv4",
        [5] = "ST",
        [6] = "TCP",
        [7] = "CBT",
        [8] = "EGP",
        [9] = "IGP",
        [10] = "BBN-RCC-MON",
        [11] = "NVP-II",
        [12] = "PUP",
        [13] = "ARGUS",
        [14] = "EMCON",
        [15] = "XNET",
        [16] = "CHAOS",
        [17] = "UDP",
        [18] = "MUX",
        [19] = "DCN-MEAS",
        [20] = "HMP",
        [21] = "PRM",
        [22] = "XNS-IDP",
        [23] = "TRUNK-1",
        [24] = "TRUNK-2",
        [25] = "LEAF-1",
        [26] = "LEAF-2",
        [27] = "RDP",
        [28] = "IRTP",
        [29] = "ISO-TP4",
        [30] = "NETBLT",
        [31] = "MFE-NSP",
        [32] = "MERIT-INP",
        [33] = "DCCP",
        [34] = "3PC",
        [35] = "IDPR",
        [36] = "XTP",
        [37] = "DDP",
        [38] = "IDPR-CMTP",
        [39] = "TP++",
        [40] = "IL",
        [41] = "IPv6",
        [42] = "SDRP",
        [43] = "IPv6-Route",
        [44] = "IPv6-Frag",
        [45] = "IDRP",
        [46] = "RSVP",
        [47] = "GRE",
        [48] = "DSR",
        [49] = "BNA",
        [50] = "ESP",
        [51] = "AH",
        [52] = "I-NLSP",
        [53] = "SWIPE",
        [54] = "NARP",
        [55] = "MOBILE",
        [56] = "TLSP",
        [57] = "SKIP",
        [58] = "IPv6-ICMP",
        [59] = "IPv6-NoNxt",
        [60] = "IPv6-Opts",
        [61] = "any host internal protocol",
        [62] = "CFTP",
        [63] = "any local network",
        [64] = "SAT-EXPAK",
        [65] = "KRYPTOLAN",
        [66] = "RVD",
        [67] = "IPPC",
        [68] = "any distributed file system",
        [69] = "SAT-MON",
        [70] = "VISA",
        [71] = "IPCV",
        [72] = "CPNX",
        [73] = "CPHB",
        [74] = "WSN",
        [75] = "PVP",
        [76] = "BR-SAT-MON",
        [77] = "SUN-ND",
        [78] = "WB-MON",
        [79] = "WB-EXPAK",
        [80] = "ISO-IP",
        [81] = "VMTP",
        [82] = "SECURE-VMTP",
        [83] = "VINES",
        [84] = "TTP/IPTM",
        [85] = "NSFNET-IGP",
        [86] = "DGP",
        [87] = "TCF",
        [88] = "EIGRP",
        [89] = "OSPFIGP",
        [90] = "Sprite-RPC",
        [91] = "LARP",
        [92] = "MTP",
        [93] = "AX.25",
        [94] = "IPIP",
        [95] = "MICP",
        [96] = "SCC-SP",
        [97] = "ETHERIP",
        [98] = "ENCAP",
        [99] = "any private encryption",
        [100] = "GMTP",
        [101] = "IFMP",
        [102] = "PNNI",
        [103] = "PIM",
        [104] = "ARIS",
        [105] = "SCPS",
        [106] = "QNX",
        [107] = "A/N",
        [108] = "IPComp",
        [109] = "SNP",
        [110] = "Compaq-Peer",
        [111] = "IPX-in-IP",
        [112] = "VRRP",
        [113] = "PGM",
        [114] = "any 0-hop protocol",
        [115] = "L2TP",
        [116] = "DDX",
        [117] = "IATP",
        [118] = "STP",
        [119] = "SRP",
        [120] = "UTI",
        [121] = "SMP",
        [122] = "SM",
        [123] = "PTP",
        [124] = "ISIS over IPv4",
        [125] = "FIRE",
        [126] = "CRTP",
        [127] = "CRUDP",
        [128] = "SSCOPMCE",
        [129] = "IPLT",
        [130] = "SPS",
        [131] = "PIPE",
        [132] = "SCTP",
        [133] = "FC",
        [134] = "RSVP-E2E-IGNORE",
        [135] = "Mobility Header",
        [136] = "UDPLite",
        [137] = "MPLS-in-IP",
        [138] = "manet",
        [139] = "HIP",
        [140] = "Shim6",
        [141] = "WESP",
        [142] = "ROHC",
        //143-252 "Unassigned"
        [253] = "experimentation and testing",
        [254] = "experimentation and testing",
        [255] = "Reserved",
};

static const char *decimal_unit_table[] = {
        "", //no unit
        "k", //kilo
        "M", //mega
        "G", //giga
        "T", //tera
        "P", //peta
        "E", //exa
        "Z", //zetta
        "Y", //yotta
};

static const char *binary_unit_table[] = {
        "", //no unit
        "Ki", //kibi
        "Mi", //mebi
        "Gi", //gibi
        "Ti", //tebi
        "Pi", //pebi
        "Ei", //exbi
        "Zi", //zebi
        "Yi", //yobi
};

static const char tcp_flags_table[] = {
    'C', //CWR Congestion Window Reduced
    'E', //ECE
    'U', //URG Urgent pointer
    'A', //ACK Acknowledgment field
    'P', //PSH Push function
    'R', //RST Reset the connection
    'S', //SYN Synchronize sequence numbers
    'F', //FIN No more data from sender
};


typedef const char *(*field_to_str_t)(const void *);
static const char * field_to_str(int field, const void *data);


/** \brief Convert timestamp in uint64_t to string.
 *
 * Timestamp is composed from Unix time (number of seconds that have elapsed
 * since 1.1.1970 UTC) and milliseconds. Seconds are multiplied by 1000,
 * afterward milliseconds are added.
 *
 * \param[in] ts Seconds and milliseconds in one uint64_t variable.
 * \return String timestamp representation. Static memory.
 */
static const char * timestamp_to_str(const uint64_t *ts)
{
        time_t sec;
        uint64_t msec;
        size_t off;
        struct tm *(*timeconv)(const time_t *);

        timeconv = output_params.ts_localtime ? localtime : gmtime;

        switch (output_params.ts_conv) {
        case OUTPUT_TS_CONV_NONE:
                snprintf(global_str, sizeof (global_str), "%" PRIu64, *ts);
                break;

        case OUTPUT_TS_CONV_STR:
                assert(output_params.ts_conv_str != NULL);

                sec = *ts / 1000;
                msec = *ts % 1000;

                off = strftime(global_str, sizeof (global_str),
                                output_params.ts_conv_str, timeconv(&sec));
                snprintf(global_str + off, sizeof (global_str) - off, ".%.3" PRIu64,
                                msec);
                break;

        default:
                assert(!"unknown timestamp conversion");
        }

        return global_str;
}

static const char * double_volume_to_str(const double *volume)
{
        double volume_conv = *volume;
        size_t unit_table_idx = 0;
        const char **unit_table;

        switch (output_params.volume_conv) {
        case OUTPUT_VOLUME_CONV_NONE:
                break;

        case OUTPUT_VOLUME_CONV_METRIC_PREFIX:
                unit_table = decimal_unit_table;
                while (volume_conv > 1000.0 && unit_table_idx + 1 <
                                ARRAY_SIZE(decimal_unit_table)) {
                        unit_table_idx++;
                        volume_conv /= 1000.0;
                }
                break;

        case OUTPUT_VOLUME_CONV_BINARY_PREFIX:
                unit_table = binary_unit_table;
                while (volume_conv > 1024.0 && unit_table_idx + 1 <
                                ARRAY_SIZE(binary_unit_table)) {
                        unit_table_idx++;
                        volume_conv /= 1024.0;
                }
                break;

        default:
                assert(!"unknown volume conversion");
        }

        if (unit_table_idx == 0) { //small number or no conversion
                snprintf(global_str, sizeof (global_str), "%.1f", volume_conv);
        } else { //converted unit plus unit string from unit table
                snprintf(global_str, sizeof (global_str), "%.1f %s",
                                volume_conv, unit_table[unit_table_idx]);
        }

        return global_str;
}

static const char * volume_to_str(const uint64_t *volume)
{
        double volume_conv = *volume;
        size_t unit_table_idx = 0;
        const char **unit_table;

        switch (output_params.volume_conv) {
        case OUTPUT_VOLUME_CONV_NONE:
                break;

        case OUTPUT_VOLUME_CONV_METRIC_PREFIX:
                unit_table = decimal_unit_table;
                while (volume_conv > 1000.0 && unit_table_idx + 1 <
                                ARRAY_SIZE(decimal_unit_table)) {
                        unit_table_idx++;
                        volume_conv /= 1000.0;
                }
                break;

        case OUTPUT_VOLUME_CONV_BINARY_PREFIX:
                unit_table = binary_unit_table;
                while (volume_conv > 1024.0 && unit_table_idx + 1 <
                                ARRAY_SIZE(binary_unit_table)) {
                        unit_table_idx++;
                        volume_conv /= 1024.0;
                }
                break;

        default:
                assert(!"unknown volume conversion");
        }

        if (unit_table_idx == 0) { //small number or no conversion
                snprintf(global_str, sizeof (global_str), "%" PRIu64, *volume);
        } else { //converted unit plus unit string from unit table
                snprintf(global_str, sizeof (global_str), "%.1f %s",
                                volume_conv, unit_table[unit_table_idx]);
        }

        return global_str;
}

static const char * tcp_flags_to_str(const uint8_t *flags)
{
        size_t idx = 0;

        switch (output_params.tcp_flags_conv) {
        case OUTPUT_TCP_FLAGS_CONV_NONE:
                snprintf(global_str, sizeof (global_str), "%" PRIu8, *flags);
                break;

        case OUTPUT_TCP_FLAGS_CONV_STR:
                for (int i = 128; i > 0; i >>= 1) {
                        global_str[idx] = ((*flags & i) == i) ?
                                tcp_flags_table[idx] : TCP_FLAG_UNSET_CHAR;
                        idx++;
                }
                global_str[idx] = '\0';
                break;

        default:
                assert(!"unknown IP protocol conversion");
        }

        return global_str;
}

static const char * ip_proto_to_str(const uint8_t *proto)
{
        const char *ret;

        switch (output_params.ip_proto_conv) {
        case OUTPUT_IP_PROTO_CONV_NONE:
                snprintf(global_str, sizeof (global_str), "%" PRIu8, *proto);

                return global_str;

        case OUTPUT_IP_PROTO_CONV_STR:
                ret = ip_proto_str_table[*proto];
                if (ret == NULL) {
                        ret = "Unassigned";
                }

                return ret;

        default:
                assert(!"unknown ip protocol conversion");
        }
}

static const char * duration_to_str(const uint64_t *duration)
{
        uint64_t dur_conv = *duration;

        switch (output_params.duration_conv) {
        case OUTPUT_DURATION_CONV_NONE:
                snprintf(global_str, sizeof (global_str), "%" PRIu64,
                                *duration);
                break;

        case OUTPUT_DURATION_CONV_STR:
        {
                size_t msec;
                size_t sec;
                size_t min;

                msec = dur_conv % 1000;
                dur_conv /= 1000;
                sec = dur_conv % 60;
                dur_conv /= 60;
                min = dur_conv % 60;
                dur_conv /= 60;

                snprintf(global_str, sizeof (global_str),
                                "%2.2" PRIu64 ":%2.2zu:%2.2zu.%3.3zu",
                                dur_conv, min, sec, msec);
                break;
        }

        default:
                assert(!"unknown duration conversion");
        }

        return global_str;
}

/** \brief Convert libnf IP address to string.
 *
 * Without conversion, IP address is converted to
 * UINT[0]:UINT[1]:UINT[2]:UINT[3]. If IPv4 is present, first three UINTs are
 * zero. With conversion, inet_ntop() is used to convert binary representation
 * to string.
 *
 * \param[in] addr Binary IP address representation.
 * \return String IP address representation. Static memory.
 */
static const char * mylnf_addr_to_str(const lnf_ip_t *addr)
{
        const char *ret;

        switch (output_params.ip_addr_conv) {
        case OUTPUT_IP_ADDR_CONV_NONE:
                snprintf(global_str, sizeof (global_str),
                                "%" PRIu32 ":%" PRIu32 ":%" PRIu32 ":%" PRIu32,
                                ntohl(addr->data[0]), ntohl(addr->data[1]),
                                ntohl(addr->data[2]), ntohl(addr->data[3]));
                break;

        case OUTPUT_IP_ADDR_CONV_STR:
                if (IN6_IS_ADDR_V4COMPAT(addr->data)) { //IPv4 compatibile
                        ret = inet_ntop(AF_INET, addr->data + 3, global_str,
                                        INET_ADDRSTRLEN);
                } else { //IPv6
                        ret = inet_ntop(AF_INET6, addr->data, global_str,
                                        INET6_ADDRSTRLEN);
                }
                assert(ret != NULL);

                break;

        default:
                assert(!"unknown IP address conversion");
        }

        return global_str;
}

/** \brief Convert libnf MAC address to string.
 *
 * \param[in] mac Binary MAC address representation.
 * \return String MAC address representation. Static memory.
 */
static const char * mylnf_mac_to_str(const lnf_mac_t *mac)
{
        snprintf(global_str, 18, "%02x:%02x:%02x:%02x:%02x:%02x", mac->data[0],
                        mac->data[1], mac->data[2], mac->data[3], mac->data[4],
                        mac->data[5]);

        return global_str;
}

static const char * uint8_t_to_str(const uint8_t *u8)
{
        snprintf(global_str, sizeof (global_str), "%" PRIu8, *u8);

        return global_str;
}

static const char * uint16_t_to_str(const uint16_t *u16)
{
        snprintf(global_str, sizeof (global_str), "%" PRIu16, *u16);

        return global_str;
}

static const char * uint32_t_to_str(const uint32_t *u32)
{
        snprintf(global_str, sizeof (global_str), "%" PRIu32, *u32);

        return global_str;
}

static const char * uint64_t_to_str(const uint64_t *u64)
{
        snprintf(global_str, sizeof (global_str), "%" PRIu64, *u64);

        return global_str;
}

static const char * double_to_str(const double *d)
{
        snprintf(global_str, sizeof (global_str), "%.1f", *d);

        return global_str;
}

static const char * string_to_str(const char *str)
{
        snprintf(global_str, sizeof (global_str), "%s", str);

        return global_str;
}

/** \brief Convert lnf_brec1_t structure to string.
 *
 * \param[in] brec lnf basic record 1.
 * \return String containing lnf_brec1_t representation. Static memory.
 */
static const char * mylnf_brec_to_str(const lnf_brec1_t *brec)
{
        static char res[MAX_STR_LEN];
        size_t off = 0;

        switch (output_params.format) {
        case OUTPUT_FORMAT_PRETTY:
                off += snprintf(res + off, MAX_STR_LEN - off, "%-27s",
                                field_to_str(LNF_FLD_FIRST, &brec->first));
                off += snprintf(res + off, MAX_STR_LEN - off, "%-27s",
                                field_to_str(LNF_FLD_LAST, &brec->last));

                off += snprintf(res + off, MAX_STR_LEN - off, "%-6s",
                                field_to_str(LNF_FLD_PROT, &brec->prot));

                off += snprintf(res + off, MAX_STR_LEN - off, "%17s:",
                                field_to_str(LNF_FLD_SRCADDR, &brec->srcaddr));
                off += snprintf(res + off, MAX_STR_LEN - off, "%-7s",
                                field_to_str(LNF_FLD_SRCPORT, &brec->srcport));

                off += snprintf(res + off, MAX_STR_LEN - off, "%17s:",
                                field_to_str(LNF_FLD_DSTADDR, &brec->dstaddr));
                off += snprintf(res + off, MAX_STR_LEN - off, "%-7s",
                                field_to_str(LNF_FLD_DSTPORT, &brec->dstport));


                off += snprintf(res + off, MAX_STR_LEN - off, "%13s",
                                field_to_str(LNF_FLD_DOCTETS, &brec->bytes));
                off += snprintf(res + off, MAX_STR_LEN - off, "%13s",
                                field_to_str(LNF_FLD_DPKTS, &brec->pkts));
                off += snprintf(res + off, MAX_STR_LEN - off, "%13s",
                                field_to_str(LNF_FLD_AGGR_FLOWS, &brec->flows));
                break;

        case OUTPUT_FORMAT_CSV:
                off += snprintf(res + off, MAX_STR_LEN - off, "%s%c",
                                field_to_str(LNF_FLD_FIRST, &brec->first),
                                CSV_SEP);
                off += snprintf(res + off, MAX_STR_LEN - off, "%s%c",
                                field_to_str(LNF_FLD_LAST, &brec->last),
                                CSV_SEP);

                off += snprintf(res + off, MAX_STR_LEN - off, "%s%c",
                                field_to_str(LNF_FLD_PROT, &brec->prot),
                                CSV_SEP);

                off += snprintf(res + off, MAX_STR_LEN - off, "%s%c",
                                field_to_str(LNF_FLD_SRCADDR, &brec->srcaddr),
                                CSV_SEP);
                off += snprintf(res + off, MAX_STR_LEN - off, "%s%c",
                                field_to_str(LNF_FLD_SRCPORT, &brec->srcport),
                                CSV_SEP);

                off += snprintf(res + off, MAX_STR_LEN - off, "%s%c",
                                field_to_str(LNF_FLD_DSTADDR, &brec->dstaddr),
                                CSV_SEP);
                off += snprintf(res + off, MAX_STR_LEN - off, "%s%c",
                                field_to_str(LNF_FLD_DSTPORT, &brec->dstport),
                                CSV_SEP);


                off += snprintf(res + off, MAX_STR_LEN - off, "%s%c",
                                field_to_str(LNF_FLD_DOCTETS, &brec->bytes),
                                CSV_SEP);
                off += snprintf(res + off, MAX_STR_LEN - off, "%s%c",
                                field_to_str(LNF_FLD_DPKTS, &brec->pkts),
                                CSV_SEP);
                off += snprintf(res + off, MAX_STR_LEN - off, "%s",
                                field_to_str(LNF_FLD_AGGR_FLOWS, &brec->flows));
                break;

        default:
                assert(!"unknown output format");
        }

        return res;
}


field_to_str_t field_to_str_func_table[] = {
        [LNF_FLD_ZERO_] = NULL,

        /* Timestamps. */
        [LNF_FLD_FIRST] = (field_to_str_t)timestamp_to_str,
        [LNF_FLD_LAST] = (field_to_str_t)timestamp_to_str,
        [LNF_FLD_RECEIVED] = (field_to_str_t)timestamp_to_str,

        /* Statistical fields. */
        [LNF_FLD_DOCTETS] = (field_to_str_t)volume_to_str,
        [LNF_FLD_DPKTS] = (field_to_str_t)volume_to_str,
        [LNF_FLD_OUT_BYTES] = (field_to_str_t)volume_to_str,
        [LNF_FLD_OUT_PKTS] = (field_to_str_t)volume_to_str,
        [LNF_FLD_AGGR_FLOWS] = (field_to_str_t)volume_to_str,

        /* TCP flags. */
        [LNF_FLD_TCP_FLAGS] = (field_to_str_t)tcp_flags_to_str,

        /* IP protocol. */
        [LNF_FLD_PROT] = (field_to_str_t)ip_proto_to_str,

        /* Computed: duration. */
        [LNF_FLD_CALC_DURATION] = (field_to_str_t)duration_to_str,
        /* Computed: volumetric. */
        [LNF_FLD_CALC_BPS] = (field_to_str_t)double_volume_to_str,
        [LNF_FLD_CALC_PPS] = (field_to_str_t)double_volume_to_str,
        [LNF_FLD_CALC_BPP] = (field_to_str_t)double_volume_to_str,

        [LNF_FLD_TERM_] = NULL,
};


static const char * field_get_name(int field)
{
        static char fld_name_buff[LNF_INFO_BUFSIZE];

        if (field <= LNF_FLD_ZERO_ || field >= LNF_FLD_TERM_) {
                return NULL;
        }

        lnf_fld_info(field, LNF_FLD_INFO_NAME, fld_name_buff, LNF_INFO_BUFSIZE);

        return fld_name_buff;
}


static const char * field_to_str(int field, const void *data)
{
        const int type = field_get_type(field);
        field_to_str_t to_str_func = field_to_str_func_table[field];

        if (type == -1) {
                return NULL;
        }

        /*
         * If "to_str" function was not found in function table, use fallback
         * "to_str" function corresponding to field type.
         */
        if (to_str_func == NULL) {
                switch (type) {
                case LNF_UINT8:
                        to_str_func = (field_to_str_t)uint8_t_to_str;
                        break;

                case LNF_UINT16:
                        to_str_func = (field_to_str_t)uint16_t_to_str;
                        break;

                case LNF_UINT32:
                        to_str_func = (field_to_str_t)uint32_t_to_str;
                        break;

                case LNF_UINT64:
                        to_str_func = (field_to_str_t)uint64_t_to_str;
                        break;

                case LNF_DOUBLE:
                        to_str_func = (field_to_str_t)double_to_str;
                        break;

                case LNF_ADDR:
                        to_str_func = (field_to_str_t)mylnf_addr_to_str;
                        break;

                case LNF_MAC:
                        to_str_func = (field_to_str_t)mylnf_mac_to_str;
                        break;

                case LNF_BASIC_RECORD1:
                        to_str_func = (field_to_str_t)mylnf_brec_to_str;
                        break;

                case LNF_STRING:
                        to_str_func = (field_to_str_t)string_to_str;
                        break;

                case LNF_NONE:
                case LNF_MPLS:
                        assert(!"unimplemented LNF data type");

                default:
                        assert(!"unknown LNF data type");
                }
        }

        return to_str_func(data);
}

static void print_field(const char *string, size_t string_width,
                size_t space_width, bool last)
{
        if (last) { //no spacing or CSV separator after last field
                puts(string); //appends newline
                return;
        }

        switch (output_params.format) {
        case OUTPUT_FORMAT_PRETTY:
                printf("%-*s%*s", (int)string_width, string, (int)space_width,
                                PRETTY_PRINT_SEP);
                break;

        case OUTPUT_FORMAT_CSV:
                printf("%s%c" , string, CSV_SEP);
                break;

        default:
                assert(!"unknown output format");
        }
}


void output_setup(struct output_params op, const struct field_info *fi)
{
        output_params = op;

        /* Fill fields array. */
        for (size_t i = 0; i < LNF_FLD_TERM_; ++i) {
                if (fi[i].id == 0) {
                        continue; //field is not present
                }
                fields[fields_cnt].id = i;
                fields[fields_cnt].size = field_get_size(i);
                fields_cnt++;
        }
}


void print_rec(const uint8_t *data)
{
        size_t off = 0;
        static bool first_rec = 1;
        static size_t col_width[LNF_FLD_TERM_];

        if (first_rec) {
                for (size_t i = 0; i < fields_cnt; ++i) {
                        const char *header_str = field_get_name(fields[i].id);
                        const size_t header_str_len = strlen(header_str);
                        const size_t field_str_len = strlen(field_to_str(
                                                fields[i].id, data + off));

                        col_width[i] = header_str_len > field_str_len ?
                                header_str_len : field_str_len;
                        col_width[i] += COL_WIDTH_RESERVE;
                        off += fields[i].size;

                        print_field(header_str, col_width[i],
                                        PRETTY_PRINT_COL_WIDTH -
                                        COL_WIDTH_RESERVE,
                                        i == (fields_cnt - 1));
                }

                off = 0;
                first_rec = false;
        }

        /* Loop through the fields in one record. */
        for (size_t i = 0; i < fields_cnt; ++i) {
                print_field(field_to_str(fields[i].id, data + off),
                                col_width[i], PRETTY_PRINT_COL_WIDTH -
                                COL_WIDTH_RESERVE,
                                i == (fields_cnt - 1));
                off += fields[i].size;
        }
}


error_code_t print_mem(lnf_mem_t *mem, size_t limit)
{
        lnf_rec_t *rec; //record = line
        size_t rec_cntr = 0; //aka lines counter

        lnf_mem_cursor_t *cursor; //current record (line) cursor
        size_t fld_max_size = 0; //maximum data size length in bytes
        size_t data_max_strlen[LNF_FLD_TERM_] = {0}; //maximum data string len


        secondary_errno = lnf_rec_init(&rec);
        if (secondary_errno != LNF_OK) {
                print_err(E_LNF, secondary_errno, "lnf_rec_init()");
                return E_LNF;
        }


        /*
         * Find out maximum data type size of present fields, length of headers
         * and last present field ID.
         */
        for (size_t i = 0; i < fields_cnt; ++i) {
                size_t header_str_len = strlen(field_get_name(fields[i].id));

                MAX_ASSIGN(fld_max_size, fields[i].size);
                MAX_ASSIGN(data_max_strlen[fields[i].id], header_str_len);
        }

        /* Find out max data length, converted to string. */
        lnf_mem_first_c(mem, &cursor);
        while (cursor != NULL) { //row loop
                char buff[fld_max_size];

                lnf_mem_read_c(mem, cursor, rec);

                for (size_t i = 0; i < fields_cnt; ++i) { //column loop
                        size_t data_str_len;

                        //XXX: lnf_rec_fget() may return LNF_ERR_UNKFLD even if
                        //field is present (e.g. if duration is zero).
                        lnf_rec_fget(rec, fields[i].id, buff);
                        data_str_len = strlen(field_to_str(fields[i].id, buff));
                        MAX_ASSIGN(data_max_strlen[fields[i].id], data_str_len);
                }

                if (++rec_cntr == limit) {
                        break;
                }

                lnf_mem_next_c(mem, &cursor);
        }
        rec_cntr = 0;


        /* Actual printing: header. */
        for (size_t i = 0; i < fields_cnt; ++i) { //column loop
                print_field(field_get_name(fields[i].id),
                                data_max_strlen[fields[i].id],
                                PRETTY_PRINT_COL_WIDTH, i == (fields_cnt - 1));
        }

        /* Actual printing: field data converted to string. */
        lnf_mem_first_c(mem, &cursor);
        while (cursor != NULL) { //row loop
                char buff[fld_max_size];

                lnf_mem_read_c(mem, cursor, rec);

                for (size_t i = 0; i < fields_cnt; ++i) { //column loop
                        //XXX: see above lnf_rec_fget()
                        lnf_rec_fget(rec, fields[i].id, buff);

                        print_field(field_to_str(fields[i].id, buff),
                                        data_max_strlen[fields[i].id],
                                        PRETTY_PRINT_COL_WIDTH,
                                        i == (fields_cnt - 1));
                }

                if (++rec_cntr == limit) {
                        break;
                }

                lnf_mem_next_c(mem, &cursor);
        }

        lnf_rec_free(rec);

        return E_OK;
}

void print_summary(const struct stats *stats, double duration)
{
        int header_len;
        const double flows_per_sec = stats->flows / duration;


        if (output_params.summary != OUTPUT_SUMMARY_YES) {
                return;
        }

        putchar('\n');
        header_len = printf("summary: ");
        assert(header_len >= 0);

        printf("%s flows, ", volume_to_str(&stats->flows));
        printf("%s packets, ", volume_to_str(&stats->pkts));
        printf("%s bytes\n", volume_to_str(&stats->bytes));

        printf("%*s%f seconds, %s flows/second\n", header_len, "", duration,
                        double_volume_to_str(&flows_per_sec));
}
