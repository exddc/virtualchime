# Buildroot build environment for Virtual Chime
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    git \
    wget \
    cpio \
    unzip \
    rsync \
    bc \
    libncurses-dev \
    flex \
    bison \
    libssl-dev \
    libelf-dev \
    python3 \
    python3-pip \
    python3-setuptools \
    file \
    locales \
    sudo \
    && rm -rf /var/lib/apt/lists/*

RUN locale-gen en_US.UTF-8
ENV LANG=en_US.UTF-8
ENV LC_ALL=en_US.UTF-8
RUN useradd -m -s /bin/bash -u 1000 builder && \
    echo "builder ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

WORKDIR /home/builder
USER builder
