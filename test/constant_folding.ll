; RUN: opt -load-pass-plugin=%S/../build/LLVMOptPasses.so -passes="custom-constant-fold" -S %s | FileCheck %s
;
; Test cases for Constant Folding Pass
; These test the visitor pattern and ConstantFoldInstruction usage

; Test 1: Simple arithmetic folding
; CHECK-LABEL: @test_arithmetic
; CHECK-NOT: add i32 5, 10
; CHECK: ret i32 15
define i32 @test_arithmetic() {
entry:
    %result = add i32 5, 10
    ret i32 %result
}

; Test 2: Chained constant operations
; CHECK-LABEL: @test_chained
; CHECK-NOT: add i32 2, 3
; CHECK-NOT: mul i32
; CHECK: ret i32 50
define i32 @test_chained() {
entry:
    %a = add i32 2, 3          ; Should fold to 5
    %b = mul i32 %a, 10        ; Should fold to 50
    ret i32 %b
}

; Test 3: Comparison folding
; CHECK-LABEL: @test_comparison
; CHECK-NOT: icmp sgt i32 10, 5
; CHECK: ret i1 true
define i1 @test_comparison() {
entry:
    %cmp = icmp sgt i32 10, 5
    ret i1 %cmp
}

; Test 4: Select with constant condition
; CHECK-LABEL: @test_select
; CHECK-NOT: select i1 true
; CHECK: ret i32 42
define i32 @test_select() {
entry:
    %result = select i1 true, i32 42, i32 0
    ret i32 %result
}

; Test 5: Cast instruction folding
; CHECK-LABEL: @test_cast
; CHECK-NOT: zext i8 255 to i32
; CHECK: ret i32 255
define i32 @test_cast() {
entry:
    %result = zext i8 255 to i32
    ret i32 %result
}

; Test 6: GEP with constant indices
; CHECK-LABEL: @test_gep
; Uses constant base and indices
@global_array = global [10 x i32] zeroinitializer

define ptr @test_gep() {
entry:
    %ptr = getelementptr [10 x i32], ptr @global_array, i64 0, i64 5
    ret ptr %ptr
}

; Test 7: Non-foldable operations (should remain)
; CHECK-LABEL: @test_non_foldable
; CHECK: %add = add i32 %x, %y
define i32 @test_non_foldable(i32 %x, i32 %y) {
entry:
    %add = add i32 %x, %y
    ret i32 %add
}

; Test 8: Division by constant (should fold)
; CHECK-LABEL: @test_division
; CHECK-NOT: sdiv i32 100, 5
; CHECK: ret i32 20
define i32 @test_division() {
entry:
    %result = sdiv i32 100, 5
    ret i32 %result
}

; Test 9: Floating point folding
; CHECK-LABEL: @test_float
; CHECK-NOT: fadd double 1.5
; CHECK: ret double 4.000000e+00
define double @test_float() {
entry:
    %result = fadd double 1.5, 2.5
    ret double %result
}

; Test 10: Bitwise operations
; CHECK-LABEL: @test_bitwise
; CHECK-NOT: and i32 15, 7
; CHECK: ret i32 7
define i32 @test_bitwise() {
entry:
    %result = and i32 15, 7
    ret i32 %result
}
