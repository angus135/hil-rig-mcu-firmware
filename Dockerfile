FROM ubuntu:24.04 AS base

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    bash \
    git \
    openssh-client \
    ca-certificates \
    build-essential \
    cmake \
    ninja-build \
    curl \
    wget \
    procps \
    tar \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /hil-rig-mcu-firmware

# CI image: build & run tests, run clang-tidy/clang-format
FROM base AS ci

RUN apt-get update && apt-get install -y --no-install-recommends \
    clang \
    clang-format \
    clang-tidy \
    clangd \
    && rm -rf /var/lib/apt/lists/*

# Dev image: everything in ci + gdb for debugging tests + python
FROM base AS dev

RUN apt-get update && apt-get install -y --no-install-recommends \
    clang \
    clang-format \
    clang-tidy \
    clangd \
    gdb \
    python3 \
    python3-pip \
    && rm -rf /var/lib/apt/lists/*

# Default to an interactive shell in dev
CMD ["bash"]
