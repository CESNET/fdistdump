/**
 * @brief Functions for printing IP flow records and fields.
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

#include "output.h"

#include <assert.h>             // for assert
#include <inttypes.h>           // for fixed-width integer types
#include <stdio.h>              // for printf, snprintf, putchar, puts
#include <string.h>             // for strlen
#include <time.h>               // for strftime, gmtime, localtime

/*
 * Define System V source as a workaround for the "IN6_IS_ADDR_UNSPECIFIED can
 * use undefined s6_addr32" GNU C library bug (fixed in version 2.25).
 * https://sourceware.org/bugzilla/show_bug.cgi?id=16421
 */
#if __GLIBC__ <= 2 && __GLIBC_MINOR__ < 25
#define __USE_MISC
#include <arpa/inet.h>          // for inet_ntop
#else
#include <arpa/inet.h>          // for inet_ntop
#endif
#include <features.h>           // for __GLIBC_MINOR__, __GLIBC__
#include <netinet/in.h>         // for ntohl, INET6_ADDRSTRLEN, IN6_IS_ADDR_...
#include <sys/socket.h>         // for AF_INET, AF_INET6

#include "common.h"             // for metadata_summ, processed_summ, ARRAY_...
#include "fields.h"             // for struct fields, field_get_*, ...
#include "print.h"              // for SNPRINTF_APPEND, PRINT_ERROR


#define PRETTY_PRINT_SEP " "
#define PRETTY_PRINT_COL_WIDTH 5
#define COL_WIDTH_RESERVE 4
#define CSV_SEP ','
#define TCP_FLAG_UNSET_CHAR '.'

#if MAX_STR_LEN < INET6_ADDRSTRLEN
#error "MAX_STR_LEN < INET6_ADDRSTRLEN"
#endif


static char global_str[MAX_STR_LEN];
static struct output_params output_params; //output parameters
const struct fields *fields;
static bool first_item = true; //first item will not print '\n'

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
    // 143-252 "Unassigned"
    [253] = "experimentation and testing",
    [254] = "experimentation and testing",
    [255] = "Reserved",
};

static const char *const decimal_unit_table[] = {
    "",   // no unit
    "k",  // kilo
    "M",  // mega
    "G",  // giga
    "T",  // tera
    "P",  // peta
    "E",  // exa
    "Z",  // zetta
    "Y",  // yotta
};

static const char *const binary_unit_table[] = {
    "",    // no unit
    "Ki",  // kibi
    "Mi",  // mebi
    "Gi",  // gibi
    "Ti",  // tebi
    "Pi",  // pebi
    "Ei",  // exbi
    "Zi",  // zebi
    "Yi",  // yobi
};

static const char tcp_flags_table[] = {
    'C',  // CWR Congestion Window Reduced
    'E',  // ECE
    'U',  // URG Urgent pointer
    'A',  // ACK Acknowledgment field
    'P',  // PSH Push function
    'R',  // RST Reset the connection
    'S',  // SYN Synchronize sequence numbers
    'F',  // FIN No more data from sender
};


typedef const char *(*field_to_str_t)(const void *const);

static const char *
field_to_str(const int field, const void *const data);


/**
 * @brief Convert a timestamp in uint64_t to string.
 *
 * Timestamp is composed of Unix time (number of seconds that have elapsed since
 * 1.1.1970 UTC) and additional milliseconds elapsed since the last full second.
 *
 * @param[in] ts Unix time extended to a milliseconds precision.
 *
 * @return Textual representation of the timestamp (in static memory).
 */
static const char *
timestamp_to_str(const uint64_t *ts)
{
    switch (output_params.ts_conv) {
    case OUTPUT_TS_CONV_NONE:
        snprintf(global_str, sizeof (global_str), "%" PRIu64, *ts);
        break;
    case OUTPUT_TS_CONV_STR: {
        assert(output_params.ts_conv_str);
        struct tm *(*const timeconv)(const time_t *) =
            output_params.ts_localtime ? localtime : gmtime;
        const time_t sec = *ts / 1000;
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
        const uint64_t written = strftime(global_str, sizeof (global_str),
                                        output_params.ts_conv_str,
                                        timeconv(&sec));
#pragma GCC diagnostic warning "-Wformat-nonliteral"
        if (written == 0) {
            return "too long";
        }
        const uint64_t msec = *ts % 1000;
        snprintf(global_str + written, sizeof (global_str) - written,
                 ".%.3" PRIu64, msec);
        break;
    }
    case OUTPUT_TS_CONV_UNSET:
        assert(!"illegal timestamp conversion");
    default:
        assert(!"unknown timestamp conversion");
    }

    return global_str;
}

