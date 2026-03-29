<img width="640" height="184" alt="image" src="https://github.com/user-attachments/assets/c8bf06c8-a9fd-4a80-9f0f-a42db3c1cfa0" />

# DEGAS — Discrete-Event GNAT Ada Simulator

Read [`degas.pdf`](docs/degas.pdf) for full background (and [`ada15--degas.pdf`](docs/ada15-degas.pdf) ).

## How it works

The Ada tasking runtime calls a small set of `pthread` routines to schedule
and synchronize tasks.  DEGAS intercepts those calls via `LD_PRELOAD` of
`libdegas.so` and replaces wall-clock time with **simulated time**, so the
entire program runs instantaneously while preserving a deterministic,
reproducible event order.

`degas.c` was written for an older `glibc` that used spinlocks; modern
`glibc` uses futexes, so the newest version of `degas.c` has adapted both for pthreads as well as for the GNAT Ada GNARL tasking runtime. .

---

## Running a test

```
cd tests
LD_PRELOAD=../Linux/libdegas.so ./<program>
```

---

## Test programs (`tests/`)

Each test is compiled with `gnatmake -D obj <name>.adb` and run under
`LD_PRELOAD=../Linux/libdegas.so`.  Automated pass/fail scripts compare
output byte-for-byte against a known-good expected string.

### Automated (pass/fail) tests

| Script | Source | Description |
|--------|--------|-------------|
| `run_test.sh` | `simple_ada_test.adb` | Single worker task with sequential rendezvous (`Start` / `Done` entries) and `delay` calls; baseline smoke-test for the DEGAS intercept layer. |
| `run_checksum_test.sh` | `checksum_test.adb` | Four worker tasks each compute `id × (1+…+5)` using staggered delays, then rendezvous with a `Collector` task. Final **CHECKSUM: 150** verifies correct task scheduling and summation. Primitives: task entries, rendezvous, `delay`. |
| `run_sync_test.sh` | `sync_test.adb` | Four workers exercise **eight** distinct Ada synchronization primitives across four phases. Final **CHECKSUM: 1110** is the deterministic sum of all contributions. See primitive table below. |

#### `sync_test` primitive coverage

| # | Primitive | Where |
|---|-----------|-------|
| 1 | Protected type — `procedure` + `function` | `Counter` |
| 2 | Protected entry with `when` barrier guard | `Sem` (semaphore), `Gate` (barrier), `Finish` |
| 3 | Basic task rendezvous (`accept`) | `Worker.Init` |
| 4 | Selective accept — multiple `or` alternatives | `Dispatcher`: Submit \| Execute \| Stop |
| 5 | `requeue` (Submit body → Execute queue) | `Dispatcher` |
| 6 | Guarded alternative in selective accept | `Scorer`: `when not Claimed` \| Shutdown |
| 7 | Timed entry call (`select … or delay`) | Worker Phase 3 |
| 8 | Conditional entry call (`select … else`) | Worker Phase 4 |

### Exploratory / development tests

| Source | Description |
|--------|-------------|
| `delays.adb` | Two tasks (`R1`, `R2`) with different `delay` intervals interleaved with the main task; used to verify delay simulation ordering. |
| `r.adb` | Two independent tasks (`A`, `B`) counting with staggered delays; demonstrates interleaved task output. |
| `t1.adb` | Main calls `Worker.Ping` five times with no delays; pure rendezvous sequencing. |
| `t2.adb` | Same as `t1` but main inserts `delay 0.5` before each call; tests delay + rendezvous interaction. |
| `s.adb` | Two tasks wait on a shared `Signals.Event` protected object; main signals it ten times then `abort`s both tasks. |
| `s1.adb` | Single-task variant of `s.adb`; isolates the `Event.Suspend` / `Event.Signal` protected-entry pair. |
| `traffic_light.adb` | North-South / East-West traffic light simulation using two tasks and entry-based phase switching; visual ASCII output. |


