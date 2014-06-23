#!/usr/bin/env bash

set -e
if make check_version >/dev/null 2>&1; then
    ./check_version
else
    make just_internal
    ./just_internal
fi
