#!/usr/bin/env bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

for LOG_FILE in $(find "${DIR}" -type f -name "*.log")
do
        echo "${LOG_FILE}:"
        cat "${LOG_FILE}"
        echo
        echo
done
