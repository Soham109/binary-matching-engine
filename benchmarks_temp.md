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

## Comprehensive bench (all 4 order types) — added after v1

`bench.cpp` was upgraded to (a) generate **all four order types** (Limit/IOC/FOK/POST_ONLY),
(b) report **latency percentiles**, and (c) tally **outcome counters**. This is a *different,
harder workload* than the Limit-only runs above, so these absolute numbers are NOT comparable
to the 162/76.6 figures — only v0-vs-v1 *within this table* are.

Workload: N=2,000,000, seed 99, ~1.80M submits / 0.20M cancels, all 4 TIF types.

| Version | throughput (clean) | p50 | p90 | p99 | p99.9 | max | vs v0 |
|--------:|-------------------:|----:|----:|----:|------:|----:|------:|
| v0 (array+list) | 793.9 ns/op · 1.26 M/s | 42 ns | 167 ns | ~20.3 µs | ~123 µs | 0.44–1.9 ms | 1.00× |
| v1 (pool+bitmask) | 765.1 ns/op · 1.31 M/s | 42 ns | ~150 ns | ~20.0 µs | ~120 µs | 0.41–0.46 ms | **1.04×** |

(Latency includes per-op timer overhead; `max` is a single worst sample and is noisy run-to-run.
`p99`/`p99.9` are stable. Use latency for tail *shape* / relative comparison, throughput for true cost.)

**Identical outcomes on both engines** (strong behavioral-equivalence check beyond the fuzzer):
```
filled=356233 partial=71154 accepted=576805 cancelled=801548 rejected=0 notfound=194260
trades count=666051  matchedVol=3867171   sink=60112548
```

### The finding (this is the point of the comprehensive bench)
- **v1's 2.1× win collapses to ~1.04× on the realistic workload.** The Limit-only bench
  massively overstated the pool/bitmask payoff.
- **Why:** ~25% of submits are FOK, and FOK calls `availableToFill()`, an **O(orders) linear
  scan** of the crossing side of the book. That scan is *unoptimized in both v0 and v1* (the pool
  and bitmask don't touch it), so it dominates and the two versions converge.
- **Tail story, textbook:** median op = **42 ns**, but mean (throughput) = **~790 ns** — a ~19×
  gap. The mean is dragged up entirely by the FOK tail (p99 ≈ 20 µs, max ≈ 1 ms) on a deep book.
  This is exactly "the average hides the tail."
- **Revealed next target:** `availableToFill` for FOK. Options: maintain a running per-level qty
  sum (turns the scan into O(levels), not O(orders)); the STP own-owner exclusion is the wrinkle
  to handle.

## How to record a new version

1. Make the optimization, rebuild with the **exact same build command** above.
2. Run the binary 3×, take the lowest `ns/op`.
3. Add a row to the table; keep the raw runs in a `### vN raw runs` block.

## Known limitations of this benchmark (for the comprehensive pass later)

- Aggregate throughput only — no latency percentiles (p50/p99/p999/max) yet.
- Only `Type::Limit` orders; TIF paths untested.
- Cancel path is dominated by hash-map misses, not erases.
- `steady_clock` timing (fine at this scale; `rdtsc`/cycle-counting would be tighter).
