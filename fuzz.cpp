#include "order_book.hpp"
#include "reference_book.hpp"
#include <random>
#include <iostream>
#include <vector>

int main() {
    std::mt19937 rng(12345);  // fixed seed = reproducible
    std::uniform_int_distribution<int> coin(0,1), price(1,99), qty(1,20), owner(1,4), kind(0,9);

    const int N = 200000;
    OrderBook real;
    ReferenceBook ref;
    std::vector<uint64_t> issued; 

    for (int i = 0; i <N; ++i) {
        Command cmd{};
        if (kind(rng) < 9 || issued.empty()) {          
            cmd.kind = Kind::Submit;
            cmd.type = Type::Limit;
            cmd.side = coin(rng) ? Side::Yes : Side::No;
            cmd.price = price(rng);
            cmd.qty = qty(rng);
            cmd.owner = owner(rng);
        } else {
            cmd.kind = Kind::Cancel;
            cmd.cancel_id= issued[rng() % issued.size()];  // may already be gone -> both say NotFound
        }

        Result a = real.execute(cmd);
        Result b = ref.execute(cmd);
        if (!(a == b) || !(real.snapshot() == ref.snapshot())) {
            std::cout <<"MISMATCH at #" << i
                      <<" (" << (cmd.kind == Kind::Submit ? "submit" : "cancel")
                      <<" side=" << (cmd.side == Side::Yes ? "Y" : "N")
                      <<" price="<< cmd.price<<" qty=" << cmd.qty
                      <<" owner=" << cmd.owner <<" cancel_id=" << cmd.cancel_id << ")\n";
            return 1;
        }

        if (cmd.kind == Kind::Submit && a.status != Status::Rejected)
            issued.push_back(a.order_id);
    }

    std::cout<<"OK: " << N << " commands, engines agreed on every result and snapshot.\n";
    return 0;
}