#include "order_book.hpp"
#include "reference_book.hpp"
#include <random>
#include <iostream>
#include <vector>

int main() {
    std::mt19937 rng(12345);  // fixed seed = reproducible
    std::uniform_int_distribution<int> coin(0,1), price(1,99), qty(1,20), owner(1,4), kind(0,9), type(0,3);

    const int N = 200000;
    OrderBook real;
    ReferenceBook ref;
    std::vector<uint64_t> issued;  // ids handed out, so cancels can target real orders

    for (int i = 0; i < N; ++i) {
        Command cmd{};
        if (kind(rng) < 9 || issued.empty()) {            // mostly submits
            cmd.kind = Kind::Submit;
            switch (type(rng)) {                          // random order type
                case 0: cmd.type = Type::Limit;    break;
                case 1: cmd.type = Type::Ioc;      break;
                case 2: cmd.type = Type::Fok;      break;
                default: cmd.type = Type::PostOnly; break;
            }
            cmd.side = coin(rng) ? Side::Yes : Side::No;
            cmd.price = price(rng);
            cmd.qty = qty(rng);
            cmd.owner = owner(rng);
        } else {
            cmd.kind = Kind::Cancel;
            cmd.cancel_id = issued[rng() % issued.size()];
        }

        Result a = real.execute(cmd);
        Result b = ref.execute(cmd);
        if (!(a == b) || !(real.snapshot() == ref.snapshot())) {
            std::cout << "MISMATCH at #" << i
                      << " (" << (cmd.kind == Kind::Submit ? "submit" : "cancel")
                      << " type=" << (int)cmd.type << " side=" << (cmd.side == Side::Yes ? "Y" : "N")
                      << " price=" << cmd.price << " qty=" << cmd.qty
                      << " owner=" << cmd.owner << " cancel_id=" << cmd.cancel_id << ")\n";
            return 1;
        }

        // type invariants (catch a shared misunderstanding the diff check can't):
        if (cmd.kind == Kind::Submit) {
            if (cmd.type == Type::Ioc && a.status == Status::Accepted) {
                std::cout << "IOC rested at #" << i << "\n"; return 1; 
            }
            if (cmd.type == Type::Fok && a.status != Status::Filled && a.status != Status::Cancelled) {
                std::cout << "FOK neither filled nor killed at #" << i << "\n"; return 1; 
            }
            if (cmd.type == Type::PostOnly &&
                (a.status == Status::Filled || a.status == Status::PartiallyFilled)) {
                std::cout << "PostOnly traded at #" << i << "\n"; return 1; 
            }
        }

        if (cmd.kind == Kind::Submit && a.status != Status::Rejected)
            issued.push_back(a.order_id);
    }

    std::cout<<"OK: " <<N<< " commands (all 4 order types), engines agreed + invariants held.\n";
    return 0;
}