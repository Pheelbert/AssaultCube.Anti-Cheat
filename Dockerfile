FROM ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    clang \
    make \
    automake \
    autoconf \
    libtool \
    zlib1g-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY source/ source/

RUN find source/ -type f -exec sed -i 's/\r$//' {} + \
    && chmod +x source/enet/configure source/enet/config.sub source/enet/config.guess source/enet/install-sh source/enet/depcomp \
    && cd source/src && make clean && make server && make server_install

# --- Runtime stage ---
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    zlib1g \
    && rm -rf /var/lib/apt/lists/*

RUN useradd -r -s /usr/sbin/nologin acserver

WORKDIR /ac

COPY --from=builder /build/bin_unix/native_server bin_unix/native_server
COPY config/ config/
COPY packages/maps/ packages/maps/

RUN mkdir -p logs data && chown -R acserver:acserver /ac

USER acserver

# Game port and server info port
EXPOSE 28763/udp 28764/udp

ENTRYPOINT ["./bin_unix/native_server"]
CMD ["-Cconfig/servercmdline.txt"]
