#!/bin/bash
#
# run_tests.sh - Run LLVM pass tests
#
# Usage: ./run_tests.sh <path_to_plugin.so>
#

set -e

PLUGIN_PATH="${1:-./build/LLVMOptPasses.so}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_DIR="${SCRIPT_DIR}/../test"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "========================================"
echo "LLVM Custom Passes Test Suite"
echo "========================================"
echo "Plugin: ${PLUGIN_PATH}"
echo ""

# Check if plugin exists
if [ ! -f "${PLUGIN_PATH}" ]; then
    echo -e "${RED}Error: Plugin not found at ${PLUGIN_PATH}${NC}"
    echo "Build the project first with: mkdir build && cd build && cmake .. && make"
    exit 1
fi

# Check if opt is available
if ! command -v opt &> /dev/null; then
    echo -e "${RED}Error: 'opt' not found in PATH${NC}"
    echo "Make sure LLVM is installed and in your PATH"
    exit 1
fi

LLVM_VERSION=$(opt --version | grep -oP 'LLVM version \K[0-9]+' || echo "unknown")
echo "LLVM Version: ${LLVM_VERSION}"
echo ""

PASSED=0
FAILED=0

run_test() {
    local test_name="$1"
    local test_file="$2"
    local passes="$3"
    local description="$4"
    
    echo -n "Testing ${test_name}... "
    
    if opt -load-pass-plugin="${PLUGIN_PATH}" -passes="${passes}" -S "${test_file}" > /dev/null 2>&1; then
        echo -e "${GREEN}PASSED${NC}"
        ((PASSED++))
    else
        echo -e "${RED}FAILED${NC}"
        echo "  Command: opt -load-pass-plugin=${PLUGIN_PATH} -passes=\"${passes}\" -S ${test_file}"
        ((FAILED++))
    fi
}

echo "----------------------------------------"
echo "Constant Folding Tests"
echo "----------------------------------------"

if [ -f "${TEST_DIR}/constant_folding.ll" ]; then
    run_test "Constant Folding Basic" "${TEST_DIR}/constant_folding.ll" "custom-constant-fold" "Basic constant folding operations"
else
    echo -e "${YELLOW}Warning: constant_folding.ll not found${NC}"
fi

echo ""
echo "----------------------------------------"
echo "Loop Unrolling Tests"
echo "----------------------------------------"

if [ -f "${TEST_DIR}/loop_unrolling.ll" ]; then
    run_test "Loop Unrolling Basic" "${TEST_DIR}/loop_unrolling.ll" "custom-loop-unroll" "Basic loop unrolling"
else
    echo -e "${YELLOW}Warning: loop_unrolling.ll not found${NC}"
fi

echo ""
echo "----------------------------------------"
echo "Redundancy Elimination Tests"
echo "----------------------------------------"

if [ -f "${TEST_DIR}/redundancy_elimination.ll" ]; then
    run_test "Redundancy Analysis" "${TEST_DIR}/redundancy_elimination.ll" "print<custom-redundancy>" "Redundancy analysis output"
    run_test "Redundancy Elimination" "${TEST_DIR}/redundancy_elimination.ll" "custom-redundancy-elim" "Redundancy elimination"
else
    echo -e "${YELLOW}Warning: redundancy_elimination.ll not found${NC}"
fi

echo ""
echo "----------------------------------------"
echo "Combined Pipeline Tests"
echo "----------------------------------------"

if [ -f "${TEST_DIR}/constant_folding.ll" ]; then
    run_test "Combined Pipeline" "${TEST_DIR}/constant_folding.ll" "custom-optimize" "All passes combined"
fi

echo ""
echo "========================================"
echo "Test Summary"
echo "========================================"
echo -e "Passed: ${GREEN}${PASSED}${NC}"
echo -e "Failed: ${RED}${FAILED}${NC}"

if [ ${FAILED} -gt 0 ]; then
    exit 1
fi

exit 0
