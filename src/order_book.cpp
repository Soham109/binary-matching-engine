#include "order_book.hpp"
#include <algorithm>   // std::min
#include <cmath>

int OrderBook::best_price(Side side) const {
    for(int p = 99; p>=1; --p) {
        if (side == Side::Yes && !yes_bids[p].empty()) return p;
        if (side == Side::No && !no_bids[p].empty()) return p;
    }
    return 0;
}

// main order processing function
Result OrderBook::execute(const Command& cmd) {
    if (cmd.kind == Kind::Submit) {
        // 1. validate
        if (cmd.price < 1 || cmd.price > 99 || cmd.qty <= 0)
            return Result{ Status::Rejected, 0, {}, 0 };

        Order order;
        order.owner = cmd.owner;
        order.type = cmd.type;
        order.price = cmd.price;
        order.qty = cmd.qty;
        order.side = cmd.side;
        order.id = next_id;
        next_id++;

        Side matchAgainst = (order.side == Side::Yes) ? Side::No : Side::Yes;
        std::vector<Fill> fills;

        // 3. match against the opposite side, best price first
        while (order.qty > 0 && order.price + best_price(matchAgainst) >= 100) {
            int bestP = best_price(matchAgainst);
            std::list<Order>& level =
                (matchAgainst == Side::Yes) ? yes_bids[bestP] : no_bids[bestP];

            while (order.qty > 0 && !level.empty()) {
                Order& resting = level.front();     
                int tradeQty = std::min(order.qty, resting.qty);

                Fill f;
                f.qty = tradeQty;
                if (order.side == Side::Yes) {            
                    f.yes_owner = order.owner;
                    f.no_owner = resting.owner;
                    f.yes_price = 100 - resting.price;          
                } 
                else {                                       
                    f.yes_owner = resting.owner;
                    f.no_owner = order.owner;
                    f.yes_price = resting.price;          
                }
                fills.push_back(f);

                order.qty -= tradeQty;
                resting.qty -= tradeQty;
                if (resting.qty == 0) {
                    index.erase(resting.id);
                    level.pop_front();
                }    
            }
        }

        if (order.qty > 0) {
            if (order.side == Side::Yes) {
                // list.insert(list.end(), order) -> equivalent to push_back, but returns iterator.
                auto it = yes_bids[order.price].insert(yes_bids[order.price].end(), order);
                index[order.id] = {Side::Yes, order.price, it};
            }
            else {
                auto it = no_bids[order.price].insert(no_bids[order.price].end(), order);
                index[order.id] = {Side::No, order.price, it};
            }
        }

        Status status = (order.qty == 0) ? Status::Filled
                      : (!fills.empty()) ? Status::PartiallyFilled
                      : Status::Accepted;
        return Result{ status, order.id, fills, order.qty };

    } else {
        auto found = index.find(cmd.cancel_id);
        if(found==index.end()) {
            return Result {Status::NotFound, 0, {}, 0};
        }
        Location loc = found->second;
        if(loc.side == Side::Yes) yes_bids[loc.price].erase(loc.it);
        else no_bids[loc.price].erase(loc.it);
        index.erase(found);

        return Result{ Status::Cancelled, cmd.cancel_id, {}, 0 };
    }
}

BookSnapshot OrderBook::snapshot() const {
    BookSnapshot snap;
    for (int p= 99; p >= 1; --p) {
        if (!yes_bids[p].empty()) {
            int total = 0;
            for (const Order& o : yes_bids[p]) total += o.qty;
            snap.yes.push_back({ p, total });
        }
    }
    for (int p = 99; p>= 1; --p) {
        if (!no_bids[p].empty()) {
            int total = 0;
            for (const Order& o : no_bids[p]) total += o.qty;
            snap.no.push_back({ p, total });
        }
    }
    return snap;
}