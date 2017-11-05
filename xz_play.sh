#!/usr/bin/env bash
DIR="$( dirname "$0" )"
xz -T 0 -d -c "${@: -1}" | "${DIR}/netvid_play" "${@:1:$#-1}" --file -