/**
 * @brief TODO
 *
 * @param volume
 *
 * @return
 */
static const char *
double_volume_to_str(const double *const volume)
{
    double volume_conv = *volume;
    const char *const *unit_table;
    uint64_t unit_table_idx = 0;

    switch (output_params.volume_conv) {
    case OUTPUT_VOLUME_CONV_NONE:
        break;

    case OUTPUT_VOLUME_CONV_METRIC_PREFIX:
        unit_table = decimal_unit_table;
        while (volume_conv > 1000.0
               && unit_table_idx + 1 < ARRAY_SIZE(decimal_unit_table))
        {
            unit_table_idx++;
            volume_conv /= 1000.0;
        }
        break;

    case OUTPUT_VOLUME_CONV_BINARY_PREFIX:
        unit_table = binary_unit_table;
        while (volume_conv > 1024.0
               && unit_table_idx + 1 < ARRAY_SIZE(binary_unit_table))
        {
            unit_table_idx++;
            volume_conv /= 1024.0;
        }
        break;

    case OUTPUT_VOLUME_CONV_UNSET:
        assert(!"illegal volume conversion");
    default:
        assert(!"unknown volume conversion");
    }

    if (unit_table_idx == 0) {  // small number or no conversion
        snprintf(global_str, sizeof (global_str), "%.1f", volume_conv);
    } else {  // converted unit plus unit string from unit table
        snprintf(global_str, sizeof (global_str), "%.1f %s", volume_conv,
                 unit_table[unit_table_idx]);
    }

    return global_str;
}

static const char *
volume_to_str(const uint64_t *const volume)
{
    double volume_conv = *volume;
    const char *const *unit_table;
    uint64_t unit_table_idx = 0;

    switch (output_params.volume_conv) {
    case OUTPUT_VOLUME_CONV_NONE:
        break;

    case OUTPUT_VOLUME_CONV_METRIC_PREFIX:
        unit_table = decimal_unit_table;
        while (volume_conv > 1000.0
               && unit_table_idx + 1 < ARRAY_SIZE(decimal_unit_table))
        {
            unit_table_idx++;
            volume_conv /= 1000.0;
        }
        break;

    case OUTPUT_VOLUME_CONV_BINARY_PREFIX:
        unit_table = binary_unit_table;
        while (volume_conv > 1024.0
               && unit_table_idx + 1 < ARRAY_SIZE(binary_unit_table))
        {
            unit_table_idx++;
            volume_conv /= 1024.0;
        }
        break;

    case OUTPUT_VOLUME_CONV_UNSET:
        assert(!"illegal volume conversion");
    default:
        assert(!"unknown volume conversion");
    }

    if (unit_table_idx == 0) {  // small number or no conversion
        snprintf(global_str, sizeof (global_str), "%" PRIu64, *volume);
    } else {  // converted unit plus unit string from unit table
        snprintf(global_str, sizeof (global_str), "%.1f %s", volume_conv,
                 unit_table[unit_table_idx]);
    }

    return global_str;
}

/**
 * @brief TODO
 *
 * @param flags
 *
 * @return
 */
static const char *
tcp_flags_to_str(const uint8_t *const flags)
{
    switch (output_params.tcp_flags_conv) {
    case OUTPUT_TCP_FLAGS_CONV_NONE:
        snprintf(global_str, sizeof (global_str), "%" PRIu8, *flags);
        break;

    case OUTPUT_TCP_FLAGS_CONV_STR:
    {
        uint64_t idx = 0;
        for (int i = 128; i > 0; i >>= 1) {
            global_str[idx] = ((*flags & i) == i) ?
                tcp_flags_table[idx] : TCP_FLAG_UNSET_CHAR;
            idx++;
        }
        global_str[idx] = '\0';
        break;
    }
    case OUTPUT_TCP_FLAGS_CONV_UNSET:
        assert(!"illegal IP protocol conversion");
    default:
        assert(!"unknown IP protocol conversion");
    }

    return global_str;
}

/**
 * @brief TODO
 *
 * @param proto
 *
 * @return
 */
