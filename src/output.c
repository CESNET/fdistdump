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
#include <errno.h>              // for errno
#include <inttypes.h>           // for fixed-width integer types
#include <stdio.h>              // for printf, snprintf, putchar, puts
#include <stdlib.h>             // for free, malloc
#include <string.h>             // for strlen
#include <time.h>               // for strftime, localtime_r



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
#include "errwarn.h"            // for error/warning/info/debug messages, ...
#include "fields.h"             // for struct fields, field_get_*, ...


#define CSV_SEP ','
#define TCP_FLAG_UNSET_CHAR '.'
#define NAN_STR "NaN"
#define ABSENT_STR "absent"

#define METRIC_PREFIX_THRESHOLD 1000.0
#define BINARY_PREFIX_THRESHOLD 1024.0

#define UINT8_T_FORMAT  "%" PRIu8
#define UINT16_T_FORMAT "%" PRIu16
#define UINT32_T_FORMAT "%" PRIu32
#define UINT64_T_FORMAT "%" PRIu64
#define DOUBLE_FORMAT   "%.1f"
#define DOUBLE_PRINT_MAX 9999999999.0  // DBL_MAX is too big

#define PROTECTIVE_PADDING 4  // has to be >= 0
#define ELLIPSIS "..."  // do not use unicode, more pain than gain

#if MAX_STR_LEN < INET6_ADDRSTRLEN
#error "MAX_STR_LEN < INET6_ADDRSTRLEN"
#endif


/*
 * Data types declarations.
 */
typedef enum {
    ALIGNMENT_LEFT,
    ALIGNMENT_RIGHT,
} alignment_t;
typedef const char *(*field_to_str_t)(const void *const);


/*
 * Global variables.
 */
struct {
    field_to_str_t *field_to_str_cb;
    size_t *field_offset;

    size_t *column_width;
    alignment_t *columnt_alignment;

    size_t max_field_size;

    bool first_item;  // first item will not print '\n' before
} o_ctx;

static char global_str[MAX_STR_LEN];
static struct output_params output_params; //output parameters
static const struct fields *fields;

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
    " ",  // no unit
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
    "  ",  // no unit
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


// forward declarations
static const char *
field_to_str(const int field, const void *const data);


/*
 * Private functions.
 */
/**
 * @brief Convert a timestamp in uint64_t to string.
 *
 * The timestamp is number of milliseconds that have elapsed since 1.1.1970 UTC.
 * In other words, it is composed of Unix time (number of seconds that have
 * elapsed since 1.1.1970 UTC) and additional milliseconds elapsed since the
 * last full second.
 *
 * Pretty conversion uses "%F %T" format:
 *   %F     Equivalent to %Y-%m-%d (the ISO 8601 date format).
 *   %T     The time in 24-hour notation (%H:%M:%S).
 *
 * @param[in] ts Unix time extended to a milliseconds precision.
 *
 * @return Static read-only textual representation of the timestamp.
 */
