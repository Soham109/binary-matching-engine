#include "order_book.hpp"
#include <random>
#include <vector>
#include <chrono>
#include <iostream>
#include <algorithm>

int main() {
    // Build mock data
    // ~90% submits / 10% cancels, random prices, 4 participants.
    std::mt19937 rng(99);
    std::uniform_int_distribution<int> coin(0,1), price(1,99), qty(1,20), owner(1,4), kind(0,9);

    const int N = 2'000'000;
    std::vector<Command> cmds;
    cmds.reserve(N);

    uint64_t submits = 0;          

    for (int i = 0; i < N; ++i) {

        Command c{};

        if (kind(rng) < 9 || submits == 0) {
            c.kind = Kind::Submit;
            c.type = Type::Limit;
            c.side = coin(rng) ? Side::Yes : Side::No;
            c.price = price(rng);
            c.qty = qty(rng);
            c.owner = owner(rng);
            ++submits;
        } 
        
        else {
            c.kind = Kind::Cancel;
            std::uniform_int_distribution<uint64_t> pick(1, submits);
            c.cancel_id = pick(rng);      
        }
        cmds.push_back(c);
    }

    long long sink = 0; 

    double best_ns = 1e18;

    for (int iter = 0; iter < 3; ++iter) {
        OrderBook book;
        auto t0 = std::chrono::steady_clock::now();
        for (const Command& c : cmds) sink += book.execute(c).remaining;
        auto t1 = std::chrono::steady_clock::now();
        double ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        best_ns = std::min(best_ns, ns);
    }

    std::cout<<"N="<<N
              << "   "<<best_ns / N<<" ns/op"
              << "   "<<(N/ (best_ns/1e9)) /1e6 << " M ops/sec"
              << "   (sink="<<sink<<")\n";
    return 0;
}