static const char *
ip_proto_to_str(const uint8_t *const proto)
{
    switch (output_params.ip_proto_conv) {
    case OUTPUT_IP_PROTO_CONV_NONE:
        snprintf(global_str, sizeof (global_str), "%" PRIu8, *proto);
        return global_str;

    case OUTPUT_IP_PROTO_CONV_STR:
        if (ip_proto_str_table[*proto] == NULL) {
            return "Unassigned";
        } else {
            return ip_proto_str_table[*proto];
        }

    case OUTPUT_IP_PROTO_CONV_UNSET:
        assert(!"illegal ip protocol conversion");
    default:
        assert(!"unknown ip protocol conversion");
    }
}

/**
 * @brief TODO
 *
 * @param duration
 *
 * @return 
 */
static const char *
duration_to_str(const uint64_t *const duration)
{

    switch (output_params.duration_conv) {
    case OUTPUT_DURATION_CONV_NONE:
        snprintf(global_str, sizeof (global_str), "%" PRIu64, *duration);
        break;

    case OUTPUT_DURATION_CONV_STR:
    {
        uint64_t dur_conv = *duration;
        const uint64_t msec = dur_conv % 1000;
        dur_conv /= 1000;
        const uint64_t sec = dur_conv % 60;
        dur_conv /= 60;
        uint64_t min = dur_conv % 60;
        dur_conv /= 60;

        snprintf(global_str, sizeof (global_str), "%2.2" PRIu64
                 ":%2.2zu:%2.2zu.%3.3zu", dur_conv, min, sec, msec);
        break;
    }

    case OUTPUT_DURATION_CONV_UNSET:
        assert(!"illegal duration conversion");
    default:
        assert(!"unknown duration conversion");
    }

    return global_str;
}

/**
 * @brief Convert libnf IP address to a string.
 *
 * Without conversion, IP address is converted to
 * UINT[0]:UINT[1]:UINT[2]:UINT[3]. If IPv4 is present, first three UINTs are
 * zero. With conversion, inet_ntop() is used to convert binary representation
 * to string.
 *
 * @param[in] addr Binary IP address representation.
 *
 * @return Static read-only string IP address representation.
 */
static const char *
libnf_addr_to_str(const lnf_ip_t *const addr)
{
    switch (output_params.ip_addr_conv) {
    case OUTPUT_IP_ADDR_CONV_NONE:
        snprintf(global_str, sizeof (global_str),
                 "%" PRIu32 ":%" PRIu32 ":%" PRIu32 ":%" PRIu32,
                 ntohl(addr->data[0]), ntohl(addr->data[1]),
                 ntohl(addr->data[2]), ntohl(addr->data[3]));
        break;

    case OUTPUT_IP_ADDR_CONV_STR:
    {
        const char *ret;
        if (IN6_IS_ADDR_V4COMPAT(addr->data)) {  // IPv4 compatibile
            ret = inet_ntop(AF_INET, addr->data + 3, global_str, INET_ADDRSTRLEN);
        } else {  // IPv6
            ret = inet_ntop(AF_INET6, addr->data, global_str, INET6_ADDRSTRLEN);
        }
        ERROR_IF(!ret, E_INTERNAL, "inet_ntop()");
        break;
    }
    case OUTPUT_IP_ADDR_CONV_UNSET:
        assert(!"illegal IP address conversion");
    default:
        assert(!"unknown IP address conversion");
    }

    return global_str;
}

/**
 * @brief Convert libnf MAC address to a string.
 *
 * @param[in] mac Binary MAC address representation.
 *
 * @return Static read-only string MAC address representation.
 */
static const char *
mylnf_mac_to_str(const lnf_mac_t *const mac)
{
    snprintf(global_str, 18, "%02x:%02x:%02x:%02x:%02x:%02x", mac->data[0],
             mac->data[1], mac->data[2], mac->data[3], mac->data[4],
             mac->data[5]);
    return global_str;
}

/**
 * @brief TODO
 *
 * @param u8
 *
 * @return 
 */
static const char *
uint8_t_to_str(const uint8_t *const u8)
{
    snprintf(global_str, sizeof (global_str), "%" PRIu8, *u8);
    return global_str;
}

/**
 * @brief TODO
 *
 * @param u16
 *
 * @return 
 */
static const char *
uint16_t_to_str(const uint16_t *const u16)
{
    snprintf(global_str, sizeof (global_str), "%" PRIu16, *u16);
    return global_str;
}

