#!/usr/bin/env bash
DIR="$( dirname "$0" )"
"${DIR}/netvid_record" --file - "${@:1:$#-1}" | setsid xz -T 0 >"${@: -1}"