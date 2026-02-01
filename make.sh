#!/bin/bash

set -e

RUN_TESTS=0

# Parse args
for arg in "$@"; do
    case $arg in
        -includeUnitTests)
            RUN_TESTS=1
            shift
            ;;
    esac
done

echo "==============================="
echo " Building Process Analyzer"
echo "==============================="

mkdir -p build
cd build

echo "Running CMake..."
cmake ..

echo "Building..."
make -j$(nproc)

if [ $RUN_TESTS -eq 1 ]; then
    echo "==============================="
    echo " Running Unit Tests"
    echo "==============================="

    ctest --output-on-failure --verbose
else
    echo "Unit tests skipped"
fi

