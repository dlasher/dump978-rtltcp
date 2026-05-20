# Stage 1: Builder
FROM debian:bookworm AS builder

SHELL ["/bin/bash", "-x", "-o", "pipefail", "-c"]

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        g++ \
        make \
        libboost-system-dev \
        libboost-program-options-dev \
        libboost-regex-dev \
        libboost-filesystem-dev \
        libsoapysdr-dev \
        librtlsdr-dev \
    && rm -rf /var/lib/apt/lists/*

COPY . /app
WORKDIR /app
RUN make clean && make

# Stage 2: Runtime
FROM debian:bookworm-slim

SHELL ["/bin/bash", "-x", "-o", "pipefail", "-c"]

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        libboost-system1.74.0 \
        libboost-program-options1.74.0 \
        libboost-regex1.74.0 \
        libboost-filesystem1.74.0 \
        libsoapysdr0.8 \
        librtlsdr0 \
        iproute2 \
        procps \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /app/dump978-fa /usr/local/bin/
COPY --from=builder /app/skyaware978 /usr/local/bin/
COPY docker-entrypoint.sh /usr/local/bin/
COPY healthcheck.sh /usr/local/bin/

RUN chmod +x /usr/local/bin/dump978-fa /usr/local/bin/skyaware978 \
              /usr/local/bin/docker-entrypoint.sh /usr/local/bin/healthcheck.sh

EXPOSE 30000/tcp 30001/tcp

HEALTHCHECK --start-period=10s --interval=15s --timeout=5s --retries=3 \
    CMD /usr/local/bin/healthcheck.sh

ENTRYPOINT ["/usr/local/bin/docker-entrypoint.sh"]
