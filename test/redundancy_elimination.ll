; RUN: opt -load-pass-plugin=%S/../build/LLVMOptPasses.so -passes="print<custom-redundancy>" -S %s 2>&1 | FileCheck %s --check-prefix=ANALYSIS
; RUN: opt -load-pass-plugin=%S/../build/LLVMOptPasses.so -passes="custom-redundancy-elim" -S %s | FileCheck %s
;
; Test cases for Redundancy Analysis and Elimination
; Tests GVN-based value numbering and dominance-based availability

; Test 1: Simple redundant computation in same block
; ANALYSIS: REDUNDANT
; CHECK-LABEL: @test_same_block
; CHECK: %a = add i32 %x, %y
; CHECK-NOT: %b = add i32 %x, %y
; CHECK: ret i32 %a
define i32 @test_same_block(i32 %x, i32 %y) {
entry:
    %a = add i32 %x, %y    ; Original computation
    %b = add i32 %x, %y    ; Redundant - same as %a
    ret i32 %b
}

; Test 2: Redundant computation across dominating block
; CHECK-LABEL: @test_dominator
; CHECK: %a = add i32 %x, %y
; CHECK-NOT: %b = add i32 %x, %y
define i32 @test_dominator(i32 %x, i32 %y, i1 %cond) {
entry:
    %a = add i32 %x, %y    ; Computed in dominating block
    br i1 %cond, label %then, label %else

then:
    %b = add i32 %x, %y    ; Redundant - %a dominates this
    br label %merge

else:
    %c = add i32 %x, %y    ; Redundant - %a dominates this too
    br label %merge

merge:
    %result = phi i32 [ %b, %then ], [ %c, %else ]
    ret i32 %result
}

; Test 3: Commutative operations - (a+b) == (b+a)
; CHECK-LABEL: @test_commutative
; CHECK: %a = add i32 %x, %y
; CHECK-NOT: %b = add i32 %y, %x
; CHECK: ret i32 %a
define i32 @test_commutative(i32 %x, i32 %y) {
entry:
    %a = add i32 %x, %y    ; x + y
    %b = add i32 %y, %x    ; y + x - should be detected as redundant
    ret i32 %b
}

; Test 4: Non-redundant computation (different operands)
; CHECK-LABEL: @test_non_redundant
; CHECK: %a = add i32 %x, %y
; CHECK: %b = add i32 %x, %z
define i32 @test_non_redundant(i32 %x, i32 %y, i32 %z) {
entry:
    %a = add i32 %x, %y
    %b = add i32 %x, %z    ; Different operand - not redundant
    %c = add i32 %a, %b
    ret i32 %c
}

; Test 5: Non-dominating computation (not available)
; The computation in 'then' doesn't dominate 'else'
; CHECK-LABEL: @test_no_dominance
; CHECK: then:
; CHECK: %a = add i32 %x, %y
; CHECK: else:
; CHECK: %b = add i32 %x, %y
define i32 @test_no_dominance(i32 %x, i32 %y, i1 %cond) {
entry:
    br i1 %cond, label %then, label %else

then:
    %a = add i32 %x, %y    ; Only dominates merge via then path
    br label %merge

else:
    %b = add i32 %x, %y    ; NOT redundant - %a doesn't dominate
    br label %merge

merge:
    %result = phi i32 [ %a, %then ], [ %b, %else ]
    ret i32 %result
}

; Test 6: Multiple redundancies in sequence
; CHECK-LABEL: @test_multiple
; CHECK: %a = add i32 %x, %y
; CHECK-NOT: %b = add i32 %x, %y
; CHECK-NOT: %c = add i32 %x, %y
; CHECK: %d = mul i32 %a, %a
define i32 @test_multiple(i32 %x, i32 %y) {
entry:
    %a = add i32 %x, %y
    %b = add i32 %x, %y    ; Redundant
    %c = add i32 %x, %y    ; Redundant
    %d = mul i32 %b, %c    ; Uses redundant values
    ret i32 %d
}

; Test 7: Redundant multiplication
; CHECK-LABEL: @test_mul_redundant
; CHECK: %a = mul i32 %x, %y
; CHECK-NOT: %b = mul i32 %x, %y
define i32 @test_mul_redundant(i32 %x, i32 %y) {
entry:
    %a = mul i32 %x, %y
    %b = mul i32 %x, %y    ; Redundant
    %c = add i32 %a, %b
    ret i32 %c
}

; Test 8: Type matters - different types are not redundant
; CHECK-LABEL: @test_types
; CHECK: %a = add i32 %x, %y
; CHECK: %b = add nsw i32 %x, %y
; Note: nsw flag creates different semantics, might be kept
define i32 @test_types(i32 %x, i32 %y) {
entry:
    %a = add i32 %x, %y
    %b = add nsw i32 %x, %y    ; Has nsw flag - different instruction
    %c = add i32 %a, %b
    ret i32 %c
}

; Test 9: Complex expression chains
; CHECK-LABEL: @test_chain
; CHECK: %a = add i32 %x, %y
; CHECK: %b = mul i32 %a, %z
; CHECK-NOT: %c = add i32 %x, %y
; CHECK-NOT: %d = mul i32 %a, %z
define i32 @test_chain(i32 %x, i32 %y, i32 %z) {
entry:
    %a = add i32 %x, %y
    %b = mul i32 %a, %z
    %c = add i32 %x, %y        ; Redundant with %a
    %d = mul i32 %c, %z        ; Redundant with %b (after %c -> %a)
    %result = add i32 %b, %d
    ret i32 %result
}

; Test 10: Bitwise operations
; CHECK-LABEL: @test_bitwise_redundant
; CHECK: %a = and i32 %x, %y
; CHECK-NOT: %b = and i32 %y, %x
define i32 @test_bitwise_redundant(i32 %x, i32 %y) {
entry:
    %a = and i32 %x, %y    ; AND is commutative
    %b = and i32 %y, %x    ; Redundant
    %c = or i32 %a, %b
    ret i32 %c
}
