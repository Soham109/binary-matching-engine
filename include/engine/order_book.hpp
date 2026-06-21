#pragma once
#include "types.hpp"
#include <vector>
#include <array>
#include <unordered_map>

class OrderBook {
public:
    OrderBook();
    Result execute(const Command& cmd);
    BookSnapshot snapshot() const;

private:
    // One order = one solt in the pool. next/prev are slot indices (-1 = none),
    // so the pool can grow/reallocate without invalidating any links.
    struct Node {
        Order order;
        int next;
        int prev;
    };

    std::vector<Node> pool; // array for storing all live orders
    std::vector<int> freeSlots; // recycled slots
    std::array<int,100> yes_head, yes_tail; // head/tail slot per price level 
    std::array<int,100> no_head,  no_tail;
    uint64_t yes_levels[2] = {0,0}; //bitmastk
    uint64_t no_levels[2] = {0,0};
    std::unordered_map<uint64_t,int> index; // order id -> slot, for O(1) cancel
    uint64_t next_id = 1;

    int  allocNode();
    void freeNode(int slot);
    void insertTail(int slot); // link slot at the back of its own level
    void unlink(int slot); // remove slot from its level
    int  best_price(Side s) const;
    int  availableToFill(const Order& o) const;

    int* headArr(Side s) { 
        return s==Side::Yes ? yes_head.data() : no_head.data(); 
    }
    int* tailArr(Side s) { 
        return s==Side::Yes ? yes_tail.data() : no_tail.data(); 
    }
    uint64_t* maskArr(Side s) { 
        return s==Side::Yes ? yes_levels : no_levels; 
    }
};