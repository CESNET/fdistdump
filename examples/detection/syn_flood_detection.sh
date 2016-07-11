#!/bin/bash

# Author = "Pavel Krobot <Pavel.Krobot@cesnet.cz>"

#
# FDistDump syn flood DoS attack detection
#

# Copyright (C) 2015 CESNET
#
# LICENSE TERMS
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the
#    distribution.
# 3. Neither the name of the Company nor the names of its contributors
#    may be used to endorse or promote products derived from this
#    software without specific prior written permission.
#
# ALTERNATIVELY, provided that this notice is retained in full, this
# product may be distributed under the terms of the GNU General Public
# License (GPL) version 2 or later, in which case the provisions
# of the GPL apply INSTEAD OF those given above.
#
# This software is provided ``as is'', and any express or implied
# warranties, including, but not limited to, the implied warranties of
# merchantability and fitness for a particular purpose are disclaimed.
# In no event shall the company or contributors be liable for any
# direct, indirect, incidental, special, exemplary, or consequential
# damages (including, but not limited to, procurement of substitute
# goods or services; loss of use, data, or profits; or business
# interruption) however caused and on any theory of liability, whether
# in contract, strict liability, or tort (including negligence or
# otherwise) arising in any way out of the use of this software, even
# if advised of the possibility of such damage.

################################################################################
## Initialization
__W_DIR=$(dirname $0)

# Source configuration
__COMMON_CONFIG="${__W_DIR}/detection_common.conf"
. $__COMMON_CONFIG

__DETECTION_LOG="${__LOG_PATH}/detection_SYN_FLOOD.log"

################################################################################
## Detection set up and defaults

# Detection time windown size (in seconds):
__TIME_WINDOW_SIZE=600

# number of top-N records
__TOP_N_SIZE=10

# minimal / maximal limits for detection of potentional syn flood attack
# minimal threshold for number of flows in TOP-N
__MIN_FLOW=100000
# per-flow packet count ratio limit (max)
__PKT_RATIO=2

################################################################################
## Print usage of this script
print_usage() {
    echo "SYN flood detection (FDistDump)"
    echo "==================================="
    echo "Script for detection of SYN flood dos attacks in stored data. Detection"
    echo "is triggered for small time intervals (time windows) in selected data sets."
    echo "For every time window there is a report printed to \"${__DETECTION_LOG}\"."
    echo "Every report contains ${__TOP_N_SIZE} most significant attacks."
    echo ""
    echo "Don't forget to setup all necessary variables in ${__COMMON_CONFIG}."
    echo "Detection parameters could be adjusted in this script."
    echo ""
    echo "Usage: ./syn_flood_detection.sh [start] [end] [window_size] [data]"
    echo ""
    echo "start: Start of the analyzed time interval in the as same format as in"
    echo "  parameter \"--date (-d)\" of \"date\" tool."
    echo "  (default is <actual_time> - <window_size>)"
    echo "  Example: \"12/20/2015 01:00\""
    echo ""
    echo "end: End of the analyzed time interval in the as same format as in"
    echo "  parameter \"--date (-d)\" of \"date\" tool."
    echo "  (default is: <actual_time> + <time-window_size> if <start> is set,"
    echo "  <actual_time> otherwise)"
    echo "  Example: \"12/20/2015 02:00\""
    echo ""
    echo "window_size: Detection window size. Analyzed time interval is split to smaller"
    echo "  interval of <window_size> size. Size of last window is adjusted to be at least"
    echo "  <window_size> long (default is $__TIME_WINDOW_SIZE)"
    echo ""
    echo "data: Path to data files (default is $__DATA)"
}

################################################################################
## Parameters check and detection time interval set up
if [ -z "$1" ]; then
    __END_TIME=$(date +"%s")
    __r1=$?
    __START_TIME=$(($__END_TIME-$__TIME_WINDOW_SIZE))
else
    __START_TIME=$(date --date="$1" +"%s")
    __r1=$?
fi

if [ -z "$2" ] && [ -z "$__END_TIME" ]; then
    __END_TIME=$(($__START_TIME+$__TIME_WINDOW_SIZE))
    __r2=$?
else
    __END_TIME=$(date --date="$2" +"%s")
    __r2=$?
fi

_is_num_re='^[0-9]+$'
if [[ "$__r1" != "0" ]] || ! [[ $__START_TIME =~ $_is_num_re ]]; then
    echo "Error: Wrong 1st argument."
    echo
    print_usage
    exit 1
fi

if [[ "$__r2" != "0" ]] || ! [[ $__END_TIME =~ $_is_num_re ]]; then
    echo "Error: Wrong 2nd argument."
    echo
    print_usage
    exit 1
fi

if [ -z "$3" ]; then
    echo "Warning: Using default detection time-window size: ${__TIME_WINDOW_SIZE} seconds."
else
    __DATA="$3"
fi

if [ -z "$4" ]; then
    echo "Warning: Path to data was not set, using default path: $__DATA"
else
    __DATA="$3"
fi

__diff=$(($__END_TIME - $__START_TIME))
if (( "$__diff" < "$__TIME_WINDOW_SIZE" )); then
    __END_TIME=$(($__START_TIME + $__TIME_WINDOW_SIZE))
    __diff="$__TIME_WINDOW_SIZE"
    echo "Warning: Time interval is too small. Minimal timewindow is $__TIME_WINDOW_SIZE seconds."
    echo "         Using time interval $1 - "$(date -d @"$__END_TIME" +"%m/%d/%Y %H:%M")"."
fi

