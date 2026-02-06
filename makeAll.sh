#!/usr/bin/env bash

set -e

BUILD_DIR="build"

echo "==============================="
echo " Clean + Full Rebuild"
echo "==============================="

if [ -d "$BUILD_DIR" ]; then
    echo "Removing build directory..."
    rm -rf "$BUILD_DIR"
fi

echo "Rebuilding..."
./make.sh "$@"

echo "==============================="
echo " Full rebuild OK"
echo "==============================="