/**
 * @brief TODO
 *
 * @param u32
 *
 * @return 
 */
static const char *
uint32_t_to_str(const uint32_t *const u32)
{
    snprintf(global_str, sizeof (global_str), "%" PRIu32, *u32);
    return global_str;
}

/**
 * @brief TODO
 *
 * @param u64
 *
 * @return 
 */
static const char *
uint64_t_to_str(const uint64_t *const u64)
{
    snprintf(global_str, sizeof (global_str), "%" PRIu64, *u64);
    return global_str;
}

/**
 * @brief TODO
 *
 * @param d
 *
 * @return 
 */
static const char *
double_to_str(const double *const d)
{
    snprintf(global_str, sizeof (global_str), "%.1f", *d);
    return global_str;
}

/**
 * @brief TODO
 *
 * @param str
 *
 * @return 
 */
static const char *
string_to_str(const char *const str)
{
    snprintf(global_str, sizeof (global_str), "%s", str);
    return global_str;
}

/**
 * @brief Convert the lnf_brec1_t structure to the textual representation.
 *
 * @param[in] brec lnf basic record 1.
 *
 * @return Textual representation of the record (in static memory).
 */
static const char *
mylnf_brec_to_str(const lnf_brec1_t *brec)
{
    static char res[MAX_STR_LEN];
    char *str_term = res;
    uint64_t remaining = sizeof (res);

    switch (output_params.format) {
    case OUTPUT_FORMAT_PRETTY:
        SNPRINTF_APPEND(str_term, remaining, "%-27s",
                        field_to_str(LNF_FLD_FIRST, &brec->first));
        SNPRINTF_APPEND(str_term, remaining, "%-27s",
                        field_to_str(LNF_FLD_LAST, &brec->last));

        SNPRINTF_APPEND(str_term, remaining, "%-6s",
                        field_to_str(LNF_FLD_PROT, &brec->prot));

        SNPRINTF_APPEND(str_term, remaining, "%17s:",
                        field_to_str(LNF_FLD_SRCADDR, &brec->srcaddr));
        SNPRINTF_APPEND(str_term, remaining, "%-7s",
                        field_to_str(LNF_FLD_SRCPORT, &brec->srcport));

        SNPRINTF_APPEND(str_term, remaining, "%17s:",
                        field_to_str(LNF_FLD_DSTADDR, &brec->dstaddr));
        SNPRINTF_APPEND(str_term, remaining, "%-7s",
                        field_to_str(LNF_FLD_DSTPORT, &brec->dstport));


        SNPRINTF_APPEND(str_term, remaining, "%13s",
                        field_to_str(LNF_FLD_DOCTETS, &brec->bytes));
        SNPRINTF_APPEND(str_term, remaining, "%13s",
                        field_to_str(LNF_FLD_DPKTS, &brec->pkts));
        SNPRINTF_APPEND(str_term, remaining, "%13s",
                        field_to_str(LNF_FLD_AGGR_FLOWS, &brec->flows));
        break;

    case OUTPUT_FORMAT_CSV:
        SNPRINTF_APPEND(str_term, remaining, "%s%c",
                        field_to_str(LNF_FLD_FIRST, &brec->first), CSV_SEP);
        SNPRINTF_APPEND(str_term, remaining, "%s%c",
                        field_to_str(LNF_FLD_LAST, &brec->last), CSV_SEP);

        SNPRINTF_APPEND(str_term, remaining, "%s%c",
                        field_to_str(LNF_FLD_PROT, &brec->prot), CSV_SEP);

        SNPRINTF_APPEND(str_term, remaining, "%s%c",
                        field_to_str(LNF_FLD_SRCADDR, &brec->srcaddr), CSV_SEP);
        SNPRINTF_APPEND(str_term, remaining, "%s%c",
                        field_to_str(LNF_FLD_SRCPORT, &brec->srcport), CSV_SEP);

        SNPRINTF_APPEND(str_term, remaining, "%s%c",
                        field_to_str(LNF_FLD_DSTADDR, &brec->dstaddr), CSV_SEP);
        SNPRINTF_APPEND(str_term, remaining, "%s%c",
                        field_to_str(LNF_FLD_DSTPORT, &brec->dstport), CSV_SEP);


        SNPRINTF_APPEND(str_term, remaining, "%s%c",
                        field_to_str(LNF_FLD_DOCTETS, &brec->bytes), CSV_SEP);
        SNPRINTF_APPEND(str_term, remaining, "%s%c",
                        field_to_str(LNF_FLD_DPKTS, &brec->pkts), CSV_SEP);
        SNPRINTF_APPEND(str_term, remaining, "%s",
                        field_to_str(LNF_FLD_AGGR_FLOWS, &brec->flows));
        break;

    case OUTPUT_FORMAT_UNSET:
        assert(!"illegal output format");
    default:
        assert(!"unknown output format");
    }

    return res;
}

