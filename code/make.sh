#!/usr/bin/env bash
BUILD_DIR="build"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake -G Ninja ..
ninja
