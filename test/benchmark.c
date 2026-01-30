/**
 * benchmark.c - Benchmark for Custom LLVM Optimization Passes
 * 
 * This benchmark contains code patterns that benefit from:
 * 1. Constant folding
 * 2. Loop unrolling
 * 3. Redundancy elimination
 * 
 * Compile and run:
 *   clang -O0 -emit-llvm -S benchmark.c -o benchmark.ll
 *   opt -load-pass-plugin=./LLVMOptPasses.so -passes="custom-optimize" benchmark.ll -o optimized.ll
 *   llc optimized.ll -o optimized.s
 *   clang optimized.s -o benchmark_optimized
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define ARRAY_SIZE 1000
#define ITERATIONS 1000000

// Test 1: Constant folding opportunities
// The compiler should fold SIZE_FACTOR at compile time
#define BASE_SIZE 100
#define SIZE_FACTOR (BASE_SIZE * 10)  // Should fold to 1000

int constant_folding_test(void) {
    int result = 0;
    
    // These should be folded at compile time
    int a = 10 + 20;           // -> 30
    int b = a * 2;             // -> 60 (after folding a)
    int c = b / 3;             // -> 20
    int d = (5 * 4) + (6 * 3); // -> 38
    
    // Conditional with constant
    if (100 > 50) {  // Always true - should simplify
        result = a + b + c + d;
    }
    
    return result;
}

// Test 2: Loop unrolling candidate - small known trip count
int loop_unroll_small(int* array) {
    int sum = 0;
    
    // Trip count = 8, should fully unroll
    for (int i = 0; i < 8; i++) {
        sum += array[i];
    }
    
    return sum;
}

// Test 3: Loop unrolling candidate - larger trip count
int loop_unroll_large(int* array, int size) {
    int sum = 0;
    
    // Known trip count but larger - partial unroll
    for (int i = 0; i < 64; i++) {
        sum += array[i % size];
    }
    
    return sum;
}

// Test 4: Redundancy elimination opportunities
int redundancy_test(int x, int y, int z) {
    // Redundant computations
    int a = x + y;
    int b = x * z;
    int c = x + y;  // Same as 'a' - redundant
    int d = x * z;  // Same as 'b' - redundant
    
    // Use all values to prevent DCE
    return a + b + c + d;
}

// Test 5: Combined optimization opportunities
void matrix_multiply_small(int A[4][4], int B[4][4], int C[4][4]) {
    // Small matrix multiply with constant dimensions
    // Benefits from:
    // - Loop unrolling (4 iterations)
    // - Constant folding (array indexing)
    // - Redundancy elimination (repeated index computations)
    
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            C[i][j] = 0;
            for (int k = 0; k < 4; k++) {
                C[i][j] += A[i][k] * B[k][j];
            }
        }
    }
}

// Test 6: Polynomial evaluation with common subexpressions
double polynomial_eval(double x) {
    // Horner's method would be better, but this tests redundancy elimination
    double x2 = x * x;
    double x3 = x * x * x;      // Redundant: could use x2 * x
    double x4 = x * x * x * x;  // Redundant: could use x2 * x2
    
    // Coefficients (constants to fold)
    double a = 3.0 + 2.0;  // -> 5.0
    double b = 7.0 - 3.0;  // -> 4.0
    double c = 2.0 * 1.5;  // -> 3.0
    double d = 10.0 / 2.0; // -> 5.0
    
    return a*x4 + b*x3 + c*x2 + d*x + 1.0;
}

// Test 7: Array processing with redundant bounds checks
int array_sum_with_redundancy(int* arr, int n) {
    int sum = 0;
    
    for (int i = 0; i < n; i++) {
        // Redundant computation: i < n checked in loop condition
        // But also: arr[i] access pattern
        int val = arr[i];
        int idx = i;  // Redundant copy
        
        // Redundant re-computation
        int same_val = arr[idx];  // Same as val
        
        sum += val + same_val;  // Effectively 2 * arr[i]
    }
    
    return sum;
}

// Benchmark harness
int main(int argc, char** argv) {
    // Allocate test data
    int* array = (int*)malloc(ARRAY_SIZE * sizeof(int));
    int A[4][4], B[4][4], C[4][4];
    
    // Initialize
    for (int i = 0; i < ARRAY_SIZE; i++) {
        array[i] = i % 100;
    }
    
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            A[i][j] = i + j;
            B[i][j] = i - j + 4;
        }
    }
    
    // Warm up
    volatile int result = 0;
    
    // Benchmark
    clock_t start = clock();
    
    for (int iter = 0; iter < ITERATIONS; iter++) {
        result += constant_folding_test();
        result += loop_unroll_small(array);
        result += loop_unroll_large(array, ARRAY_SIZE);
        result += redundancy_test(iter, iter + 1, iter + 2);
        matrix_multiply_small(A, B, C);
        result += (int)polynomial_eval((double)(iter % 10));
        result += array_sum_with_redundancy(array, 100);
    }
    
    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    
    printf("Benchmark completed in %.3f seconds\n", elapsed);
    printf("Result (prevent optimization): %d\n", result);
    printf("Matrix C[0][0] = %d\n", C[0][0]);
    
    free(array);
    return 0;
}
