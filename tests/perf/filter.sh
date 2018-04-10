#!/usr/bin/env bash

# Copyright 2018 CESNET
#
# This file is part of Fdistdump.
#
# Fdistdump is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Fdistdump is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Fdistdump.  If not, see <http://www.gnu.org/licenses/>.

set -eu

readonly RUNS=3
readonly DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly TEST="$(basename "$0" .sh)"
readonly FDD_ARGS=(--output-items=processed-records-summary
                   --output-format=csv
                   --no-bfindex
                   --progress-bar-type=none
                   )

# expected output:
# flows,packets,bytes,seconds,flows/second
# 4,8,320,3.447123,1.2

################################################################################
die() {
    echo Error: "$@" >&2
    exit 1
}

main() {
    if [[ $# -lt 2 ]]; then
        echo Usage: $0 fdistdump-executable path ...
        exit 1
    fi

    local -r FDD_EXECUTABLE="$1"
    shift

    while IFS= read -r FILTER; do
        echo "$FILTER:"
        unset FLOWS
        unset PACKETS
        unset BYTES
        unset OUT_ARR

        for THREADS in {1..4}; do
            echo -ne "\t${THREADS}:"
            local TIME_SUM="0.0"
            for RUN in $(seq "$RUNS"); do
                # WARNING: mpiexec consumes whole stdin and causes the while
                # loop to break after first iteration
                OUT="$(mpiexec -n 2 -env OMP_NUM_THREADS "$THREADS" \
                       "$FDD_EXECUTABLE" "${FDD_ARGS[@]}" \
                       -f "$FILTER" \
                       "$@" </dev/null | tail -1)"
                IFS=, read -ra OUT_ARR <<<"$OUT"

                [[ ${FLOWS:=${OUT_ARR[0]}} -ne "${OUT_ARR[0]}" ]] \
                    && die flows mismatch
                [[ ${PACKETS:=${OUT_ARR[1]}} -ne "${OUT_ARR[1]}" ]] \
                    && die packets mismatch
                [[ ${BYTES:=${OUT_ARR[2]}} -ne "${OUT_ARR[2]}" ]] \
                    && die bytes mismatch

                TIME="${OUT_ARR[3]}"
                TIME_SUM="$(bc -l <<<"$TIME_SUM+$TIME")"
                echo -ne "\t${TIME}"
            done
            local TIME_AVG="$(bc -l <<<"$TIME_SUM/$RUNS")"
            echo -e "\t\t${TIME_AVG}"
        done
    done <"$DIR/$TEST.in"
}

################################################################################
main "$@"
