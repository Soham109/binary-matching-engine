# Binary Outcome Matching Engine

A deterministic C++17 matching engine for binary outcome markets: YES/NO
contracts whose complementary legs always settle to one dollar.

**[Read the full interactive engineering write-up →](https://soham109.github.io/binary-matching-engine/)**

The write-up covers the market model, matching semantics, memory layout,
differential fuzzing, benchmark methodology, and the optimization journey from
the baseline book to the current implementation.

## What it implements

- Price-time-priority matching across YES and NO bid ladders
- Limit/GTC, immediate-or-cancel, fill-or-kill, and post-only orders
- Self-trade prevention
- Maker-price execution with complementary YES/NO fill prices
- O(1) best-price lookup through a two-word level bitmask
- Preallocated index-linked order storage with slot recycling
- REST endpoints for submitting orders, cancelling orders, health, and book state
- An independent brute-force reference engine and differential fuzzer
- Reproducible benchmarks spanning narrow microbenchmarks and realistic mixed workloads

## Core market rule

A YES bid at $p_y$ and a NO bid at $p_n$ can trade when:

$$
p_y + p_n \ge 100
$$

The resting order receives its posted price; the incoming order receives the
complementary leg and keeps any price improvement.

## Performance story

The project intentionally keeps three implementation milestones:

| Version | Main change | Mixed-workload result |
| --- | --- | ---: |
| v0 | `array<list<Order>, 100>` baseline | 793.9 ns/op |
| v1 | pooled index-linked nodes + price-level bitmask | 765.1 ns/op |
| v2 | early exit in fill-or-kill liquidity checks | 58.6 ns/op |

The interesting lesson is not simply that v2 is faster. The pooled rewrite
looked like a 2.12× win in a limit-only microbenchmark but improved the realistic
mixed workload by only 4%. The real cost was a deep linear FOK liquidity scan;
stopping as soon as sufficient quantity was found produced the 13.5× end-to-end
speedup and collapsed p99 latency from roughly 20 µs to 292 ns.

## Correctness

`fuzz.cpp` drives the optimized engine and a deliberately simple reference
implementation through identical seeded command streams. After every command it
compares results and complete book snapshots, while also checking order-type
invariants. This makes failures reproducible by seed and command index.

## Build and run

```bash
cmake -S . -B build
cmake --build build -j
```

Start the REST server:

```bash
./build/server
```

Run the differential fuzzer:

```bash
./build/fuzz
```

The server listens on port `8080` and exposes:

- `GET /health`
- `POST /orders`
- `DELETE /orders/:id`
- `GET /book`

## Repository layout

```text
include/engine/       public engine types and interfaces
src/order_book.cpp    optimized matching engine
src/server.cpp        REST/JSON transport
fuzz.cpp              differential fuzz campaign
bench.cpp             benchmark workloads
docs/index.html       GitHub Pages engineering write-up
ai-session/           development session record
```

## Versions

- `v0-baseline` — original array/list implementation
- `v1` — pooled book and O(1) best-price lookup
- `v2` — fill-or-kill early-exit optimization