/**
 * @brief Table of "to_str()" function specializations for fields that require
 * different handling than the general <data_type>_to_str() functions provide.
 */
static const field_to_str_t
field_to_str_func_table[] = {
    // timestamps
    [LNF_FLD_FIRST] = (field_to_str_t)timestamp_to_str,
    [LNF_FLD_LAST] = (field_to_str_t)timestamp_to_str,
    [LNF_FLD_RECEIVED] = (field_to_str_t)timestamp_to_str,

    // statistical fields
    [LNF_FLD_DOCTETS] = (field_to_str_t)volume_to_str,
    [LNF_FLD_DPKTS] = (field_to_str_t)volume_to_str,
    [LNF_FLD_OUT_BYTES] = (field_to_str_t)volume_to_str,
    [LNF_FLD_OUT_PKTS] = (field_to_str_t)volume_to_str,
    [LNF_FLD_AGGR_FLOWS] = (field_to_str_t)volume_to_str,

    // TCP flags
    [LNF_FLD_TCP_FLAGS] = (field_to_str_t)tcp_flags_to_str,

    // IP protocol
    [LNF_FLD_PROT] = (field_to_str_t)ip_proto_to_str,

    // computed: duration
    [LNF_FLD_CALC_DURATION] = (field_to_str_t)duration_to_str,
    // computed: volumetric
    [LNF_FLD_CALC_BPS] = (field_to_str_t)double_volume_to_str,
    [LNF_FLD_CALC_PPS] = (field_to_str_t)double_volume_to_str,
    [LNF_FLD_CALC_BPP] = (field_to_str_t)double_volume_to_str,

    [LNF_FLD_TERM_] = NULL,
};

/**
 * @brief Call appropriate "to_str()" function for the given libnf field.
 *
 * First, check if there is a specialized function for the given field. If not,
 * use the general (fallback) function for the data type of the field.
 *
 * @param field_id Libnf field ID.
 * @param data Pointer to the data to print.
 *
 * @return Static read-only string with text representation of the given data.
 */
