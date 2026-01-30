; RUN: opt -load-pass-plugin=%S/../build/LLVMOptPasses.so -passes="custom-loop-unroll" -S %s | FileCheck %s
;
; Test cases for Loop Unrolling Pass
; These test SCEV trip count computation and unroll strategies

; Test 1: Simple loop with known trip count (should fully unroll)
; CHECK-LABEL: @test_simple_loop
; CHECK-NOT: br i1
; The loop should be fully unrolled
define i32 @test_simple_loop() {
entry:
    br label %loop

loop:
    %i = phi i32 [ 0, %entry ], [ %next, %loop ]
    %sum = phi i32 [ 0, %entry ], [ %add, %loop ]
    %add = add i32 %sum, %i
    %next = add i32 %i, 1
    %cond = icmp slt i32 %next, 4
    br i1 %cond, label %loop, label %exit

exit:
    ret i32 %add
}

; Test 2: Loop with larger trip count (should partially unroll)
; CHECK-LABEL: @test_partial_unroll
define i32 @test_partial_unroll() {
entry:
    br label %loop

loop:
    %i = phi i32 [ 0, %entry ], [ %next, %loop ]
    %sum = phi i32 [ 0, %entry ], [ %add, %loop ]
    %add = add i32 %sum, %i
    %next = add i32 %i, 1
    %cond = icmp slt i32 %next, 100
    br i1 %cond, label %loop, label %exit

exit:
    ret i32 %add
}

; Test 3: Loop with unknown trip count (runtime unroll candidate)
; CHECK-LABEL: @test_runtime_unroll
define i32 @test_runtime_unroll(i32 %n) {
entry:
    br label %loop

loop:
    %i = phi i32 [ 0, %entry ], [ %next, %loop ]
    %sum = phi i32 [ 0, %entry ], [ %add, %loop ]
    %add = add i32 %sum, %i
    %next = add i32 %i, 1
    %cond = icmp slt i32 %next, %n
    br i1 %cond, label %loop, label %exit

exit:
    ret i32 %add
}

; Test 4: Nested loops (inner should be processed first)
; CHECK-LABEL: @test_nested_loops
define i32 @test_nested_loops() {
entry:
    br label %outer

outer:
    %i = phi i32 [ 0, %entry ], [ %i.next, %outer.latch ]
    br label %inner

inner:
    %j = phi i32 [ 0, %outer ], [ %j.next, %inner ]
    %sum.inner = phi i32 [ 0, %outer ], [ %add, %inner ]
    %idx = add i32 %i, %j
    %add = add i32 %sum.inner, %idx
    %j.next = add i32 %j, 1
    %inner.cond = icmp slt i32 %j.next, 4
    br i1 %inner.cond, label %inner, label %outer.latch

outer.latch:
    %i.next = add i32 %i, 1
    %outer.cond = icmp slt i32 %i.next, 4
    br i1 %outer.cond, label %outer, label %exit

exit:
    %result = phi i32 [ %add, %outer.latch ]
    ret i32 %result
}

; Test 5: Loop with side effects (should not unroll by default)
; CHECK-LABEL: @test_side_effects
define void @test_side_effects(ptr %ptr) {
entry:
    br label %loop

loop:
    %i = phi i32 [ 0, %entry ], [ %next, %loop ]
    %addr = getelementptr i32, ptr %ptr, i32 %i
    store i32 %i, ptr %addr
    %next = add i32 %i, 1
    %cond = icmp slt i32 %next, 4
    br i1 %cond, label %loop, label %exit

exit:
    ret void
}

; Test 6: Loop with complex body
; CHECK-LABEL: @test_complex_body
define i32 @test_complex_body(ptr %a, ptr %b) {
entry:
    br label %loop

loop:
    %i = phi i32 [ 0, %entry ], [ %next, %loop ]
    %sum = phi i32 [ 0, %entry ], [ %new.sum, %loop ]
    
    ; Load from array a
    %a.ptr = getelementptr i32, ptr %a, i32 %i
    %a.val = load i32, ptr %a.ptr
    
    ; Load from array b
    %b.ptr = getelementptr i32, ptr %b, i32 %i
    %b.val = load i32, ptr %b.ptr
    
    ; Compute
    %mul = mul i32 %a.val, %b.val
    %new.sum = add i32 %sum, %mul
    
    %next = add i32 %i, 1
    %cond = icmp slt i32 %next, 8
    br i1 %cond, label %loop, label %exit

exit:
    ret i32 %new.sum
}