static const char *
timestamp_to_str(const uint64_t *ts)
{
    assert(ts);

    switch (output_params.ts_conv) {
    case OUTPUT_TS_CONV_NONE:
        snprintf(global_str, sizeof (global_str), UINT64_T_FORMAT, *ts);
        break;
    case OUTPUT_TS_CONV_PRETTY: {
        const time_t calendar_seconds = *ts / 1000;         // only seconds
        const unsigned calendar_milliseconds = *ts % 1000;  // only milliseconds

        // time is expressed relative to the user's specified timezone (tzset(3)
        // was called before)
        struct tm broken_down_time;
        void *const ret = localtime_r(&calendar_seconds, &broken_down_time);
        ABORT_IF(!ret, E_INTERNAL, "localtime_r(): %s", strerror(errno));

        // convert date and time to string
        const uint64_t written = strftime(global_str, sizeof (global_str),
                                          "%F %T", &broken_down_time);
        assert(written > 0);
        // convert milliseconds to string
        snprintf(global_str + written, sizeof (global_str) - written, ".%.3d",
                 calendar_milliseconds);
        break;
    }
    case OUTPUT_TS_CONV_UNSET:
        assert(!"illegal timestamp conversion");
    default:
        assert(!"unknown timestamp conversion");
    }

    return global_str;
}
static size_t
timestamp_to_str_strlen(void)
{
    switch (output_params.ts_conv) {
    case OUTPUT_TS_CONV_NONE:
        return snprintf(NULL, 0, UINT64_T_FORMAT, UINT64_MAX);
    case OUTPUT_TS_CONV_PRETTY:
        return STRLEN_STATIC("YYYY-MM-DD HH:mm:ss.mls");
    case OUTPUT_TS_CONV_UNSET:
        assert(!"illegal timestamp conversion");
    default:
        assert(!"unknown timestamp conversion");
    }
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
    uint64_t unit_table_idx = 0;

    switch (output_params.volume_conv) {
    case OUTPUT_VOLUME_CONV_NONE:
        snprintf(global_str, sizeof (global_str), DOUBLE_FORMAT, volume_conv);
        return global_str;

    case OUTPUT_VOLUME_CONV_METRIC_PREFIX:
        while (volume_conv > METRIC_PREFIX_THRESHOLD
               && unit_table_idx + 1 < ARRAY_SIZE(decimal_unit_table))
        {
            unit_table_idx++;
            volume_conv /= METRIC_PREFIX_THRESHOLD;
        }
        snprintf(global_str, sizeof (global_str), DOUBLE_FORMAT " %s",
                 volume_conv, decimal_unit_table[unit_table_idx]);
        return global_str;

    case OUTPUT_VOLUME_CONV_BINARY_PREFIX:
        while (volume_conv > BINARY_PREFIX_THRESHOLD
               && unit_table_idx + 1 < ARRAY_SIZE(binary_unit_table))
        {
            unit_table_idx++;
            volume_conv /= BINARY_PREFIX_THRESHOLD;
        }
        snprintf(global_str, sizeof (global_str), DOUBLE_FORMAT " %s",
                 volume_conv, binary_unit_table[unit_table_idx]);
        return global_str;

    case OUTPUT_VOLUME_CONV_UNSET:
        assert(!"illegal volume conversion");
    default:
        assert(!"unknown volume conversion");
    }
}
static size_t
double_volume_to_str_strlen(void)
{
    switch (output_params.volume_conv) {
    case OUTPUT_VOLUME_CONV_NONE:
        return snprintf(NULL, 0, DOUBLE_FORMAT, DOUBLE_PRINT_MAX);
    case OUTPUT_VOLUME_CONV_METRIC_PREFIX:
        return snprintf(NULL, 0, DOUBLE_FORMAT " %s",
                        METRIC_PREFIX_THRESHOLD - 0.1, decimal_unit_table[0]);
    case OUTPUT_VOLUME_CONV_BINARY_PREFIX:
        return snprintf(NULL, 0, DOUBLE_FORMAT " %s",
                        BINARY_PREFIX_THRESHOLD - 0.1, binary_unit_table[0]);

    case OUTPUT_VOLUME_CONV_UNSET:
        assert(!"illegal volume conversion");
    default:
        assert(!"unknown volume conversion");
    }
}

