#include "order_book.hpp"
#include <algorithm>   // std::min
#include <cmath>

// returns the best available price @ a given side
int OrderBook::best_price(Side side) const {
    for(int p = 99; p>=1; --p) {
        if (side == Side::Yes && !yes_bids[p].empty()) return p;
        if (side == Side::No && !no_bids[p].empty()) return p;
    }
    return 0;
}

// total quantity available to fill (for FOK)
int OrderBook::availableToFill(const Order& order) const {
    Side opp = (order.side == Side::Yes) ? Side::No : Side::Yes;
    int total = 0;
    for (int p = 99; p >= 1; --p) {
        if (order.price + p < 100) break;          // this level (and all lower) don't cross
        const std::list<Order>& level = (opp == Side::Yes) ? yes_bids[p] : no_bids[p];
        for (const Order& o : level)
            if (o.owner != order.owner) total += o.qty;
    }
    return total;
}


// main order processing function
Result OrderBook::execute(const Command& cmd) {
    if (cmd.kind == Kind::Submit) {
        //CASE: Submit order

        // 1. validate 
            //  - price 0 translates to a definitive event -> no risk & no profit
            //  - cannot order 0 or less quantitiy.

            //outcome => reject (return Result with a status of Rejected)
        if (cmd.price < 1 || cmd.price > 99 || cmd.qty <= 0)
            return Result{ Status::Rejected, 0, {}, 0 };
        
        // Create an order based on the given command
        Order order;
        order.owner = cmd.owner;
        order.type = cmd.type;
        order.price = cmd.price;
        order.qty = cmd.qty;
        order.side = cmd.side;
        order.id = next_id; // next_id starts with 1 
        next_id++; // increment next_id once order created to avoid duplicate order ids

        // match side = opposite of order side
        Side matchAgainst = (order.side == Side::Yes) ? Side::No : Side::Yes;
        std::vector<Fill> fills;

        if(order.type == Type::PostOnly && order.price + best_price(matchAgainst)>=100){
            return Result{ Status::Cancelled, order.id, {}, order.qty };
        }

        if(order.type == Type::Fok && availableToFill(order)<order.qty) {
            return Result { Status::Cancelled, order.id, {}, order.qty };
        }


        // 3. match against the opposite side, best price first

        // logic behind order.price + best_price(matchAgainst) >= 100:
            // if its < 100 will mean the taker pays more than the max value (order.price)
            // they placed the order at.
        while (order.qty > 0 && order.price + best_price(matchAgainst) >= 100) {
            int bestP = best_price(matchAgainst);

            //order book selection -> to match the order with
            std::list<Order>& level =
                (matchAgainst == Side::Yes) ? yes_bids[bestP] : no_bids[bestP];

            //traverse till either:
                // - complete order gets fullfilled OR No more eligible trades are left
            while (order.qty > 0 && !level.empty()) {
                Order& resting = level.front();

                //self trade prevention
                if (resting.owner == cmd.owner) {
                    index.erase(resting.id); 
                    level.pop_front();      
                    continue;       
                }

                //minimum to ensure final quantity to be positive (limiting case)
                int tradeQty = std::min(order.qty, resting.qty);

                // curate fill vector as orders get matched.
                Fill f;
                f.qty = tradeQty;

                //YES side order
                if (order.side == Side::Yes) {            
                    f.yes_owner = order.owner;
                    f.no_owner = resting.owner;
                    f.yes_price = 100 - resting.price;          
                } 

                //NO side order
                else {                                       
                    f.yes_owner = resting.owner;
                    f.no_owner = order.owner;
                    f.yes_price = resting.price;          
                }

                fills.push_back(f);

                order.qty -= tradeQty;
                resting.qty -= tradeQty;

                // if current node is exhausted, remove it to make sure there is no stale data.
                if (resting.qty == 0) {
                    index.erase(resting.id); 
                    level.pop_front();
                }    
            }
        }

        // if order is not satisfied fully, add it to the order book
        if (order.qty > 0 && (order.type==Type::Limit || order.type == Type::PostOnly)) {
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

        Status status;
        if(order.qty == 0) status = Status::Filled;
        else if(!fills.empty()) status = Status::PartiallyFilled;
        else if(order.type == Type::Ioc) status = Status::Cancelled;
        else status = Status::Accepted;
        return Result{ status, order.id, fills, order.qty };

    } else {
        //CASE: Cancel Order

        // search for cancel_id in hashmap -> O(1)
        auto found = index.find(cmd.cancel_id);
        if(found==index.end()) {
            return Result {Status::NotFound, 0, {}, 0};
        }
        Location loc = found->second;
        if(loc.side == Side::Yes) yes_bids[loc.price].erase(loc.it); //remove order based on order book
        else no_bids[loc.price].erase(loc.it);
        index.erase(found);

        return Result{ Status::Cancelled, cmd.cancel_id, {}, 0 };
    }
}
// Because the yes_bids and no_bids are private, this is a helper 
// function for the server to get an overall context of the order book 

//returns {price,quantity} (Level) vectors for all prices avl in yes and no bids
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