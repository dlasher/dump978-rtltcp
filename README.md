# dump978-fa

Fork of FlightAware's 978MHz UAT decoder with RTL_TCP remote SDR support.

It is a reimplementation in C++, loosely based on the demodulator from
https://github.com/mutability/dump978.

## Overview

dump978-fa is the main binary. It talks to the SDR, demodulates UAT data,
and provides the data in a variety of ways - either as raw messages or as
json-formatted decoded messages, and either on a network port or to stdout.

skyaware978 connects to a running dump978-fa and writes json files suitable
for use by the SkyAware web map.

## Building as a package

Caution: The package build is memory-hungry. A 1GB Pi will fail to build the
package. Please build on a machine with more memory, or add swap.

```
$ sudo apt-get install \
  build-essential \
  debhelper \
  dh-systemd \
  libboost-system-dev \
  libboost-program-options-dev \
  libboost-regex-dev \
  libboost-filesystem-dev \
  libsoapysdr-dev

$ dpkg-buildpackage -b
$ sudo dpkg -i ../dump978-fa_*.deb ../skyaware978_*.deb
```

## Building from source

  1. Ensure SoapySDR, Boost, and librtlsdr are installed
  2. 'make'

## Installing the SoapySDR driver module

You will want at least one SoapySDR driver installed. For rtlsdr, try

```
$ sudo apt-get install soapysdr-module-rtlsdr
```

## Configuration

For a package install, see `/etc/default/dump978-fa` and
`/etc/default/skyaware978`.

The main options are:

 * `--sdr` specifies the SDR to use, in the format expected by
   SoapySDR. For a rtlsdr, try `--sdr driver=rtlsdr`. To select a
   particular rtlsdr dongle by serial number, try
   `--sdr driver=rtlsdr,serial=01234567`
 * `--sdr rtl_tcp:<host>:<port>` connects to a remote RTL_TCP server.
   If port is omitted, 1234 is used as default.
 * `--sdr-gain` sets the SDR gain (default: max)
 * `--raw-port` listens on the given TCP port and provides raw messages
 * `--json-port` listens on the given TCP port and provides decoded messages
   in json format

Pass `--help` for a full list of options.

## Docker

### Building

```bash
docker build -t ghcr.io/dlasher/dump978-rtltcp:latest .
```

### Environment Variables

| Variable | Maps to | Default |
|----------|---------|---------|
| `SDR_DEVICE` | `--sdr` | required |
| `RAW_PORT` | `--raw-port` | `30000` |
| `JSON_PORT` | `--json-port` | `30001` |
| `SDR_GAIN` | `--sdr-gain` | auto |
| `SDR_PPM` | `--sdr-ppm` | not set |
| `BEAST_PORT` | Beast output via uat2esnt | not set |
| `EXTRA_ARGS` | appended verbatim | empty |

When `BEAST_PORT` is set, a pipeline (`uat2esnt | avr2beast`) runs alongside
dump978-fa, converting raw UAT frames to Beast binary format on the given port
(for readsb/ultrafeeder). `avr2beast` also injects Beast heartbeat/status frames
every 30s of inactivity to keep readers connected during silent periods.

**Recommended configuration with ultrafeeder:**

```yaml
dump978:
  image: ghcr.io/dlasher/dump978-rtltcp:latest
  environment:
    - SDR_DEVICE=rtl_tcp:172.21.0.1:2345
    - RAW_PORT=30000
    - JSON_PORT=30001
    - BEAST_PORT=37982
```

Ultrafeeder connects to `dump978:37982` with `beast_in` and receives
Beast-format 978MHz data.

### Docker Compose

**Host network mode** (simplest, no port mapping needed):

```yaml
services:
  dump978:
    image: ghcr.io/dlasher/dump978-rtltcp:latest
    network_mode: host
    environment:
      - SDR_DEVICE=rtl_tcp:10.10.10.10:1234
      - RAW_PORT=30000
      - JSON_PORT=30001
    restart: on-failure
```

**Bridged network** (explicit port mapping):

```yaml
services:
  dump978:
    image: ghcr.io/dlasher/dump978-rtltcp:latest
    ports:
      - "30000:30000"
      - "30001:30001"
    environment:
      - SDR_DEVICE=rtl_tcp:10.10.10.10:1234
      - RAW_PORT=0.0.0.0:30000
      - JSON_PORT=0.0.0.0:30001
    restart: on-failure
```

### Docker Swarm

```yaml
services:
  dump978:
    image: ghcr.io/dlasher/dump978-rtltcp:latest
    environment:
      - SDR_DEVICE=rtl_tcp:10.10.10.10:1234
      - RAW_PORT=0.0.0.0:30000
      - JSON_PORT=0.0.0.0:30001
      - TZ=${FEEDER_TZ}
    networks:
      - swarmnet
    deploy:
      replicas: 1
      restart_policy:
        condition: on-failure
        delay: 10s
        window: 120s
      update_config:
        parallelism: 1
        delay: 10s
        failure_action: rollback
      placement:
        constraints:
          - node.hostname == adsb
    healthcheck:
      test: ["CMD-SHELL", "ss -tlnp | grep -E ':(30000|30001)' || netstat -tlnp 2>/dev/null | grep -E ':(30000|30001)'"]
      interval: 30s
      timeout: 10s
      retries: 3
      start_period: 30s
```

## Third-party code

Third-party source code included in libs/:

 * fec - from Phil Karn's fec-3.0.1 library (see fec/README)
 * json.hpp - JSON for Modern C++ v3.5.0 - https://github.com/nlohmann/json
