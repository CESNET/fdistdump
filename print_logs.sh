#!/usr/bin/env sh

for f in tests/*.testlog; do
      echo "${f}:"
      cat $f
      echo "=== logs ========================================================="
done
