# Benchmarks (temp / working notes)

Engine-core throughput benchmark (`bench.cpp`), used to compare versions of the
matching engine and confirm whether optimizations help on a broad level.

## Fixed setup (keep identical across versions for comparisons to be valid)

- **Build:** `clang++ -std=c++17 -O3 -DNDEBUG -I include/engine bench.cpp src/order_book.cpp -o bench`
  (optimized, no sanitizers — this is the *speed* build, separate from the ASan/UBSan correctness build)
- **Compiler:** Apple clang 17.0.0
- **Machine:** Apple M2 Pro, 10 cores, arm64 (macOS)
- **Workload:** 2,000,000 commands, RNG seed `99`, ~90% submits / ~10% cancels,
  prices 1–99, qty 1–20, 4 owners. **All submits are `Type::Limit`** (IOC/FOK/POST_ONLY
  not exercised). Cancels target a random id over all issued ids (mostly misses).
- **Method:** workload pre-built outside the timed region; `sink` consumes `.remaining`
  to defeat dead-code elimination; harness reports min over 3 internal iterations.
  Reported number below = best of 3 separate invocations (min-of-min).

## Results

| Version | Description | ns/op (best) | M ops/sec | vs v0 | Notes |
|--------:|-------------|-------------:|----------:|------:|-------|
| v0 | Baseline: flat `array<list<Order>,100>` per side, `unordered_map` cancel index, `best_price` = O(99) linear scan re-run per match step | **162.1** | **6.17** | 1.00× | GTC + STP + TIF implemented; differential-fuzz clean |
| v1 | Pre-allocated node **pool** (intrusive doubly-linked list, integer-index links, free-list recycling) replaces `array<list>` → **no per-order malloc**; `best_price` now **O(1)** via a 128-bit (2×`uint64_t`) non-empty-levels bitmask + `clzll` | **76.6** | **13.05** | **2.12×** | Fuzzer clean under ASan/UBSan (200k cmds); `sink` identical to v0 (same behavior). NOTE: bundles *both* optimizations, so the 2.12× can't be split between pool vs bitmask. |

### v0 raw runs
```
N=2000000   170.678 ns/op   5.859   M ops/sec   (sink=37115352)
N=2000000   162.083 ns/op   6.16966 M ops/sec   (sink=37115352)
N=2000000   166.997 ns/op   5.98814 M ops/sec   (sink=37115352)
```
- `sink=37115352` identical across runs → workload is deterministic/reproducible.
- Run-to-run spread ~5% (machine noise; thermal/turbo). For comparisons, run versions
  back-to-back on the same quiet machine.

### v1 raw runs
```
N=2000000   76.6079 ns/op   13.0535 M ops/sec   (sink=37115352)
N=2000000   77.8447 ns/op   12.8461 M ops/sec   (sink=37115352)
N=2000000   77.2512 ns/op   12.9448 M ops/sec   (sink=37115352)
```
- `sink=37115352` matches v0 exactly → v1 is behaviorally identical, just faster.

## How to record a new version

1. Make the optimization, rebuild with the **exact same build command** above.
2. Run the binary 3×, take the lowest `ns/op`.
3. Add a row to the table; keep the raw runs in a `### vN raw runs` block.

## Known limitations of this benchmark (for the comprehensive pass later)

- Aggregate throughput only — no latency percentiles (p50/p99/p999/max) yet.
- Only `Type::Limit` orders; TIF paths untested.
- Cancel path is dominated by hash-map misses, not erases.
- `steady_clock` timing (fine at this scale; `rdtsc`/cycle-counting would be tighter).
