#pragma once
#include "types.hpp"
#include <vector>
#include <algorithm>

// A deliberately dumb, obviously-correct matching engine.
// Same interface and same rules as OrderBook, but implemented with
// plain vectors and linear scans.
class ReferenceBook {
public:
    Result execute(const Command& cmd) {
        if (cmd.kind == Kind::Submit) {
            if (cmd.price< 1 || cmd.price > 99 || cmd.qty <= 0)
                return Result{ Status::Rejected, 0, {}, 0 };

            Order order { 
                next_id++, 
                cmd.owner, 
                cmd.price, 
                cmd.qty, 
                cmd.side, 
                cmd.type 
            };

            std::vector<Order>& opp = (order.side == Side::Yes) ? no_book : yes_book;
            std::vector<Order>& same = (order.side == Side::Yes) ? yes_book : no_book;
            std::vector<Fill> fills;

            while (order.qty > 0) {
                // Find the best opposite order
                int best = -1;
                for (int i = 0; i < (int)opp.size(); ++i)
                    if (best == -1 || opp[i].price > opp[best].price) best = i;
                if (best == -1) break;                         
                if (order.price + opp[best].price < 100) break;

                Order& resting= opp[best];

                if (resting.owner == order.owner) {             // self-trade -> cancel the resting one
                    opp.erase(opp.begin() + best);
                    continue;
                }

                int tradeQty = std::min(order.qty, resting.qty);
                Fill f;
                f.qty = tradeQty;
                if (order.side == Side::Yes) {
                    f.yes_owner = order.owner;
                    f.no_owner = resting.owner;
                    f.yes_price = 100 - resting.price;
                } else {
                    f.yes_owner = resting.owner;
                    f.no_owner = order.owner;
                    f.yes_price = resting.price;
                }
                fills.push_back(f);

                order.qty -= tradeQty;
                resting.qty -= tradeQty;
                if (resting.qty == 0) opp.erase(opp.begin() + best);
            }

            if (order.qty > 0) same.push_back(order);         

            Status status = (order.qty == 0) ? Status::Filled
                          : (!fills.empty()) ? Status::PartiallyFilled
                          : Status::Accepted;
            return Result{ status, order.id, fills, order.qty };

        } else { // Cancel: linear search both books by id
            for (auto it = yes_book.begin(); it != yes_book.end(); ++it)
                if (it->id == cmd.cancel_id) {
                    yes_book.erase(it);
                    return Result{ Status::Cancelled, cmd.cancel_id, {}, 0 };
                }
            for (auto it = no_book.begin(); it != no_book.end(); ++it)
                if (it->id == cmd.cancel_id) {
                    no_book.erase(it);
                    return Result{ Status::Cancelled, cmd.cancel_id, {}, 0 };
                }
            return Result{ Status::NotFound, 0, {}, 0 };
        }
    }

    BookSnapshot snapshot() const {
        BookSnapshot snap;
        for (int p = 99; p >= 1; --p) {
            int total = 0; bool any = false;
            for (const Order& o : yes_book) if (o.price == p) { total += o.qty; any = true; }
            if (any) snap.yes.push_back({ p, total });
        }
        for (int p = 99; p >= 1; --p) {
            int total = 0; bool any = false;
            for (const Order& o : no_book) if (o.price == p) { total += o.qty; any = true; }
            if (any) snap.no.push_back({ p, total });
        }
        return snap;
    }

private:
    std::vector<Order> yes_book;  // resting YES orders, in insertion (time) order
    std::vector<Order> no_book;   // resting NO orders, in insertion (time) order
    uint64_t next_id = 1;
};