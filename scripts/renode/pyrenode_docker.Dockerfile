FROM python:3.11-slim

# Renode portable (dotnet) + pyrenode3 dependencies.
RUN apt-get update -y >/dev/null \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        git \
        libicu-dev \
        libglib2.0-0 \
        libgtk-3-0 \
        libgdk-pixbuf-2.0-0 \
        libpango-1.0-0 \
        libcairo2 \
        libatk1.0-0 \
        libx11-6 \
        libxext6 \
        libxrender1 \
        libxrandr2 \
        libxi6 \
        libxfixes3 \
        libxcursor1 \
        libxinerama1 \
        libasound2 \
        libnss3 \
        libnspr4 \
        libdbus-1-3 \
        libexpat1 \
        libfontconfig1 \
        libfreetype6 \
        libuuid1 \
        libstdc++6 \
        xz-utils \
    >/dev/null \
    && rm -rf /var/lib/apt/lists/*

RUN python -m pip install -q "pyrenode3[all] @ git+https://github.com/antmicro/pyrenode3.git"

ARG RENODE_TARBALL_URL=https://builds.renode.io/renode-latest.linux-arm64-portable-dotnet.tar.gz

RUN curl -L -o /opt/renode-dotnet.tar.gz ${RENODE_TARBALL_URL} >/dev/null \
    && mkdir -p /opt/renode \
    && tar -xf /opt/renode-dotnet.tar.gz -C /opt/renode \
    && ln -sf /opt/renode/*/renode /opt/renode/renode

ENV PYRENODE_RUNTIME=coreclr
ENV PYRENODE_BIN=/opt/renode/renode

WORKDIR /work
