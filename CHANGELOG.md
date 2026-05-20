# Changelog

## [v10.3] - 2026-05-20

### Added
- `avr2beast` — AVR-to-binary-Beast converter with built-in heartbeat injection
  (`select()`-based 30s idle timeout sends Beast status frames to keep readers alive)
- Beast heartbeat/status frames (`0x1a 0x14 + timestamp`) every 30s of inactivity,
  preventing readsb 70-second disconnect during silent UAT periods
- BEAST_PORT environment variable: exposes Beast binary output for ultrafeeder/readsb
- `uat2esnt` (legacy UAT→1090ES converter) built into Docker image
- RTL_TCP sample source (`--sdr rtl_tcp:<host>:<port>`) for remote SDR devices
- Multi-arch Docker build (linux/amd64 + linux/arm64) via GitHub Actions + QEMU + Buildx
- Docker healthcheck (pidof-based) for Swarm orchestration
- `.github/workflows/docker-publish.yml` for GHCR image publication

### Changed
- Bridge pipeline: replaced fragile background-process bridge script with
  `socat SYSTEM` running `socat raw_port | uat2esnt | avr2beast` as a single
  foreground pipeline — no race conditions, no orphaned background PIDs
- `docker-entrypoint.sh`: cleaner env var mapping, socat bridge uses SYSTEM address
- Healthcheck: pidof-only (removed ss-based port check) to avoid ARM64 compatibility issues
- Signal handling: SIGTERM/SIGINT triggers clean shutdown with exit 0 instead of
  "Abnormal exit" false positive

### Fixed
- readsb disconnecting every 70s during silent periods (root cause: uat2esnt outputs
  AVR text format, not binary Beast; avr2beast now converts to correct format)
- Container killed unexpectedly on ARM64 due to healthcheck ss port check mismatch
- Bridge script background processes not reliably writing to socat EXEC TCP socket

## [v10.2] - 2025-08-15

Initial fork from flightaware/dump978 with Docker and RTL_TCP support.