static const char *
volume_to_str(const uint64_t *const volume)
{
    double volume_conv = *volume;
    uint64_t unit_table_idx = 0;

    switch (output_params.volume_conv) {
    case OUTPUT_VOLUME_CONV_NONE:
        snprintf(global_str, sizeof (global_str), UINT64_T_FORMAT, *volume);
        return global_str;

    case OUTPUT_VOLUME_CONV_METRIC_PREFIX:
        while (volume_conv > METRIC_PREFIX_THRESHOLD
               && unit_table_idx + 1 < ARRAY_SIZE(decimal_unit_table))
        {
            unit_table_idx++;
            volume_conv /= METRIC_PREFIX_THRESHOLD;
        }
        snprintf(global_str, sizeof (global_str), DOUBLE_FORMAT " %s",
                 volume_conv, decimal_unit_table[unit_table_idx]);
        return global_str;

    case OUTPUT_VOLUME_CONV_BINARY_PREFIX:
        while (volume_conv > BINARY_PREFIX_THRESHOLD
               && unit_table_idx + 1 < ARRAY_SIZE(binary_unit_table))
        {
            unit_table_idx++;
            volume_conv /= BINARY_PREFIX_THRESHOLD;
        }
        snprintf(global_str, sizeof (global_str), DOUBLE_FORMAT " %s",
                 volume_conv, binary_unit_table[unit_table_idx]);
        return global_str;

    case OUTPUT_VOLUME_CONV_UNSET:
        assert(!"illegal volume conversion");
    default:
        assert(!"unknown volume conversion");
    }
}
static size_t
volume_to_str_strlen(void)
{
    switch (output_params.volume_conv) {
    case OUTPUT_VOLUME_CONV_NONE:
        return snprintf(NULL, 0, UINT64_T_FORMAT, UINT64_MAX);

    case OUTPUT_VOLUME_CONV_METRIC_PREFIX:
        return snprintf(NULL, 0, DOUBLE_FORMAT " %s",
                        METRIC_PREFIX_THRESHOLD - 0.1, decimal_unit_table[0]);

    case OUTPUT_VOLUME_CONV_BINARY_PREFIX:
        return snprintf(NULL, 0, DOUBLE_FORMAT " %s",
                        BINARY_PREFIX_THRESHOLD - 0.1, binary_unit_table[0]);

    case OUTPUT_VOLUME_CONV_UNSET:
        assert(!"illegal volume conversion");
    default:
        assert(!"unknown volume conversion");
    }
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
static size_t
tcp_flags_to_str_strlen(void)
{
    switch (output_params.tcp_flags_conv) {
    case OUTPUT_TCP_FLAGS_CONV_NONE:
        return snprintf(NULL, 0, UINT8_T_FORMAT, UINT8_MAX);
    case OUTPUT_TCP_FLAGS_CONV_STR:
        return ARRAY_SIZE(tcp_flags_table);
    case OUTPUT_TCP_FLAGS_CONV_UNSET:
        assert(!"illegal IP protocol conversion");
    default:
        assert(!"unknown IP protocol conversion");
    }
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
static size_t
ip_proto_to_str_strlen(void)
{
    switch (output_params.ip_proto_conv) {
    case OUTPUT_IP_PROTO_CONV_NONE:
        return snprintf(NULL, 0, UINT8_T_FORMAT, UINT8_MAX);
    case OUTPUT_IP_PROTO_CONV_STR:
        return 9;  // for no good reason
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
        const unsigned msec = dur_conv % 1000;
        dur_conv /= 1000;
        const unsigned sec = dur_conv % 60;
        dur_conv /= 60;
        const unsigned min = dur_conv % 60;
        dur_conv /= 60;

        snprintf(global_str, sizeof (global_str), "%2.2" PRIu64
                 ":%2.2u:%2.2u.%3.3u", dur_conv, min, sec, msec);
        break;
    }

    case OUTPUT_DURATION_CONV_UNSET:
        assert(!"illegal duration conversion");
    default:
        assert(!"unknown duration conversion");
    }

    return global_str;
}
static size_t
duration_to_str_strlen(void)
{
    switch (output_params.duration_conv) {
    case OUTPUT_DURATION_CONV_NONE:
        return snprintf(NULL, 0, UINT64_T_FORMAT, UINT64_MAX);
    case OUTPUT_DURATION_CONV_STR:
        return STRLEN_STATIC("00:00:00.000");
    case OUTPUT_DURATION_CONV_UNSET:
        assert(!"illegal duration conversion");
    default:
        assert(!"unknown duration conversion");
    }
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
        snprintf(global_str, sizeof (global_str), UINT32_T_FORMAT ":"
                 UINT32_T_FORMAT ":" UINT32_T_FORMAT ":" UINT32_T_FORMAT,
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
        ABORT_IF(!ret, E_INTERNAL, "inet_ntop()");
        break;
    }
    case OUTPUT_IP_ADDR_CONV_UNSET:
        assert(!"illegal IP address conversion");
    default:
        assert(!"unknown IP address conversion");
    }

    return global_str;
}
static size_t
libnf_addr_to_str_strlen(void)
{
    switch (output_params.ip_addr_conv) {
    case OUTPUT_IP_ADDR_CONV_NONE:
        return snprintf(NULL, 0, UINT32_T_FORMAT ":" UINT32_T_FORMAT ":"
                        UINT32_T_FORMAT ":" UINT32_T_FORMAT, UINT32_MAX,
                        UINT32_MAX, UINT32_MAX, UINT32_MAX);
    case OUTPUT_IP_ADDR_CONV_STR:
        return STRLEN_STATIC("255.255.255.255");  // IPv6 will be ellipsized
    case OUTPUT_IP_ADDR_CONV_UNSET:
        assert(!"illegal IP address conversion");
    default:
        assert(!"unknown IP address conversion");
    }
}

/**
 * @brief Convert libnf MAC address to a string.
 *
 * @param[in] mac Binary MAC address representation.
 *
 * @return Static read-only string MAC address representation.
 */
