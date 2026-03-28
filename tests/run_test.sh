#!/bin/bash

gnatmake -D obj simple_ada_test.adb

# Run the test and capture output
OUTPUT=$(LD_PRELOAD=../Linux/libdegas.so ./simple_ada_test 2>&1)

# Expected output
EXPECTED="0 : SIM_CONTEXT_DEBUG
Main: Starting simple Ada test with DEGAS
Worker: Starting
Worker: Finished
Worker: In Done accept 1
Worker: After Done accept 1
Worker: In Done accept 2
Worker: After Done accept 2
Worker: In Done accept 3
Worker: After Done accept 3
Worker: In Done accept 4
Worker: After Done accept 4
Worker: In Done accept 5
Worker: After Done accept 5
Worker: In Done accept 6
Worker: After Done accept 6
Main: Test completed

--- FINAL CONTEXT STATUS ---
Monotonic Time: 3.010000000
Active: 1, Waiting: 0
Context 0: finished=0 waiter=0 wait=0.000000000
Context 1: finished=1 waiter=0 wait=0.000000000"

# Display the output
echo "$OUTPUT"

# Check if output matches expected
if [ "$OUTPUT" = "$EXPECTED" ]; then
    echo ""
    echo "TEST PASSED"
    exit 0
else
    echo ""
    echo "TEST FAILED"
    echo "Expected output did not match actual output"
    exit 1
fi
