#!/bin/bash

set -e

RUN_UNIT=0
RUN_INTEGRATION=0

# Parse args
for arg in "$@"; do
    case $arg in
        -includeUnitTests)
            RUN_UNIT=1
            ;;
        -includeIntegrationTests)
            RUN_INTEGRATION=1
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

# Unit tests
if [ $RUN_UNIT -eq 1 ]; then
    echo "==============================="
    echo " Running Unit Tests"
    echo "==============================="

    ctest --output-on-failure --verbose
else
    echo "Unit tests skipped"
fi

# Integration tests
if [ $RUN_INTEGRATION -eq 1 ]; then
    echo "==============================="
    echo " Running Integration Tests"
    echo "==============================="
    
    cd ..
    tests/integration/run_all.sh
else
    echo "Integration tests skipped"
fi
