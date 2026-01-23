#!/bin/bash

# Run the test and capture output
OUTPUT=$(LD_PRELOAD=./Linux/libdegas.so ./simple_ada_test 2>&1)

# Expected output
EXPECTED="Main: Starting simple Ada test with DEGAS
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
Main: Test completed"

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
