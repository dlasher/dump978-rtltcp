#!/bin/sh
set -e

ARGS=""

if [ -n "$SDR_DEVICE" ]; then
    ARGS="$ARGS --sdr $SDR_DEVICE"
fi

ARGS="$ARGS --raw-port ${RAW_PORT:-30000}"
ARGS="$ARGS --json-port ${JSON_PORT:-30001}"

if [ -n "$SDR_GAIN" ]; then
    ARGS="$ARGS --sdr-gain $SDR_GAIN"
fi

if [ -n "$SDR_PPM" ]; then
    ARGS="$ARGS --sdr-ppm $SDR_PPM"
fi

if [ -n "$EXTRA_ARGS" ]; then
    ARGS="$ARGS $EXTRA_ARGS"
fi

exec /usr/local/bin/dump978-fa $ARGS "$@"
