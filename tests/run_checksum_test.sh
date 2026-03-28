#!/bin/bash

gnatmake -D obj checksum_test.adb

OUTPUT=$(LD_PRELOAD=../Linux/libdegas.so ./checksum_test 2>&1)

EXPECTED="0 : SIM_CONTEXT_DEBUG
Main: starting checksum test
Collector: received 15 from worker 1
Collector: received 30 from worker 2
Collector: received 45 from worker 3
Collector: received 60 from worker 4
CHECKSUM: 150

--- FINAL CONTEXT STATUS ---
Monotonic Time: 2.010000000
Active: 1, Waiting: 0
Context 0: finished=0 waiter=0 wait=0.000000000
Context 1: finished=1 waiter=0 wait=0.000000000
Context 2: finished=1 waiter=0 wait=0.000000000
Context 3: finished=1 waiter=0 wait=0.000000000
Context 4: finished=1 waiter=0 wait=0.000000000
Context 5: finished=1 waiter=0 wait=0.000000000"

echo "$OUTPUT"

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
