#!/bin/bash

gnatmake -D obj sync_test.adb

OUTPUT=$(LD_PRELOAD=../Linux/libdegas.so ./sync_test 2>&1)

EXPECTED="0 : SIM_CONTEXT_DEBUG
Main: starting sync test
Worker 1:  Phase 1 sem   + 1
Worker 2:  Phase 1 sem   + 2
Worker 3:  Phase 1 sem   + 3
Worker 4:  Phase 1 sem   + 4
Worker 1:  Phase 2 barrier passed
Worker 2:  Phase 2 barrier passed
Worker 3:  Phase 2 barrier passed
Worker 4:  Phase 2 barrier passed
Dispatcher: queued  worker 1
Dispatcher: execute worker 1 + 10
Scorer:     claimed  worker 1 + 1000
Dispatcher: queued  worker 2
Dispatcher: execute worker 2 + 20
Worker 2:  Phase 4 no claim
Dispatcher: queued  worker 3
Dispatcher: execute worker 3 + 30
Worker 3:  Phase 4 no claim
Dispatcher: queued  worker 4
Dispatcher: execute worker 4 + 40
Worker 4:  Phase 4 no claim
CHECKSUM: 1110

--- FINAL CONTEXT STATUS ---
Monotonic Time: 0.810000000
Active: 1, Waiting: 0
Context 0: finished=0 waiter=0 wait=0.000000000
Context 1: finished=1 waiter=0 wait=0.000000000
Context 2: finished=1 waiter=0 wait=0.000000000
Context 3: finished=1 waiter=0 wait=0.000000000
Context 4: finished=1 waiter=0 wait=0.000000000
Context 5: finished=1 waiter=0 wait=0.000000000
Context 6: finished=1 waiter=0 wait=0.000000000"

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
