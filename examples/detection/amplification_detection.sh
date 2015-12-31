#!/bin/bash

# Author = "Pavel Krobot <Pavel.Krobot@cesnet.cz>"

#
# FDistDump amplification attack detection
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

__DETECTION_LOG="${__LOG_PATH}/detection_AMPL_ATCK.log"

################################################################################
## Detection set up and defaults

# Port of service could be changed through script arguments
__PORT_OF_SERVICE=53

# Detection time windown size (in seconds):
__TIME_WINDOW_SIZE=600

# number of top-N records
__TOP_N_SIZE=5

# minimal / maximal limits for detection of potentional amplification attack
# traffic - PER-RECORD limits
__MIN_BYTES=3000
__MIN_BPP=1200
__MIN_PACKETS=1
__MAX_PACKETS=1000

# minimal threshold for number of flows in TOP-N
__MIN_FLOW=300

# minimal / maximal limits for detection of potentional amplification attack
# traffic - PER-FLOW(top-N record) limits
__FPS_THRESHOLD=1000
__BPS_THRESHOLD=1000000
__FPS_THRESHOLD2=$(($__FPS_THRESHOLD/20))
__BPS_THRESHOLD2=$(($__BPS_THRESHOLD/20))

# threshold for checking of message size distinctness
__MSG_SIZE_CHANGE_THRESHOLD=10

################################################################################
## Print usage of this script
print_usage() {
    echo "Amplification detection (FDistDump)"
    echo "==================================="
    echo "Script for detection of amplification attacks in stored data. Detection"
    echo "is triggered for small time intervals (time windows) in selected data sets."
    echo "For every time window there is a report printed to \"${__DETECTION_LOG}\"."
    echo "Every report contains ${__TOP_N_SIZE} most significant \"attackers\" (i.e. abused servers)"
    echo "for the ${__TOP_N_SIZE} most affected victims."
    echo ""
    echo "Don't forget to setup all necessary variables in ${__COMMON_CONFIG}."
    echo "Detection parameters could be adjusted in this script."
    echo ""
    echo "Usage: ./amplification_detection.sh [start] [end] [window_size] [data] [port]"
    echo ""
    echo "start: Start of the analyzed time interval"
    echo "  (default is <actual_time> - <window_size>)"
    echo ""
    echo "end: End of the analyzed time interval (default is:"
    echo "  <actual_time> + <time-window_size> if <start> is set,"
    echo "  <actual_time> otherwise)"
    echo ""
    echo "window_size: Detection window size. Analyzed time interval is split to smaller"
    echo "  interval of <window_size> size. Size of last window is adjusted to be at least"
    echo "  <window_size> long (default is $__TIME_WINDOW_SIZE)"
    echo ""
    echo "data: Path to data files (default is $__DATA)"
    echo ""
    echo "port: Port number of amplification attack type (default is $__PORT_OF_SERVICE)"
}

