#!/bin/bash

# Author: Pavel Krobot, <Pavel.Krobot@cesnet.cz>
# Author: Jan Wrona, <wrona@cesnet.cz>
# Date: 2015
#
# Description: Compare results from FDistDump and NFDump queries. Return 0 if
# matches, 1 if not.
#
#
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


ADV_TESTS_HOME=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )

# import common setup
. ${ADV_TESTS_HOME}/tests_setup.sh


# Function searches for lines with same "sort value". Lines with same value are
# then sorted with linux sort - "global" sort remains untouched, "local" sorts
# are unified.
function unify_sort {
        __tmp_f="${1}.tmp"
        __tmp_sort="${ADV_TESTS_HOME}/sort_tmp.txt"

        #it is important to "delete" original results file here
        mv $1 $__tmp_f
        rm -f $tmp_sort

        __sf=$2

        __c=0
        unset __last_line
        while read line || [[ -n "$line" ]]; do
                if [[ ! -z $__last_line ]]; then
                        #if [ `echo $line | cut -d"," -f${__sf}` -eq `echo $__last_line | cut -d"," -f${__sf}` ]; then
                        if [[ `echo $line | cut -d"," -f${__sf}` == `echo $__last_line | cut -d"," -f${__sf}` ]]; then
                                echo $__last_line >> $__tmp_sort
                                __c=$(expr $__c + 1)
                        else
                                if (( $__c == 0 )); then
                                        echo $__last_line >>$1
                                else
                                        echo $__last_line >> $__tmp_sort
                                        sort -k$__sf $__tmp_sort >>$1

                                        rm -f $__tmp_sort
                                        c=0
                                fi
                        fi
                 fi
                 __last_line=$line
        done < $__tmp_f


        if (( $__c == 0 )); then
                echo $__last_line >>$1
        else
                echo $__last_line >> $__tmp_sort
                sort -k$__sf $__tmp_sort >>$1
        fi

        #cleanup
        unset __last_line

        rm -f $__tmp_sort
        rm -f $__tmp_f
}



if [ -z $1 ]; then
        echo "Diff results: Error: Missing FDistDump query results filename."
        return 1
else
        if [ ! -s $1 ]; then
                echo "Diff results: Error: FDistDump query results file missing or is empty."
                return 1
        fi
        fddr=$1
        fddr_tmp="${fddr}.tmp"
fi

if [ -z $2 ]; then
        echo "Diff results: Error: Missing NFDump query results filename."
        return 1
else
        if [ ! -s $2 ]; then
                echo "Diff results: Error: FDistDump query results file missing or is empty."
                return 1
        fi
        nfdr=$2
        nfdr_tmp="${nfdr}.tmp"
fi

if [ -z $3 ]; then
        echo "Diff results: Error: Missing query type specification."
        return 1
else
        query_type=$3
        AGG_FIELDS=(${4//,/ })
fi

if [ ! -z $5 ]; then
        result_is_sorted=1
        sort_field=$5
else
        result_is_sorted=0
fi

AWK_LIST_NFD2FDD='{
        first_sec = $2
        first_msec = $3
        last_sec = $4
        last_msec = $5
        proto = $6
        srcip=$7":"$8":"$9":"$10
        srcport=$11
        dstip=$12":"$13":"$14":"$15
        dstport=$16
        tcpflags=$21
        pkts=$23
        bytes=$24

        first = first_sec * 1000 + first_msec
        last = last_sec * 1000 + last_msec

        print first","last","bytes","pkts","srcport","dstport","tcpflags","srcip","dstip","proto
}'

AWK_AGGR_NFD2FDD='{
        first_sec = $2
        first_msec = $3
        last_sec = $4
        last_msec = $5
        pkts=$23
        bytes=$24

        first = first_sec * 1000 + first_msec
        last = last_sec * 1000 + last_msec

        print first","last","bytes","pkts","F_SPEC
}'

if [[ $query_type -eq $G_QTYPE_LISTFLOWS ]]; then
        #fdd results formating
        sed "1d" $fddr > $fddr_tmp

        #nfd results formating
        awk -F "|" "$AWK_LIST_NFD2FDD" $nfdr >$nfdr_tmp

elif [[ $query_type -eq $G_QTYPE_AGGREG ]]; then
        #create column specification string for awk - driven by selected aggregation fields
        f_spec=""
        for f in ${AGG_FIELDS[@]}; do
                if [ ! -z $f_spec ]; then
                        f_spec=${f_spec}"\",\""
                fi

                case "$f" in
                "srcport")
                        f_spec="${f_spec}\$11"
                        ;;
                "dstport")
                        f_spec="${f_spec}\$16"
                        ;;
                "proto")
                        f_spec="${f_spec}\$6"
                        ;;
                "srcip")
                        f_spec="${f_spec}\$7\":\"\$8\":\"\$9\":\"\$10"
                        ;;
                "dstip")
                        f_spec="${f_spec}\$12\":\"\$13\":\"\$14\":\"\$15"
                        ;;
                *)
                        echo "Diff results: Error: Unknown aggregation field $f."
                        return 1
                esac
        done

        #fdd results formating
        sed "1d" $fddr > $fddr_tmp

        #nfd results formating
        awk -F "|" "${AWK_AGGR_NFD2FDD/F_SPEC/$f_spec}" $nfdr > $nfdr_tmp

elif [[ $query_type -eq $G_QTYPE_STATS ]]; then
        #create column specification string for awk - driven by selected aggregation fields
        f_spec=""
        for f in ${AGG_FIELDS[@]}; do
                if [ ! -z $f_spec ]; then
                        f_spec=${f_spec}"\",\""
                fi

                case "$f" in
                "srcport")
                        f_spec="${f_spec}\$11"
                        ;;
                "dstport")
                        f_spec="${f_spec}\$16"
                        ;;
                "proto")
                        f_spec="${f_spec}\$6"
                        ;;
                "srcip")
                        f_spec="${f_spec}\$7\":\"\$8\":\"\$9\":\"\$10"
                        ;;
                "dstip")
                        f_spec="${f_spec}\$12\":\"\$13\":\"\$14\":\"\$15"
                        ;;
                *)
                        echo "Diff results: Error: Unknown aggregation field $f."
                        return 1
                esac
        done

        #fdd results formating
        sed "1d" $fddr | cut -d, -f -4,6- > $fddr_tmp

        #nfd results formating
        awk -F "|" "${AWK_AGGR_NFD2FDD/F_SPEC/$f_spec}" $nfdr > $nfdr_tmp
else
        echo "Diff results: Error: Unknown query type specification."
        return 1
fi


if [[ $result_is_sorted == 1 ]]; then
        #sorting of results was requested in query, so lines order have to be considered
        unify_sort $fddr_tmp $sort_field
        unify_sort $nfdr_tmp $sort_field
else
        sort -o $fddr_tmp $fddr_tmp
        sort -o $nfdr_tmp $nfdr_tmp
fi


DIFF=$(diff $fddr_tmp $nfdr_tmp)
ret_code=$?
if [[ $ret_code -ne 0 ]]; then
        echo "Diff results: Results do not match."
        echo "$DIFF"
else
        echo "Diff results: Results match."
fi

rm -f $fddr_tmp $nfdr_tmp
return $ret_code
