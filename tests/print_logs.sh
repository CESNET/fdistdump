#!/usr/bin/env bash

for LOG_FILE in `find . -type f -name "*.log"`
do
        echo "${LOG_FILE}:"
        cat "${LOG_FILE}"
        echo
        echo
done
