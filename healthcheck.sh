#!/bin/sh

RAW_PORT="${RAW_PORT:-30000}"
JSON_PORT="${JSON_PORT:-30001}"

if ! pidof dump978-fa > /dev/null 2>&1; then
    exit 1
fi

if ! ss -tlnp 2>/dev/null | grep -q ":${RAW_PORT} "; then
    exit 1
fi

if ! ss -tlnp 2>/dev/null | grep -q ":${JSON_PORT} "; then
    exit 1
fi

exit 0