static const char *
libnf_mac_to_str(const lnf_mac_t *const mac)
{
    snprintf(global_str, 18, "%02x:%02x:%02x:%02x:%02x:%02x", mac->data[0],
             mac->data[1], mac->data[2], mac->data[3], mac->data[4],
             mac->data[5]);
    return global_str;
}
static size_t
libnf_mac_to_str_strlen(void)
{
    return STRLEN_STATIC("00:00:00:00:00:00");
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
    snprintf(global_str, sizeof (global_str), UINT8_T_FORMAT, *u8);
    return global_str;
}
static size_t
uint8_t_to_str_strlen(void)
{
    return snprintf(NULL, 0, UINT8_T_FORMAT, UINT8_MAX);
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
    snprintf(global_str, sizeof (global_str), UINT16_T_FORMAT, *u16);
    return global_str;
}
static size_t
uint16_t_to_str_strlen(void)
{
    return snprintf(NULL, 0, UINT16_T_FORMAT, UINT16_MAX);
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
    snprintf(global_str, sizeof (global_str), UINT32_T_FORMAT, *u32);
    return global_str;
}
static size_t
uint32_t_to_str_strlen(void)
{
    return snprintf(NULL, 0, UINT32_T_FORMAT, UINT32_MAX);
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
    snprintf(global_str, sizeof (global_str), UINT64_T_FORMAT, *u64);
    return global_str;
}
static size_t
uint64_t_to_str_strlen(void)
{
    return snprintf(NULL, 0, UINT64_T_FORMAT, UINT64_MAX);
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
    snprintf(global_str, sizeof (global_str), DOUBLE_FORMAT, *d);
    return global_str;
}
static size_t
double_to_str_strlen(void)
{
    return snprintf(NULL, 0, DOUBLE_FORMAT, DOUBLE_PRINT_MAX);
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
static size_t
string_to_str_strlen(void)
{
    return 10;  // for no good reason
}

/**
 * @brief Convert the lnf_brec1_t structure to the textual representation.
 *
 * @param[in] brec lnf basic record 1.
 *
 * @return Textual representation of the record (in static memory).
 */
static const char *
libnf_brec_to_str(const lnf_brec1_t *brec)
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
static size_t
libnf_brec_to_str_strlen(void)
{
    lnf_brec1_t brec = { 0 };
    return strlen(libnf_brec_to_str(&brec));
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
 * @brief Retrun pointer to appropriate "to_str()" function for the given libnf
 *        field.
 *
 * First, check if there is a specialized function for the given field. If not,
 * use the general (fallback) function for the data type of the field.
 *
 * @param field_id Libnf field ID.
 *
 * @return Pointer to the field_to_str_t function.
 */
static field_to_str_t
get_field_to_str_callback(const int field_id)
{
    const int type = field_get_type(field_id);

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
            to_str_func = (field_to_str_t)libnf_mac_to_str;
            break;
        case LNF_BASIC_RECORD1:
            to_str_func = (field_to_str_t)libnf_brec_to_str;
            break;
        case LNF_STRING:
            to_str_func = (field_to_str_t)string_to_str;
            break;
        case LNF_NONE:
        case LNF_MPLS:
        case LNF_ACL:
            assert(!"unimplemented LNF data type");

        default:
            assert(!"unknown LNF data type");
        }
    }

    return to_str_func;
}

/**
 * @brief Convert the libnf field data to string.
 *
 * Contains just a call to the function returned by get_field_to_str_callback().
 *
 * @param field_id Libnf field ID.
 * @param data Pointer to the data to print.
 *
 * @return Static read-only string with text representation of the given data.
 */
static const char *
field_to_str(const int field_id, const void *const data)
{

    return get_field_to_str_callback(field_id)(data);
}

/**
 * @brief TODO
 *
 * Field string is left aligned. If it has fewer characters than the string
 * width, it will be padded with spaces on the right. The last field is printed
 * without any alignment or padding.
 *
 * |  col_width   | PROTECTIVE_PADDING |
 * 192.168.1.1~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * @param string
 * @param col_width
 * @param space_width
 * @param last
 */
