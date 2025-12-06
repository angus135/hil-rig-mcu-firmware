FROM alpine:3.20 AS base

RUN apk add --no-cache \
    bash \
    git \
    openssh \
    ca-certificates \
    gcc \
    g++ \
    make \
    cmake \
    ninja

WORKDIR /hil-rig-mcu-firmware

# CI image: build & run tests, run clang-tidy/clang-format
FROM base AS ci

RUN apk add --no-cache \
    clang \
    clang-extra-tools

# Dev image: everything in ci + gdb for debugging tests
FROM base AS dev

RUN apk add --no-cache \
    clang \
    clang-extra-tools \
    gdb \
    python3 \
    py3-pip

# Default to an interactive shell in dev
CMD ["bash"]
