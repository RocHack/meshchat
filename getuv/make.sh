#!/bin/sh
set -e
if make check_version ; then
    ./check_version
else
    make just_internal
    ./just_internal
fi
