#!/bin/bash
#
# benchmark.sh - Run performance benchmarks for LLVM passes
#
# Usage: ./benchmark.sh <path_to_plugin.so>
#
# This script compares execution time of code compiled with and without
# the custom optimization passes to measure performance improvement.
#

set -e

PLUGIN_PATH="${1:-./build/LLVMOptPasses.so}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_DIR="${SCRIPT_DIR}/../test"
BUILD_DIR="${SCRIPT_DIR}/../benchmark_build"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo "========================================"
echo "LLVM Custom Passes Benchmark"
echo "========================================"

# Check prerequisites
if [ ! -f "${PLUGIN_PATH}" ]; then
    echo -e "${RED}Error: Plugin not found at ${PLUGIN_PATH}${NC}"
    exit 1
fi

if [ ! -f "${TEST_DIR}/benchmark.c" ]; then
    echo -e "${RED}Error: benchmark.c not found${NC}"
    exit 1
fi

# Create build directory
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo ""
echo -e "${BLUE}Step 1: Compiling baseline (no custom passes)${NC}"
echo "----------------------------------------"

# Baseline: -O1 without custom passes
clang -O1 -emit-llvm -S "${TEST_DIR}/benchmark.c" -o baseline.ll
llc baseline.ll -O1 -o baseline.s
clang baseline.s -o baseline -lm

echo "Baseline compiled successfully"

echo ""
echo -e "${BLUE}Step 2: Compiling with custom passes${NC}"
echo "----------------------------------------"

# With custom passes: -O0 to IR, then custom passes, then codegen
clang -O0 -emit-llvm -S "${TEST_DIR}/benchmark.c" -o unoptimized.ll

# Run custom passes
opt -load-pass-plugin="${PLUGIN_PATH}" \
    -passes="custom-constant-fold,custom-redundancy-elim,custom-loop-unroll" \
    unoptimized.ll -S -o custom_optimized.ll

# Generate code
llc custom_optimized.ll -O1 -o custom_optimized.s
clang custom_optimized.s -o custom_optimized -lm

echo "Custom optimized version compiled successfully"

echo ""
echo -e "${BLUE}Step 3: Running benchmarks${NC}"
echo "----------------------------------------"

# Number of benchmark runs
RUNS=5

run_benchmark() {
    local binary="$1"
    local name="$2"
    local times=()
    
    echo "Running ${name}..."
    
    for ((i=1; i<=RUNS; i++)); do
        # Extract time from benchmark output
        output=$("./${binary}" 2>&1)
        time_sec=$(echo "$output" | grep -oP 'completed in \K[0-9.]+')
        times+=("$time_sec")
        echo "  Run ${i}: ${time_sec}s"
    done
    
    # Calculate average
    total=0
    for t in "${times[@]}"; do
        total=$(echo "$total + $t" | bc)
    done
    avg=$(echo "scale=3; $total / $RUNS" | bc)
    
    echo "  Average: ${avg}s"
    echo "$avg"
}

echo ""
BASELINE_TIME=$(run_benchmark "baseline" "Baseline (-O1)")

echo ""
CUSTOM_TIME=$(run_benchmark "custom_optimized" "Custom Optimized")

echo ""
echo "========================================"
echo "Results"
echo "========================================"

# Calculate improvement
if command -v bc &> /dev/null; then
    IMPROVEMENT=$(echo "scale=2; (($BASELINE_TIME - $CUSTOM_TIME) / $BASELINE_TIME) * 100" | bc)
    SPEEDUP=$(echo "scale=2; $BASELINE_TIME / $CUSTOM_TIME" | bc)
    
    echo "Baseline time:     ${BASELINE_TIME}s"
    echo "Custom opt time:   ${CUSTOM_TIME}s"
    echo ""
    
    if (( $(echo "$IMPROVEMENT > 0" | bc -l) )); then
        echo -e "${GREEN}Performance improvement: ${IMPROVEMENT}%${NC}"
        echo -e "${GREEN}Speedup: ${SPEEDUP}x${NC}"
    else
        echo -e "${YELLOW}Performance change: ${IMPROVEMENT}%${NC}"
        echo -e "${YELLOW}Speedup: ${SPEEDUP}x${NC}"
    fi
else
    echo "Baseline time:     ${BASELINE_TIME}s"
    echo "Custom opt time:   ${CUSTOM_TIME}s"
    echo "(Install 'bc' for percentage calculation)"
fi

echo ""
echo "========================================"
echo "Analysis Statistics"
echo "========================================"

echo ""
echo "IR Statistics (unoptimized):"
echo "  Instructions: $(grep -c '=' unoptimized.ll || echo 0)"
echo "  Basic blocks: $(grep -c '^[a-z].*:' unoptimized.ll || echo 0)"

echo ""
echo "IR Statistics (custom optimized):"
echo "  Instructions: $(grep -c '=' custom_optimized.ll || echo 0)"
echo "  Basic blocks: $(grep -c '^[a-z].*:' custom_optimized.ll || echo 0)"

# Instruction reduction
BEFORE=$(grep -c '=' unoptimized.ll || echo 0)
AFTER=$(grep -c '=' custom_optimized.ll || echo 0)
if command -v bc &> /dev/null && [ "$BEFORE" -gt 0 ]; then
    REDUCTION=$(echo "scale=2; (($BEFORE - $AFTER) / $BEFORE) * 100" | bc)
    echo ""
    echo -e "Instruction reduction: ${GREEN}${REDUCTION}%${NC}"
fi

echo ""
echo "Build artifacts in: ${BUILD_DIR}"