static const char *
field_to_str(const int field_id, const void *const data)
{
    const int type = field_get_type(field_id);
    if (type == -1) {
        return NULL;
    }

    field_to_str_t to_str_func = field_to_str_func_table[field_id];
    if (!to_str_func) {
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
            to_str_func = (field_to_str_t)libnf_addr_to_str;
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

/**
 * @brief TODO
 *
 * @param string
 * @param string_width
 * @param space_width
 * @param last
 */
static void
print_field(const char *const string, const uint64_t string_width,
            const uint64_t space_width, const bool last)
{
    if (last) {  // last field in record, no trailing spacing or CSV separator
        puts(string);  // puts() appends a newline
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

    case OUTPUT_FORMAT_UNSET:
        assert(!"illegal output format");
    default:
        assert(!"unknown output format");
    }
}


/**
 * @brief TODO
 *
 * @param op
 * @param fields
 */
void
output_init(struct output_params op, const struct fields *const fields_param)
{
        output_params = op;
        fields = fields_param;
}

void
print_rec(const uint8_t *const data)
{
    static uint64_t col_width[LNF_FLD_TERM_];
    static bool first_rec = true;

    if (output_params.print_records != OUTPUT_ITEM_YES) {
        return;
    }

    if (first_rec) {
        first_rec = false;
        first_item = first_item ? false : (putchar('\n'), false);

        // TODO: add width table for certain fields (IP, ...)
        uint64_t off = 0;
        for (size_t i = 0; i < fields->all_cnt; ++i) {
            const char *const header_str = field_get_name(fields->all[i].id);
            const uint64_t header_str_len = strlen(header_str);
            const uint64_t field_str_len =
                strlen(field_to_str(fields->all[i].id, data + off));

            col_width[i] = MAX(header_str_len, field_str_len);
            col_width[i] += COL_WIDTH_RESERVE;
            off += fields->all[i].size;

            print_field(header_str, col_width[i],
                        PRETTY_PRINT_COL_WIDTH - COL_WIDTH_RESERVE,
                        i == (fields->all_cnt - 1));
        }

    }

    // loop through the fields in the record
    uint64_t off = 0;
    for (size_t i = 0; i < fields->all_cnt; ++i) {
        print_field(field_to_str(fields->all[i].id, data + off), col_width[i],
                    PRETTY_PRINT_COL_WIDTH - COL_WIDTH_RESERVE,
                    i == (fields->all_cnt - 1));
        off += fields->all[i].size;
    }
}

void
print_mem(lnf_mem_t *const mem, const uint64_t limit)
{
    if (output_params.print_records != OUTPUT_ITEM_YES) {
        return;
    }

    first_item = first_item ? false : (putchar('\n'), false);

    // initialize libne records
    lnf_rec_t *rec;
    int lnf_ret = lnf_rec_init(&rec);
    ERROR_IF(lnf_ret != LNF_OK, E_LNF, "lnf_rec_init()");

    /*
     * Find out maximum data type size of present fields, length of headers
     * and last present field ID.
     */
    uint64_t fld_max_size = 0; //maximum data size length in bytes
    uint64_t data_max_strlen[LNF_FLD_TERM_] = {0}; //maximum data string len
    for (size_t i = 0; i < fields->all_cnt; ++i) {
        uint64_t header_str_len = strlen(field_get_name(fields->all[i].id));

        MAX_ASSIGN(fld_max_size, fields->all[i].size);
        MAX_ASSIGN(data_max_strlen[fields->all[i].id], header_str_len);
    }

    /* Find out max data length, converted to string. */
    lnf_mem_cursor_t *cursor; //current record (line) cursor
    lnf_mem_first_c(mem, &cursor);
    uint64_t rec_cntr = 0; //aka lines counter
    while (cursor != NULL) { //row loop
        char buff[fld_max_size];

        lnf_mem_read_c(mem, cursor, rec);

        for (uint64_t i = 0; i < fields->all_cnt; ++i) { //column loop
            uint64_t data_str_len;

            //XXX: lnf_rec_fget() may return LNF_ERR_UNKFLD even if
            //field is present (e.g. if duration is zero).
            lnf_rec_fget(rec, fields->all[i].id, buff);
            data_str_len = strlen(field_to_str(fields->all[i].id, buff));
            MAX_ASSIGN(data_max_strlen[fields->all[i].id], data_str_len);
        }

        if (++rec_cntr == limit) {
            break;
        }

        lnf_mem_next_c(mem, &cursor);
    }
    rec_cntr = 0;


    /* Actual printing: header. */
    for (uint64_t i = 0; i < fields->all_cnt; ++i) { //column loop
        print_field(field_get_name(fields->all[i].id),
                data_max_strlen[fields->all[i].id],
                PRETTY_PRINT_COL_WIDTH, i == (fields->all_cnt - 1));
    }

    /* Actual printing: field data converted to string. */
    lnf_mem_first_c(mem, &cursor);
    while (cursor != NULL) { //row loop
        char buff[fld_max_size];

        lnf_mem_read_c(mem, cursor, rec);

        for (uint64_t i = 0; i < fields->all_cnt; ++i) { //column loop
            //XXX: see above lnf_rec_fget()
            lnf_rec_fget(rec, fields->all[i].id, buff);

            print_field(field_to_str(fields->all[i].id, buff),
                    data_max_strlen[fields->all[i].id],
                    PRETTY_PRINT_COL_WIDTH,
                    i == (fields->all_cnt - 1));
        }

        if (++rec_cntr == limit) {
            break;
        }

        lnf_mem_next_c(mem, &cursor);
    }

    lnf_rec_free(rec);
}

void
print_processed_summ(const struct processed_summ *const s,
                     const double duration)
{
    const double flows_per_sec = s->flows / duration;

    if (output_params.print_processed_summ != OUTPUT_ITEM_YES) {
        return;
    }

    first_item = first_item ? false : (putchar('\n'), false);

    switch (output_params.format) {
    case OUTPUT_FORMAT_PRETTY:
        printf("processed records summary:\n");

        printf("\t%s flows, ", volume_to_str(&s->flows));
        printf("%s packets, ", volume_to_str(&s->pkts));
        printf("%s bytes\n", volume_to_str(&s->bytes));

        printf("\t%f seconds, %s flows/second\n", duration,
                double_volume_to_str(&flows_per_sec));
        break;

    case OUTPUT_FORMAT_CSV:
        printf("flows%cpackets%cbytes%cseconds%cflows/second\n",
                CSV_SEP, CSV_SEP, CSV_SEP, CSV_SEP);

        printf("%s%c", volume_to_str(&s->flows), CSV_SEP);
        printf("%s%c", volume_to_str(&s->pkts), CSV_SEP);
        printf("%s%c", volume_to_str(&s->bytes), CSV_SEP);
        printf("%f%c%s\n", duration, CSV_SEP,
                double_volume_to_str(&flows_per_sec));
        break;

    case OUTPUT_FORMAT_UNSET:
        assert(!"illegal output format");
    default:
        assert(!"unknown output format");
    }
}

void
print_metadata_summ(const struct metadata_summ *const s)
{
    if (output_params.print_metadata_summ != OUTPUT_ITEM_YES) {
        return;
    }

    first_item = first_item ? false : (putchar('\n'), false);

    switch (output_params.format) {
    case OUTPUT_FORMAT_PRETTY:
        printf("metadata summary:\n");

        printf("\tflows:\n");
        printf("\t\ttotal: %s\n", volume_to_str(&s->flows));
        printf("\t\tTCP:   %s\n", volume_to_str(&s->flows_tcp));
        printf("\t\tUDP:   %s\n", volume_to_str(&s->flows_udp));
        printf("\t\tICMP:  %s\n", volume_to_str(&s->flows_icmp));
        printf("\t\tother: %s\n", volume_to_str(&s->flows_other));

        printf("\tpackets:\n");
        printf("\t\ttotal: %s\n", volume_to_str(&s->pkts));
        printf("\t\tTCP:   %s\n", volume_to_str(&s->pkts_tcp));
        printf("\t\tUDP:   %s\n", volume_to_str(&s->pkts_udp));
        printf("\t\tICMP:  %s\n", volume_to_str(&s->pkts_icmp));
        printf("\t\tother: %s\n", volume_to_str(&s->pkts_other));

        printf("\tbytes:\n");
        printf("\t\ttotal: %s\n", volume_to_str(&s->bytes));
        printf("\t\tTCP:   %s\n", volume_to_str(&s->bytes_tcp));
        printf("\t\tUDP:   %s\n", volume_to_str(&s->bytes_udp));
        printf("\t\tICMP:  %s\n", volume_to_str(&s->bytes_icmp));
        printf("\t\tother: %s\n", volume_to_str(&s->bytes_other));
        break;

    case OUTPUT_FORMAT_CSV:
        printf("field%ctotal%cTCP%cUDP%cICMP%cother\n", CSV_SEP, CSV_SEP,
               CSV_SEP, CSV_SEP, CSV_SEP);

        printf("flows%c", CSV_SEP);
        printf("%s%c", volume_to_str(&s->flows), CSV_SEP);
        printf("%s%c", volume_to_str(&s->flows_tcp), CSV_SEP);
        printf("%s%c", volume_to_str(&s->flows_udp), CSV_SEP);
        printf("%s%c", volume_to_str(&s->flows_icmp), CSV_SEP);
        printf("%s\n", volume_to_str(&s->flows_other));

        printf("packets%c", CSV_SEP);
        printf("%s%c", volume_to_str(&s->pkts), CSV_SEP);
        printf("%s%c", volume_to_str(&s->pkts_tcp), CSV_SEP);
        printf("%s%c", volume_to_str(&s->pkts_udp), CSV_SEP);
        printf("%s%c", volume_to_str(&s->pkts_icmp), CSV_SEP);
        printf("%s\n", volume_to_str(&s->pkts_other));

        printf("bytes%c", CSV_SEP);
        printf("%s%c", volume_to_str(&s->bytes), CSV_SEP);
        printf("%s%c", volume_to_str(&s->bytes_tcp), CSV_SEP);
        printf("%s%c", volume_to_str(&s->bytes_udp), CSV_SEP);
        printf("%s%c", volume_to_str(&s->bytes_icmp), CSV_SEP);
        printf("%s\n", volume_to_str(&s->bytes_other));

        break;

    case OUTPUT_FORMAT_UNSET:
        assert(!"illegal output format");
    default:
        assert(!"unknown output format");
    }
}
