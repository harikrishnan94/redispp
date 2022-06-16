# See here for image contents: https://github.com/microsoft/vscode-dev-containers/tree/v0.195.0/containers/cpp/.devcontainer/base.Dockerfile
# [Choice] Debian / Ubuntu version (use Debian 11/9, Ubuntu 18.04/21.04 on local arm64/Apple Silicon): debian-11, debian-10, debian-9, ubuntu-21.04, ubuntu-20.04, ubuntu-18.04
ARG VARIANT=ubuntu-22.04
FROM mcr.microsoft.com/vscode/devcontainers/cpp:0-${VARIANT}

RUN apt update && export DEBIAN_FRONTEND=noninteractive \
    && apt -y install --no-install-recommends software-properties-common cmake-format ccache \
    && add-apt-repository ppa:ubuntu-toolchain-r/test \
    && apt update \
    && apt -y install g++-12

# Install CMake
ARG CMAKE_VERSION=3.23.2
RUN cd /tmp && \
    wget -c \
    https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.sh \
    -O cmake.sh && \
    chmod +x cmake.sh && ./cmake.sh --prefix=/usr/local --skip-license && rm cmake.sh

# Install Ninja
ARG NINJA_VERSION=1.10.2
RUN cd /tmp && wget -c \
    https://github.com/ninja-build/ninja/releases/download/v${NINJA_VERSION}/ninja-linux.zip -O ninja.zip && \
    unzip ninja.zip && rm ninja.zip && chmod +x ninja && mv ninja /usr/local/bin

# VCPKG
# Add vcpkg to PATH
ENV VCPKG_FORCE_SYSTEM_BINARIES=1
ENV VCPKG_ROOT=/usr/local/vcpkg
ENV PATH="${PATH}:${VCPKG_ROOT}"

ARG VCPKG_RELEASE=2022.05.10
RUN wget -c https://github.com/microsoft/vcpkg/archive/refs/tags/${VCPKG_RELEASE}.zip -O vcpkg.zip \
    && unzip -qq vcpkg.zip && mv vcpkg-${VCPKG_RELEASE} vcpkg \
    && cd vcpkg && ./bootstrap-vcpkg.sh && rm ../vcpkg.zip

RUN vcpkg integrate install && vcpkg integrate bash

# Install Clangd
ARG CLANGD_VERSION=14.0.3
RUN wget -c https://github.com/clangd/clangd/releases/download/14.0.3/clangd-linux-${CLANGD_VERSION}.zip -O clangd.zip \
    && unzip -qq clangd.zip && cp -r clangd_${CLANGD_VERSION}/* /usr/local/ \
    && rm clangd.zip

# Install Boost
RUN vcpkg install boost fmt

