#!/usr/bin/env bash

set -e

if [ ! -f config-host.mak ]
then
  ./configure --target-list=x86_64-softmmu --enable-kvm
fi

time make -j "$(nproc)" V=1
