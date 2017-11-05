#!/usr/bin/env bash
DIR="$( dirname "$0" )"
xz -T 0 -d -c "${@:$#-1:1}" | "${DIR}/netvid_slice" "${@:1:$#-2}" --input-file - --output-file - | xz -T 0 >"${@: -1}"