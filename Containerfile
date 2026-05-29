# ════════════════════════════════════════════════════════════════════════
#  Containerfile — DfApp Direction Finding Service
#  Multi-stage: builder → runtime
#  Base: Ubuntu 24.04
#
#  Build:
#    podman build -t sdr-df:1.0.0 .
#
#  Run:
#    podman run --rm --network=host \
#      -v ./config/df.xml:/etc/sdr-df/df.xml:ro \
#      sdr-df:1.0.0
# ════════════════════════════════════════════════════════════════════════

FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

# Ubuntu 24.04 merges proton-core and proton-proactor into libqpid-proton.pc
# but libqpid-proton-cpp.pc still lists them as Requires. Stub them out.
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake pkg-config git ca-certificates \
        libqpid-proton11-dev \
        libqpid-proton-cpp12-dev \
        libtinyxml2-dev \
        libspdlog-dev \
        libfmt-dev \
        libeigen3-dev \
        libpq-dev \
        libfftw3-dev \
    && rm -rf /var/lib/apt/lists/* \
    && sed -i 's|prefix=${pcfiledir}/../..|prefix=/usr|' \
           /usr/lib/x86_64-linux-gnu/pkgconfig/libqpid-proton*.pc \
    && printf 'prefix=/usr\nName: Proton Core\nDescription: stub\nVersion: 0.37.0\nLibs: -lqpid-proton\nCflags: -I/usr/include\n' \
       > /usr/lib/x86_64-linux-gnu/pkgconfig/libqpid-proton-core.pc \
    && printf 'prefix=/usr\nName: Proton Proactor\nDescription: stub\nVersion: 0.37.0\nLibs: -lqpid-proton\nCflags: -I/usr/include\n' \
       > /usr/lib/x86_64-linux-gnu/pkgconfig/libqpid-proton-proactor.pc

RUN git clone --depth 1 --branch "main/1.0" \
        https://github.com/BMichaud7/SdrSdk.git /workspace/SdrSdk && \
    git clone --depth 1 --branch "main/1.0" \
        https://github.com/BMichaud7/SdrTaskApi.git /workspace/SdrTaskApi

WORKDIR /workspace/DfApp
COPY CMakeLists.txt .
COPY include/        include/
COPY src/            src/
COPY config/         config/
COPY schema/         schema/

RUN cmake -B build \
        -S . \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/install \
        -DFETCHCONTENT_QUIET=OFF \
        -DWITH_DB=ON \
        -DBUILD_TESTING=OFF \
    && cmake --build build --parallel "$(nproc)" \
    && cmake --install build


# ── Runtime ───────────────────────────────────────────────────────────────────
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

RUN apt-get update && apt-get install -y --no-install-recommends \
        libtinyxml2-10 \
        libspdlog1.12 \
        libfmt9 \
        libqpid-proton-cpp12 \
        libpq5 \
        tini \
    && rm -rf /var/lib/apt/lists/*

RUN groupadd -r sdrdf && useradd -r -g sdrdf -s /sbin/nologin sdrdf
RUN mkdir -p /etc/sdr-df && chown sdrdf:sdrdf /etc/sdr-df

COPY --from=builder /install/bin/sdr_df         /usr/local/bin/sdr_df
COPY --from=builder /install/etc/sdr-df/df.xml  /etc/sdr-df/df.xml

USER sdrdf

ENV SDR_DF_CONFIG_PATH=/etc/sdr-df/df.xml
ENV SDR_LOG_LEVEL=info

ENTRYPOINT ["/usr/bin/tini", "--"]
CMD ["/usr/local/bin/sdr_df"]
