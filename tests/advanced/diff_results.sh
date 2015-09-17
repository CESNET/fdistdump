#!/bin/bash

# Author: Pavel Krobot, <Pavel.Krobot@cesnet.cz>
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
                        if [ `echo $line | cut -d"," -f${__sf}` -eq `echo $__last_line | cut -d"," -f${__sf}` ]; then
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

if [[ $query_type -eq $G_QTYPE_LISTFLOWS ]]; then
        #fdd results formating
        cut -d"," -f1-9 $fddr >$fddr_tmp
        #nfd results formating
        awk -F "|" 'NF>20{ts=$3;te=$5;for(i=length($3);i<3;i++)ts="0" ts;for(i=length($5);i<3;i++)te="0" te;$2=$2 ts;$4=$4 te;print $2","$4","$6","$7":"$8":"$9":"$10","$11","$12":"$13":"$14":"$15","$16","$24","$23;}' $nfdr >$nfdr_tmp
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
        tail -n +2 $fddr | cut -d"," -f1-4,6- >$fddr_tmp
        #nfd results formating
        awk -F "|" 'NF>20{ts=$3;te=$5;for(i=length($3);i<3;i++)ts="0" ts;for(i=length($5);i<3;i++)te="0" te;$2=$2 ts;$4=$4 te;print $2","$4","$24","$23","'$f_spec';}' $nfdr >$nfdr_tmp
elif [[ $query_type -eq $G_QTYPE_STATS ]]; then
        if [[ "$AGG_FIELDS" == "srcip" ]] || [[ "$AGG_FIELDS" == "dstip" ]]; then
                tail -n +2 $fddr | sed -E 's#([0-9]{2}:[0-9]{2}:[0-9]{2})\.[0-9]{1,3},#\1,#g' > $fddr_tmp
                tail -n +2 $nfdr | awk -F "," 'NF>10{print $1","$2","$10","$8","$6","$5;}' > $nfdr_tmp
        else
                tail -n +2 $fddr > $fddr_tmp
                awk -F "|" 'NF>10{ts=$3;te=$5;for(i=length($3);i<3;i++)ts="0" ts;for(i=length($5);i<3;i++)te="0" te;$2=$2 ts;$4=$4 te;print $2","$4","$10","$9","$8","$7;}' $nfdr > $nfdr_tmp
        fi
else
        echo "Diff results: Error: Unknown query type specification."
        return 1
fi

diff1="${ADV_TESTS_HOME}/diff1.txt"
diff2="${ADV_TESTS_HOME}/diff2.txt"

rm -f $diff1 $diff2
touch $diff1 $diff2

if [[ $result_is_sorted == 1 ]]; then
        #sorting of results was requested in query, so lines order have to be considered
        unify_sort $fddr_tmp $sort_field
        unify_sort $nfdr_tmp $sort_field
        diff $fddr_tmp $nfdr_tmp >$diff1
else
        grep -vxFf $fddr_tmp $nfdr_tmp >$diff1
        grep -vxFf $nfdr_tmp $fddr_tmp >$diff2
fi

echo "=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#"
cat $fddr_tmp
echo "=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#"
cat $nfdr_tmp
echo "=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#"

rm -f $fddr_tmp $nfdr_tmp

if [[ -s $diff1 ]] || [[ -s $diff2 ]]; then
        echo "Diff results: Results do not match."
        echo "-- diff1: ------------------------------->>>"
        cat $diff1
        echo "<<<------------------------------------ diff1"
        echo "-- diff2: ------------------------------->>>"
        cat $diff2
        echo "<<<------------------------------------ diff2"
        rm -f $diff1 $diff2
        return 1
else
        echo "Diff results: Results match."
        rm -f $diff1 $diff2
        return 0
fi
