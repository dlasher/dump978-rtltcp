#!/bin/sh

if [ -n "$BEAST_PORT" ]; then
    RAW_PORT_NUM="${RAW_PORT##*:}"
    RAW_PORT_NUM="${RAW_PORT_NUM:-30000}"

    # Unidirectional: dump978 raw port → uat2esnt (UAT→1090ES) → avr2beast (AVR→binary Beast, with heartbeats)
    # socat SYSTEM runs the pipeline via sh -c, connecting stdin/stdout to the TCP socket.
    # avr2beast sends Beast heartbeat/status frames every 30s of inactivity.
    socat TCP-LISTEN:${BEAST_PORT},fork,reuseaddr SYSTEM:"socat -u TCP:127.0.0.1:${RAW_PORT_NUM} STDOUT 2>/dev/null | /usr/local/bin/uat2esnt | /usr/local/bin/avr2beast" &
fi

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
