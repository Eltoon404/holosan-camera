ARG UPDATE_CA=no
ARG HOLOSCAN_SDK_VERSION=3.5.0
ARG HOLOSCAN_SDK_IMAGE=nvcr.io/nvidia/clara-holoscan/holoscan:v${HOLOSCAN_SDK_VERSION}-dgpu
FROM ${HOLOSCAN_SDK_IMAGE} as base-yes
ARG UID=1000
ARG GID=1000
ARG USERNAME=dev
ARG TARGETARCH
# When building aarch64 we are forced to set VCPKG_FORCE_SYSTEM_BINARIES
# For this reason we need to install following dependencies to the system
# Required from packages: tiff, Qt6Xcb
# Install dependencies required for building on arm
RUN if [ "${TARGETARCH}" = "arm64" ]; then \
        apt update && apt install -y \
        clang \
        libxcursor-dev; \
    fi
# Install CMake version 3.29.2
RUN if [ "${TARGETARCH}" = "arm64" ]; then \
        cd /tmp; \
        wget https://github.com/Kitware/CMake/releases/download/v3.29.2/cmake-3.29.2.tar.gz; \
        tar -xzf cmake-3.29.2.tar.gz; \
        cd cmake-3.29.2; \
        ./bootstrap; \
        make -j${nproc}; \
        make install; \
    fi
# Install vcpkg dependencies
RUN apt update && apt install -y \
    curl \
    zip \
    unzip \
    tar
# Install qt6 dependencies
RUN apt update && apt install -y \
    autoconf \
    pkgconf \
    libtool \
    autoconf-archive \
    liblzma-dev \
    libxrandr-dev \
    '^libxcb.*-dev' libx11-xcb-dev libglu1-mesa-dev libxrender-dev libxi-dev libxkbcommon-dev \
    libxkbcommon-x11-dev libegl1-mesa-dev
# Install DBus as it is used for the IPC
RUN apt-get update && apt-get install -y dbus
# Install opencv dependencies
RUN apt-get update && apt-get install -y bison libxrandr-dev libxtst-dev

RUN apt install -y locales fontconfig gdb valgrind
RUN locale-gen en_US.UTF-8
RUN apt install -y libqrencode-dev
ENV CUDA_PATH=/usr/local/cuda/bin
ENV LANG=en_US.UTF-8
ENV HOLOSCAN_LOG_LEVEL=OFF
USER ${USERNAME}
WORKDIR /home/$USERNAME
