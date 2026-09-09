FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential \
      cmake \
      git \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /work/cs2-anticheat
