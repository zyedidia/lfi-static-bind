#!/bin/sh

# Build sandbox/stub.elf and sandbox/host.so
make -C sandbox

./build/lfi-static-bind sandbox/stub.elf sandbox/host.so -o host_combined.so
