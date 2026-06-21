#include "order_book.hpp"
#include <random>
#include <vector>
#include <chrono>
#include <iostream>
#include <algorithm>
#include <cstdint>

int main() {
    // ---------------------------------------------------------------------
    // Workload: ~90% submits / 10% cancels, 4 participants, prices 1..99.
    // Submits now span ALL four order types (Limit / IOC / FOK / PostOnly),
    // so every matching path is exercised, not just Limit.
    // ---------------------------------------------------------------------
    std::mt19937 rng(99);
    std::uniform_int_distribution<int> coin(0,1), price(1,99), qty(1,20),
                                       owner(1,4), kind(0,9), typ(0,3);

    const int N = 2'000'000;
    std::vector<Command> cmds;
    cmds.reserve(N);

    uint64_t submits = 0;

    for (int i = 0; i < N; ++i) {
        Command c{};
        if (kind(rng) < 9 || submits == 0) {
            c.kind = Kind::Submit;
            switch (typ(rng)) {                       // pick an order type
                case 0:  c.type = Type::Limit;    break;
                case 1:  c.type = Type::Ioc;      break;
                case 2:  c.type = Type::Fok;      break;
                default: c.type = Type::PostOnly; break;
            }
            c.side  = coin(rng) ? Side::Yes : Side::No;
            c.price = price(rng);
            c.qty   = qty(rng);
            c.owner = owner(rng);
            ++submits;
        } else {
            c.kind = Kind::Cancel;
            std::uniform_int_distribution<uint64_t> pick(1, submits);
            c.cancel_id = pick(rng);
        }
        cmds.push_back(c);
    }

    long long sink = 0;   // consumed + printed -> stops the optimizer deleting execute()

    // ---------------------------------------------------------------------
    // Pass 1 — THROUGHPUT (clean): no per-op timer in the loop, so the only
    // thing measured is the engine. Fresh book each replay, take the min of 3
    // (min = least-noise sample). This is the accurate per-op cost.
    // ---------------------------------------------------------------------
    double best_ns = 1e18;
    for (int iter = 0; iter < 3; ++iter) {
        OrderBook book;
        auto t0 = std::chrono::steady_clock::now();
        for (const Command& c : cmds) sink += book.execute(c).remaining;
        auto t1 = std::chrono::steady_clock::now();
        double ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        best_ns = std::min(best_ns, ns);
    }

    // ---------------------------------------------------------------------
    // Pass 2 — PER-OP LATENCY + workload characterization (one replay).
    // We time each execute() individually to get the latency DISTRIBUTION
    // (percentiles), and tally what each command actually did.
    // NOTE: timing every op adds the clock's own overhead (tens of ns) to
    // each sample, so the absolute latency numbers are inflated vs Pass 1.
    // Use them for tail SHAPE and for v0-vs-v1 comparison (both pay the same
    // overhead), not as the true per-op cost — that's the Pass 1 number.
    // ---------------------------------------------------------------------
    std::vector<uint32_t> lat;          // nanoseconds per op (uint32 == up to ~4.3 s, ample)
    lat.reserve(N);

    long long nFilled=0, nPartial=0, nAccepted=0, nCancelled=0, nRejected=0, nNotFound=0;
    long long nTrades=0, matchedVolume=0;   // total Fill records, and total qty traded

    {
        OrderBook book;
        for (const Command& c : cmds) {
            auto t0 = std::chrono::steady_clock::now();
            Result r = book.execute(c);
            auto t1 = std::chrono::steady_clock::now();
            lat.push_back((uint32_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());

            sink += r.remaining;
            switch (r.status) {
                case Status::Filled:          ++nFilled;    break;
                case Status::PartiallyFilled: ++nPartial;   break;
                case Status::Accepted:        ++nAccepted;  break;
                case Status::Cancelled:       ++nCancelled; break;
                case Status::Rejected:        ++nRejected;  break;
                case Status::NotFound:        ++nNotFound;  break;
            }
            nTrades += (long long)r.fills.size();
            for (const Fill& f : r.fills) matchedVolume += f.qty;
        }
    }

    // percentiles by nearest-rank on the sorted samples
    std::sort(lat.begin(), lat.end());
    auto pct = [&](double p) -> uint32_t {
        size_t idx = (size_t)(p / 100.0 * (double)(lat.size() - 1));
        return lat[idx];
    };

    // ---------------------------------------------------------------------
    // Report
    // ---------------------------------------------------------------------
    std::cout << "workload     N=" << N
              << "  submits=" << submits
              << "  cancels=" << (N - submits) << "\n";

    std::cout << "throughput   " << best_ns / N << " ns/op   "
              << (N / (best_ns / 1e9)) / 1e6 << " M ops/sec   (clean, min of 3)\n";

    std::cout << "latency(ns)  p50=" << pct(50)
              << "  p90=" << pct(90)
              << "  p99=" << pct(99)
              << "  p99.9=" << pct(99.9)
              << "  max=" << lat.back()
              << "   (incl. timer overhead)\n";

    std::cout << "outcomes     filled=" << nFilled
              << " partial=" << nPartial
              << " accepted=" << nAccepted
              << " cancelled=" << nCancelled
              << " rejected=" << nRejected
              << " notfound=" << nNotFound << "\n";

    std::cout << "trades       count=" << nTrades
              << "  matchedVol=" << matchedVolume << "\n";

    std::cout << "(sink=" << sink << ")\n";
    return 0;
}
