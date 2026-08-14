# Copyright (c) 2026 RAKwireless Technology Limited
# SPDX-License-Identifier: Apache-2.0
#
# Development image: Zephyr SDK 0.17.4 + west/cmake/ninja.
# Mount the west workspace (zephyr + BSP + modules/lib/rak-fw) at /workdir.

FROM ubuntu:22.04

ARG ZEPHYR_SDK_VERSION=0.17.4
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
	build-essential \
	ccache \
	cmake \
	device-tree-compiler \
	dfu-util \
	file \
	g++-multilib \
	gcc-multilib \
	git \
	ninja-build \
	python3 \
	python3-dev \
	python3-pip \
	python3-setuptools \
	python3-wheel \
	wget \
	xz-utils \
	&& rm -rf /var/lib/apt/lists/*

RUN pip3 install --no-cache-dir west pyelftools

WORKDIR /opt
RUN wget -q "https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZEPHYR_SDK_VERSION}/zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-x86_64.tar.xz" \
	&& tar -xf "zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-x86_64.tar.xz" \
	&& rm "zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-x86_64.tar.xz" \
	&& "./zephyr-sdk-${ZEPHYR_SDK_VERSION}/setup.sh" -t arm-zephyr-eabi -h -c

ENV ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk-${ZEPHYR_SDK_VERSION}
ENV ZEPHYR_TOOLCHAIN_VARIANT=zephyr
ENV PATH="${ZEPHYR_SDK_INSTALL_DIR}/arm-zephyr-eabi/bin:${PATH}"

WORKDIR /workdir
COPY docker/entrypoint.sh /usr/local/bin/rak-fw-entrypoint
RUN chmod +x /usr/local/bin/rak-fw-entrypoint

ENTRYPOINT ["/usr/local/bin/rak-fw-entrypoint"]
CMD ["bash"]
