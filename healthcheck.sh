#!/bin/sh

if ! pidof dump978-fa > /dev/null 2>&1; then
    exit 1
fi

if [ -n "$BEAST_PORT" ] && ! pidof socat > /dev/null 2>&1; then
    exit 1
fi

exit 0