__act_start="$__START_TIME"
__act_end="$__act_start"
echo "Starting detection of SYN flood attacks:" >"$__DETECTION_LOG" 2>&1
echo "- for port (service): $__PORT_OF_SERVICE" >>"$__DETECTION_LOG" 2>&1
_s=$(date -d @"$__START_TIME" +"%m/%d/%Y %H:%M")
_e=$(date -d @"$__END_TIME" +"%m/%d/%Y %H:%M")
echo "- in time interval: $_s to $_e" >>"$__DETECTION_LOG" 2>&1
while (("$__diff" > 0)); do

    # To flush stdout ...
    echo "" | tee -a "$__DETECTION_LOG" > /dev/null

    __act_start="$__act_end"
    if (( "$__diff" - "$__TIME_WINDOW_SIZE" >= "$__TIME_WINDOW_SIZE")); then
        __act_end=$(($__act_start + $__TIME_WINDOW_SIZE))
        __diff=$(($__diff - $__TIME_WINDOW_SIZE))
    else
        __act_end=$(($__act_start + $__diff))
        __diff=0
    fi
    _s=$(date -d @"$__act_start" +"%m/%d/%Y %H:%M")
    _e=$(date -d @"$__act_end" +"%m/%d/%Y %H:%M")

    __TIME_SELECTOR="${_s}#${_e}"

    echo "################################################################################" >>"$__DETECTION_LOG" 2>&1
    echo "# $__TIME_SELECTOR" >>"$__DETECTION_LOG" 2>&1
    ############################################################################
    # Phase 1: Search for potentional SYN flod attackers

    # Prepare FDistDump query command
    __OUTPUT_FORMAT="--fields=bytes,pkts,flows --output-format=csv --output-addr-conv=str --progress-bar=none"
    cmd="mpiexec --hostfile $__HOSTFILE --preload-binary $__FDD_BIN $__OUTPUT_FORMAT -s srcip,dstip -l $__TOP_N_SIZE -o flows -f \"proto TCP\" -t \"$__TIME_SELECTOR\" $__DATA"

    unset __POT_ATTACKERS
    while read -r line; do
        # columns: bytes,pkts,flows,srcip,dstip

        # Test & store potentional attackers and victims
        # IP addresses with flow count above and packet ratio under thresholds
        _attacker=$(echo $line | awk -v min_f="$__MIN_FLOW" -v p_rat="$__PKT_RATIO" 'BEGIN {FS=","} {if ( (int($3)) >= min_f && (int($2)) < (p_rat*(int($3))) ) print "( src ip "$4" and dst ip "$5" )"}')

        if [[ "$_attacker" != "" ]]; then
            # Store ip addresses of the potentional attacker and dvictim (in fdistdump filter format)
            __POT_ATTACKERS+=("$_attacker")
        fi
    done < <(eval $cmd)

    ############################################################################
    # Phase 2: Check SYN / SYN+ACK ratio of potentional attacks
    for attckr in "${__POT_ATTACKERS[@]}"; do
        # Prepare FDistDump query command for SYN packet count
        __OUTPUT_FORMAT="--fields=bytes,pkts,flows --output-format=csv --output-addr-conv=str --progress-bar=none"
        cmd="mpiexec --hostfile $__HOSTFILE --preload-binary $__FDD_BIN $__OUTPUT_FORMAT -s srcip,dstip -l 2 -o pkts -f \"proto TCP && (flags S && not flags AF) && ${attckr}\" -t \"$__TIME_SELECTOR\" $__DATA"

        # Get SYN packet count
        _syn_packets=$(eval $cmd | awk -F "," 'NR==2 {print int($2);exit}')
        if [[ "$_syn_packets" == "" ]]; then
            _syn_packets=0
        fi

        # Prepare FDistDump query command for SYN+ACK packet count
        cmd="mpiexec --hostfile $__HOSTFILE --preload-binary $__FDD_BIN $__OUTPUT_FORMAT -s srcip,dstip -l 2 -o pkts -f \"proto TCP && (flags SA && not flags F) && ${attckr}\" -t \"$__TIME_SELECTOR\" $__DATA"

        # Get SYN+ACK packet count
        _ack_packets=$(eval $cmd | awk -F "," 'NR==2 {print int($2);exit}')
        if [[ "$_ack_packets" == "" ]]; then
            _ack_packets=0
        fi

        # Compare SYN / SYN+ACK packet count and report detected SYN flood events
        if (("$_syn_packets" > "2*$_ack_packets")); then
            _src_ip=$(echo $attckr | awk '{print $4}')
            _dst_ip=$(echo $attckr | awk '{print $8}')
            echo "--------------------------------------------------------------------------------" >>"$__DETECTION_LOG" 2>&1
            echo "Attack Source: ${_src_ip}" >>"$__DETECTION_LOG" 2>&1
            echo "Attack Target: ${_dst_ip}" >>"$__DETECTION_LOG" 2>&1
            echo "SYN packet count: $_syn_packets" >>"$__DETECTION_LOG" 2>&1
            echo "ACK packet count: $_ack_packets" >>"$__DETECTION_LOG" 2>&1
            echo "Traffic Sample:" >>"$__DETECTION_LOG" 2>&1
            _output_filter="proto TCP && ${attckr}"
            _cmd="mpiexec --hostfile $__HOSTFILE --preload-binary $__FDD_BIN --fields=first,last,duration,srcip,dstip,bytes,pkts,flows,tcpflags --progress-bar=none -l 100 -f \"$_output_filter\" -t \"$__TIME_SELECTOR\" $__DATA"
            echo "" >>"$__DETECTION_LOG" 2>&1
            echo "$_cmd" >>"$__DETECTION_LOG" 2>&1
            echo "" >>"$__DETECTION_LOG" 2>&1
            eval "$_cmd" >>"$__DETECTION_LOG" 2>&1
        fi
    done

done