static void
print_field(const char *const string, const int col_width,
            const alignment_t alignment, const bool last)
{
    assert(string && col_width > 0);

    switch (output_params.format) {
    case OUTPUT_FORMAT_PRETTY:
    {
        const size_t string_len = strlen(string);
        const ssize_t col_width_remainder = col_width - string_len;
        size_t padding_width;

        char aligned_and_padded_string[string_len + col_width_remainder + PROTECTIVE_PADDING];
        char *pos = aligned_and_padded_string;

        if (col_width_remainder < 0 && output_params.ellipsize) {
            // not enough space: ellipsize and print the whole padding
            const size_t copy_len = col_width - STRLEN_STATIC(ELLIPSIS);
            memcpy(pos, string, copy_len);
            pos += copy_len;
            memcpy(pos, ELLIPSIS, STRLEN_STATIC(ELLIPSIS));
            pos += STRLEN_STATIC(ELLIPSIS);
            padding_width = PROTECTIVE_PADDING;
        } else {
            // enough space or ellipsization disabled: print the whole string
            // right alignment means spaces on the left
            if (alignment == ALIGNMENT_RIGHT && col_width_remainder > 0) {
                memset(pos, ' ', col_width_remainder);
                pos += col_width_remainder;
            }

            // the string
            memcpy(pos, string, string_len);
            pos += string_len;

            // left alignment means spaces on the right + protective padding
            if (col_width_remainder >= 0) {
                if (alignment == ALIGNMENT_LEFT) {
                    padding_width = col_width_remainder + PROTECTIVE_PADDING;
                } else {
                    padding_width = PROTECTIVE_PADDING;
                }
            } else if (col_width_remainder <= PROTECTIVE_PADDING) {
                padding_width = 1;
            } else {
                padding_width = PROTECTIVE_PADDING - (-col_width_remainder);
            }
        }

        // add trailing spaces if the field is not last one
        if (!last) {
            memset(pos, ' ', padding_width);
            pos += padding_width;
        }

        // handy to debug output
        //*pos = '|';
        //pos++;

        *pos = '\0';  // terminate
        fputs(aligned_and_padded_string, stdout);
        break;
    }

    case OUTPUT_FORMAT_CSV:
        fputs(string, stdout);
        if (!last) {  // no trailing CSV separator for the last field
            putchar(CSV_SEP);
        }
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
 * @param to_str_func
 *
 * @return
 */
static size_t
get_column_width_estimate(field_to_str_t to_str_func)
{
    if (to_str_func == (field_to_str_t)timestamp_to_str) {
        return timestamp_to_str_strlen();
    } else if (to_str_func == (field_to_str_t)double_volume_to_str) {
        return double_volume_to_str_strlen();
    } else if (to_str_func == (field_to_str_t)volume_to_str) {
        return volume_to_str_strlen();
    } else if (to_str_func == (field_to_str_t)tcp_flags_to_str) {
        return tcp_flags_to_str_strlen();
    } else if (to_str_func == (field_to_str_t)ip_proto_to_str) {
        return ip_proto_to_str_strlen();
    } else if (to_str_func == (field_to_str_t)duration_to_str) {
        return duration_to_str_strlen();
    } else if (to_str_func == (field_to_str_t)libnf_addr_to_str) {
        return libnf_addr_to_str_strlen();
    } else if (to_str_func == (field_to_str_t)libnf_mac_to_str) {
        return libnf_mac_to_str_strlen();
    } else if (to_str_func == (field_to_str_t)uint8_t_to_str) {
        return uint8_t_to_str_strlen();
    } else if (to_str_func == (field_to_str_t)uint16_t_to_str) {
        return uint16_t_to_str_strlen();
    } else if (to_str_func == (field_to_str_t)uint32_t_to_str) {
        return uint32_t_to_str_strlen();
    } else if (to_str_func == (field_to_str_t)uint64_t_to_str) {
        return uint64_t_to_str_strlen();
    } else if (to_str_func == (field_to_str_t)double_to_str) {
        return double_to_str_strlen();
    } else if (to_str_func == (field_to_str_t)string_to_str) {
        return string_to_str_strlen();
    } else if (to_str_func == (field_to_str_t)libnf_brec_to_str) {
        return libnf_brec_to_str_strlen();
    } else {
        assert(!"unknown to_str function");
    }
}

/**
 * @brief TODO
 *
 * @param to_str_func
 *
 * @return
 */
static alignment_t
get_column_alignment(field_to_str_t to_str_func)
{
    if (to_str_func == (field_to_str_t)timestamp_to_str) {
        return ALIGNMENT_LEFT;
    } else if (to_str_func == (field_to_str_t)double_volume_to_str) {
        return ALIGNMENT_RIGHT;
    } else if (to_str_func == (field_to_str_t)volume_to_str) {
        return ALIGNMENT_RIGHT;
    } else if (to_str_func == (field_to_str_t)tcp_flags_to_str) {
        return ALIGNMENT_LEFT;
    } else if (to_str_func == (field_to_str_t)ip_proto_to_str) {
        return ALIGNMENT_LEFT;
    } else if (to_str_func == (field_to_str_t)duration_to_str) {
        return ALIGNMENT_LEFT;
    } else if (to_str_func == (field_to_str_t)libnf_addr_to_str) {
        return ALIGNMENT_LEFT;
    } else if (to_str_func == (field_to_str_t)libnf_mac_to_str) {
        return ALIGNMENT_LEFT;
    } else if (to_str_func == (field_to_str_t)uint8_t_to_str) {
        return ALIGNMENT_RIGHT;
    } else if (to_str_func == (field_to_str_t)uint16_t_to_str) {
        return ALIGNMENT_RIGHT;
    } else if (to_str_func == (field_to_str_t)uint32_t_to_str) {
        return ALIGNMENT_RIGHT;
    } else if (to_str_func == (field_to_str_t)uint64_t_to_str) {
        return ALIGNMENT_RIGHT;
    } else if (to_str_func == (field_to_str_t)double_to_str) {
        return ALIGNMENT_RIGHT;
    } else if (to_str_func == (field_to_str_t)string_to_str) {
        return ALIGNMENT_LEFT;
    } else if (to_str_func == (field_to_str_t)libnf_brec_to_str) {
        return ALIGNMENT_LEFT;
    } else {
        assert(!"unknown to_str function");
    }
}

/**
 * @brief TODO
 *
 * @param idx
 * @param lnf_rec
 * @param buff
 *
 * @return
 */
static const char *
get_field_str(const size_t idx, lnf_rec_t *const lnf_rec, char *const buff)
{
    const int lnf_ret = lnf_rec_fget(lnf_rec, fields->all[idx].id, buff);
    switch (lnf_ret) {
    case LNF_OK:
        return o_ctx.field_to_str_cb[idx](buff);
    case LNF_ERR_NAN:
        return NAN_STR;
    case LNF_ERR_NOTSET:
        return ABSENT_STR;
    default:
        assert(!"invalid return code from lnf_rec_fget()");
    }
}

/**
 * @brief TODO
 *
 * @param lnf_mem
 * @param rec_limit
 */
static void
set_column_widths_exactly(lnf_mem_t *const lnf_mem, const size_t rec_limit)
{
    // initialize a libnf record
    lnf_rec_t *lnf_rec;
    int lnf_ret = lnf_rec_init(&lnf_rec);
    ABORT_IF(lnf_ret != LNF_OK, E_LNF, "lnf_rec_init()");

    // initialize the cursor to point to the first record in the memory
    lnf_mem_cursor_t *cursor;
    lnf_ret = lnf_mem_first_c(lnf_mem, &cursor);
    assert((cursor && lnf_ret == LNF_OK) || (!cursor && lnf_ret == LNF_EOF));

    // loop through all records
    uint64_t rec_cntr = 0;  // aka lines counter
    char buff[o_ctx.max_field_size];
    while (cursor && rec_limit > rec_cntr++) {
        lnf_ret = lnf_mem_read_c(lnf_mem, cursor, lnf_rec);
        assert(lnf_ret == LNF_OK);

        // loop through all fields in the record
        for (size_t i = 0; i < fields->all_cnt; ++i) {
            const size_t field_str_len = strlen(get_field_str(i, lnf_rec, buff));
            MAX_ASSIGN(o_ctx.column_width[i], field_str_len);
        }

        lnf_ret = lnf_mem_next_c(lnf_mem, &cursor);
        assert((cursor && lnf_ret == LNF_OK) || (!cursor && lnf_ret == LNF_EOF));
    }

    lnf_rec_free(lnf_rec);
}

/**
 * @brief TODO
 */
static void
print_names_enriched_aggr(void)
{
    assert(fields->aggr_keys_cnt > 0);

    char buff[256];
    const bool have_sort_key = (fields->sort_key.field != NULL);
    bool sort_key_is_one_of_aggregation_keys = false;
    size_t fields_cntr = 0;

    // loop through aggregation keys
    for (size_t i = 0; i < fields->aggr_keys_cnt; ++i) {
        const int field_id = fields->aggr_keys[i].field->id;
        const char *const field_name = field_get_name(field_id);

        if (have_sort_key
                && fields->sort_key.field == fields->aggr_keys[i].field) {
            // this aggregation key is also the sort key
            snprintf(buff, sizeof (buff), "%s (%s){%s}", field_name, "key",
                    libnf_sort_dir_to_str(fields->sort_key.direction));
            sort_key_is_one_of_aggregation_keys = true;
        } else {
            snprintf(buff, sizeof (buff), "%s (%s)", field_name, "key");
        }

        MAX_ASSIGN(o_ctx.column_width[i], strlen(buff));

        const bool last_column = (i == (fields->all_cnt - 1));
        print_field(buff, o_ctx.column_width[i], o_ctx.columnt_alignment[i],
                    last_column);
        fields_cntr++;
    }

    // print sort key name if there is a sort key and is not one of aggr. keys
    if (have_sort_key && !sort_key_is_one_of_aggregation_keys) {
        snprintf(buff, sizeof (buff), "%s {%s}",
                field_get_name(fields->sort_key.field->id),
                libnf_sort_dir_to_str(fields->sort_key.direction));

        MAX_ASSIGN(o_ctx.column_width[fields_cntr], strlen(buff));

        const bool last_column = (fields_cntr == (fields->all_cnt - 1));
        print_field(buff, o_ctx.column_width[fields_cntr],
                    o_ctx.columnt_alignment[fields_cntr], last_column);
        fields_cntr++;
    }

    // loop through all output fields
    for (size_t i = 0; i < fields->output_fields_cnt; ++i) {
        const int id = fields->output_fields[i].field->id;
        const char *func;
        if (IN_RANGE_INCL(id, LNF_FLD_CALC_DURATION, LNF_FLD_CALC_BPP)) {
            func = "calc";
        } else {
            func = libnf_aggr_func_to_str(fields->output_fields[i].aggr_func);
        }
        snprintf(buff, sizeof (buff), "%s [%s]", field_get_name(id), func);

        MAX_ASSIGN(o_ctx.column_width[fields_cntr], strlen(buff));

        const bool last_column = (fields_cntr  == (fields->all_cnt - 1));
        print_field(buff, o_ctx.column_width[fields_cntr],
                    o_ctx.columnt_alignment[fields_cntr], last_column);
        fields_cntr++;
    }
}

/**
 * @brief TODO
 */
static void
print_names_enriched_sort(void)
{
    assert(fields->aggr_keys_cnt == 0 && fields->sort_key.field);

    size_t fields_cntr = 0;

    // print sort key name
    char buff[256];
    snprintf(buff, sizeof (buff), "%s {%s}",
            field_get_name(fields->sort_key.field->id),
            libnf_sort_dir_to_str(fields->sort_key.direction));

    MAX_ASSIGN(o_ctx.column_width[fields_cntr], strlen(buff));

    bool last_column = (fields_cntr == (fields->all_cnt - 1));
    print_field(buff, o_ctx.column_width[fields_cntr],
                o_ctx.columnt_alignment[fields_cntr], last_column);
    fields_cntr++;

    // loop through all output fields
    for (size_t i = 0; i < fields->output_fields_cnt; ++i) {
        const char *const field_name =
            field_get_name(fields->output_fields[i].field->id);

        MAX_ASSIGN(o_ctx.column_width[fields_cntr], strlen(field_name));

        last_column = (fields_cntr  == (fields->all_cnt - 1));
        print_field(field_name, o_ctx.column_width[fields_cntr],
                    o_ctx.columnt_alignment[fields_cntr], last_column);
        fields_cntr++;
    }
}

/**
 * @brief TODO
 */
static void
print_names_only()
{
    for (size_t i = 0; i < fields->all_cnt; ++i) {
        const bool last_column = (i == (fields->all_cnt - 1));
        const char *const name_str = field_get_name(fields->all[i].id);
        const size_t name_str_len = strlen(name_str);

        MAX_ASSIGN(o_ctx.column_width[i], name_str_len);
        print_field(name_str, o_ctx.column_width[i], o_ctx.columnt_alignment[i],
                    last_column);
    }
}


/*
 * Public functions.
 */
/**
 * @brief TODO
 *
 * @param op
 * @param fields
 */
void
output_init(struct output_params op, const struct fields *const fields_param)
{
    assert(op.format != OUTPUT_FORMAT_UNSET && fields_param);

    output_params = op;
    fields = fields_param;

    o_ctx.field_to_str_cb = malloc(
            fields->all_cnt * sizeof (*o_ctx.field_to_str_cb));
    o_ctx.field_offset = malloc(fields->all_cnt * sizeof (*o_ctx.field_offset));

    o_ctx.column_width = malloc(fields->all_cnt * sizeof (*o_ctx.column_width));
    o_ctx.columnt_alignment = malloc(
            fields->all_cnt * sizeof (*o_ctx.columnt_alignment));
    ABORT_IF(!o_ctx.field_to_str_cb || !o_ctx.field_offset
             || !o_ctx.column_width || !o_ctx.columnt_alignment, E_MEM,
             "output context memory allocation failed");

    size_t off = 0;
    for (size_t i = 0; i < fields->all_cnt; ++i) {
        o_ctx.field_to_str_cb[i] = get_field_to_str_callback(fields->all[i].id);
        o_ctx.field_offset[i] = off;
        off += fields->all[i].size;
        MAX_ASSIGN(o_ctx.max_field_size, fields->all[i].size);

        o_ctx.column_width[i] =
            get_column_width_estimate(o_ctx.field_to_str_cb[i]);
        o_ctx.columnt_alignment[i] =
            get_column_alignment(o_ctx.field_to_str_cb[i]);
    }

    o_ctx.first_item = true;
}

/**
 * @brief TODO
 */
void
output_free(void)
{
    assert(o_ctx.field_to_str_cb);

    free(o_ctx.field_to_str_cb);
    free(o_ctx.field_offset);
    free(o_ctx.column_width);
    free(o_ctx.columnt_alignment);
}

/**
 * @brief TODO
 */
void
print_stream_names()
{
    assert(o_ctx.field_to_str_cb);

    if (output_params.print_records != OUTPUT_ITEM_YES) {
        return;
    }

    o_ctx.first_item = o_ctx.first_item ? false : (putchar('\n'), false);

    print_names_only();
    putchar('\n');
}

/**
 * @brief TODO
 *
 * @param data
 */
void
print_stream_next(const uint8_t *const data)
{
    assert(o_ctx.field_to_str_cb);

    if (output_params.print_records != OUTPUT_ITEM_YES) {
        return;
    }

    // loop through the fields in the record
    for (size_t i = 0; i < fields->all_cnt; ++i) {
        const bool last_column = (i == (fields->all_cnt - 1));

        print_field(o_ctx.field_to_str_cb[i](data + o_ctx.field_offset[i]),
                    o_ctx.column_width[i], o_ctx.columnt_alignment[i],
                    last_column);
    }
    putchar('\n');
}

/**
 * @brief TODO
 *
 * @param lnf_mem
 * @param rec_limit
 */
void
print_batch(lnf_mem_t *const lnf_mem, uint64_t rec_limit)
{
    assert(o_ctx.field_to_str_cb);

    if (output_params.print_records != OUTPUT_ITEM_YES) {
        return;
    }

    if (rec_limit == 0) {
        rec_limit = UINT64_MAX;
    }

    o_ctx.first_item = o_ctx.first_item ? false : (putchar('\n'), false);

    if (!output_params.ellipsize) {
        // loop through all records and calculate with of all columns
        set_column_widths_exactly(lnf_mem, rec_limit);
    }


    /*
     * Print the header.
     */
    if (output_params.rich_header && fields->aggr_keys_cnt > 0) {
        // at least one aggregation key and possibly a sort key
        print_names_enriched_aggr();
    } else if (output_params.rich_header && fields->sort_key.field) {
        // no aggregation keys, only a sort key
        print_names_enriched_sort();
    } else {
        // no aggregation nor sorting
        print_names_only();
    }
    putchar('\n');  // break line after the header


    /*
     * Print the fields.
     */
    // initialize a libnf record
    lnf_rec_t *lnf_rec;
    int lnf_ret = lnf_rec_init(&lnf_rec);
    ABORT_IF(lnf_ret != LNF_OK, E_LNF, "lnf_rec_init()");

    // initialize the cursor to point to the first record in the memory
    lnf_mem_cursor_t *cursor;
    lnf_ret = lnf_mem_first_c(lnf_mem, &cursor);
    assert((cursor && lnf_ret == LNF_OK) || (!cursor && lnf_ret == LNF_EOF));

    // loop through all records
    uint64_t rec_cntr = 0;  // aka lines counter
    char buff[o_ctx.max_field_size];
    while (cursor && rec_limit > rec_cntr++) {
        lnf_ret = lnf_mem_read_c(lnf_mem, cursor, lnf_rec);
        assert(lnf_ret == LNF_OK);

        // loop through all fields in the record
        for (size_t i = 0; i < fields->all_cnt; ++i) {
            const char *const field_str = get_field_str(i, lnf_rec, buff);
            const bool last_column = (i == (fields->all_cnt - 1));

            print_field(field_str, o_ctx.column_width[i],
                        o_ctx.columnt_alignment[i], last_column);
        }
        putchar('\n');

        lnf_ret = lnf_mem_next_c(lnf_mem, &cursor);
        assert((cursor && lnf_ret == LNF_OK) || (!cursor && lnf_ret == LNF_EOF));
    }

    lnf_rec_free(lnf_rec);
}

/**
 * @brief TODO
 *
 * @param s
 * @param duration
 */
void
print_processed_summ(const struct processed_summ *const s,
                     const double duration)
{
    assert(o_ctx.field_to_str_cb);

    const double flows_per_sec = s->flows / duration;

    if (output_params.print_processed_summ != OUTPUT_ITEM_YES) {
        return;
    }

    o_ctx.first_item = o_ctx.first_item ? false : (putchar('\n'), false);

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

/**
 * @brief TODO
 *
 * @param s
 */
void
print_metadata_summ(const struct metadata_summ *const s)
{
    assert(o_ctx.field_to_str_cb);

    if (output_params.print_metadata_summ != OUTPUT_ITEM_YES) {
        return;
    }

    o_ctx.first_item = o_ctx.first_item ? false : (putchar('\n'), false);

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
