#pragma once

#include "types.hpp"       
#include <array>
#include <list>
#include <unordered_map>

class OrderBook {
public:
    Result execute(const Command& cmd);
    BookSnapshot snapshot() const;

private:
    struct Location {
        Side side;                       // yes_bids or no_bids
        int  price;                      // which price level (which list)
        std::list<Order>::iterator it;   // the exact node in that list
    };
    std::array<std::list<Order>, 100> yes_bids;   // resting YES bids, one list per price
    std::array<std::list<Order>, 100> no_bids;    // resting NO bids, one list per price
    std::unordered_map<uint64_t, Location> index;   // order id -> where it lives 

    uint64_t next_id=1; 
    
    int best_price(Side side) const; //helper for finding the best avl price
};