################################################################################
## Function for determination if given address is "attacker" or not
## Parameters:
##    $1: IP address of potentional "attacker" (abused server)
##    $2: IP address of potentional attack target
##    $3: Time window
is_attacker() {
    __OUTPUT_FORMAT="--fields=bytes --output-format=csv --progress-bar=none"
    __FILTER="src ip $1 && dst ip $2 && port $__PORT_OF_SERVICE"
    _REC_CNT=500
    cmd="mpirun --hostfile $__HOSTFILE --preload-binary $__FDD_BIN $__OUTPUT_FORMAT -l $_REC_CNT -f \"$__FILTER\" -t \"$3\" $__DATA"

    _old_msg_size=-1
    _msg_size_changes=0
    _cmp_cnt=0
    while read -r line; do
        #skip header
        if [[ "$line" == "bytes" ]]; then
            continue
        fi

        if (( "$line" != "$_old_msg_size" )); then
            if (( "$_old_msg_size" >= "0" )); then
                _msg_size_changes=$(($_msg_size_changes+1))
                if (( "$_msg_size_changes" == "$__MSG_SIZE_CHANGE_THRESHOLD" )); then
                    # Too many changes in message sizes - not attack(er)
                    # 1 = false
                    return 1
                fi
            fi
        fi
        _cmp_cnt=$(($_cmp_cnt+1))
        _old_msg_size="$line"
    done < <(eval $cmd)

    # If there was at least $_REC_CNT messages with consistent sizes - is attack
    if (( "$_cmp_cnt" == "$_REC_CNT")); then
        # 0 = true
        return 0
    else
        # 1 = false
        return 1
    fi
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
echo "Starting detection of amplification attacks:" >"$__DETECTION_LOG" 2>&1
echo "- for port (service): $__PORT_OF_SERVICE" >>"$__DETECTION_LOG" 2>&1
_s=$(date -d @"$__START_TIME" +"%m/%d/%Y %H:%M")
_e=$(date -d @"$__END_TIME" +"%m/%d/%Y %H:%M")
echo "- in time interval: $_s to $_e" >>"$__DETECTION_LOG" 2>&1
while (("$__diff" > 0)); do
    unset __POT_VICTIMS
    unset __PV_TOTAL
    unset __PV_STDDEV
    unset __PV_AVG
    unset __PV_ATT_CNT
    unset __PV_ATTACKERS

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
    # Phase 1: Search for potentional amplification attack victims

    __OUTPUT_FORMAT="--fields=bytes,pkts,bpp,bps,duration --output-format=csv --output-addr-conv=str --progress-bar=none"

    # Prepare detection filter:
    # detection v1: bpp + minimal packet count
    __DET_FILTER="src port $__PORT_OF_SERVICE && bpp > $__MIN_BPP && packets > $__MIN_PACKETS && packets < $__MAX_PACKETS"
    # detection v2: min bytes count
    #__DET_FILTER="src port $__PORT_OF_SERVICE && bytes > $__MIN_BYTES && packets < $__MAX_PACKETS"

    # Prepare FDistDump query command
    cmd="mpirun --hostfile $__HOSTFILE --preload-binary $__FDD_BIN $__OUTPUT_FORMAT -s dstip -l $__TOP_N_SIZE -o flows -f \"$__DET_FILTER\" -t \"$__TIME_SELECTOR\" $__DATA"

    # Associative array for storing "per-victim volume of attack traffic"
    declare -A __POT_VICTIMS
    while read -r line; do
        # columns: first,last,bytes,pkts,flows,dstip,duration,bps,bpp

        # 2nd part of Phase 1 detection (1st is through the filter) - take only
        # IP addresses with flow-per-second/bytes-per-second above the threshold
        _ip=$(echo $line | awk -v fps="$__FPS_THRESHOLD" -v bps="$__BPS_THRESHOLD" 'BEGIN {FS=","} {if (int($8) >= bps || ($5*1000/($7+0.2)) >= fps) print $6}')

        if [[ "$_ip" != "" ]]; then
            # Store ip address of the victim (key) and volume of traffic (value)
            __POT_VICTIMS[$_ip]=$(echo $line | awk 'BEGIN {FS=","} {print $8}')
        fi
    done < <(eval $cmd)

    ############################################################################
    # Phase 2: Make statistics about 5 most significant (potentional)
    #          "attackers" (abused servers) for every found victim

    for ip in "${!__POT_VICTIMS[@]}"; do
        # Prepare FDistDump query command
        __OUTPUT_FORMAT="--fields=bytes,pkts,bpp,bps,duration --output-format=csv --output-addr-conv=str --progress-bar=none"
        cmd="mpirun --hostfile $__HOSTFILE --preload-binary $__FDD_BIN $__OUTPUT_FORMAT -s srcip,dstip -l $__TOP_N_SIZE -o pkts -f \"$__DET_FILTER && ip ${ip}\" -t \"$__TIME_SELECTOR\" $__DATA"

        _attackers=()
        while read -r line; do
            # columns: first,last,bytes,pkts,flows,dstip,duration,bps,bpp

            # Take only significant records (with flow count greater than
            # threshold) - not used in v1
            #_ip=$(echo $line | awk -v fps="$__FPS_THRESHOLD2" -v bps="$__BPS_THRESHOLD2" 'BEGIN {FS=","} {if (int($8) >= bps || ($5*1000/($7+0.2)) >= fps) print $6}')

            _ip=$(echo $line | awk 'BEGIN {FS=","} {print $6}')
            # Skip header
            if [[ "$_ip" != "srcip" ]]; then
                _attackers+=("$_ip")
            fi
        done < <(eval $cmd)

        _det_rel=0

        # Check "the most significant" attacker
        if is_attacker "${_attackers[0]}" "$ip" "$__TIME_SELECTOR"; then
            _det_rel=$(($_det_rel+50))
        fi
        # Check "the least significant" attacker from "the most significant"
        # attackers
        if is_attacker "${_attackers[-1]}" "$ip" "$__TIME_SELECTOR"; then
            _det_rel=$(($_det_rel+50))
        fi

        if (("$_det_rel" >= "50")); then
            echo "--------------------------------------------------------------------------------" >>"$__DETECTION_LOG" 2>&1
            echo "Detection Reliability: $_det_rel %" >>"$__DETECTION_LOG" 2>&1
            echo "Attack Target: $ip" >>"$__DETECTION_LOG" 2>&1
            echo "Attack Rate: ${__POT_VICTIMS[$ip]} B/s" >>"$__DETECTION_LOG" 2>&1
            echo "Dominant Amplifiers: [${_attackers[0]},${_attackers[1]},${_attackers[2]},${_attackers[3]},${_attackers[4]}]" >>"$__DETECTION_LOG" 2>&1
            echo "Traffic Sample:" >>"$__DETECTION_LOG" 2>&1
            _output_filter="port $__PORT_OF_SERVICE && ip ${ip} && (ip ${_attackers[0]}"
            for i in {1..4}; do
                if [[ "${_attackers[$i]}" != "" ]]; then
                    _output_filter="${_output_filter} || ip ${_attackers[$i]}"
                fi
            done
            _output_filter="${_output_filter} )"
            _cmd="mpirun --hostfile $__HOSTFILE --preload-binary ~/fdistdump/src/fdistdump --output-volume-conv=none --progress-bar=none -l 100 -f \"$_output_filter\" -t \"$__TIME_SELECTOR\" $__DATA"
            echo "" >>"$__DETECTION_LOG" 2>&1
            echo "$_cmd" >>"$__DETECTION_LOG" 2>&1
            echo "" >>"$__DETECTION_LOG" 2>&1
            eval "$_cmd" >>"$__DETECTION_LOG" 2>&1
        fi

    done
done
