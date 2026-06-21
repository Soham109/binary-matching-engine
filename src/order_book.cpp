#include "order_book.hpp"
#include <algorithm>   // std::min


//the below 2 functions are essentially present to maintain the prices for which 
// there are orders present in the order book.
static void setBit(uint64_t* mask, int price) {
    int bucket = price >> 6; // price/64 -> for deciding WHICH index for the array this enables the bit. eg. if price is 30, mask[0].
    int bit = price&63; // price%64
    mask[bucket] |= (1ULL << bit); // turn this price level ON
}

static void clearBit(uint64_t* mask, int price) {
    int bucket = price >> 6;
    int bit = price&63;
    mask[bucket] &= ~(1ULL << bit); // turn this price level OFF
}

// orderbook constructor for empty book
OrderBook::OrderBook() {
    // -1 means "this price level has no orders"
    yes_head.fill(-1);
    yes_tail.fill(-1);

    no_head.fill(-1);
    no_tail.fill(-1);

    // pre-allocate space for 2^16 nodes
    pool.reserve(1<<16);
}


// gives us a slot in pool for a new resting order
int OrderBook::allocNode() {
    // if we have an old cancelled/filled slot, reuse it
    if (!freeSlots.empty()) {
        int slot = freeSlots.back();
        freeSlots.pop_back();
        return slot;
    }

    // otherwise create a fresh empty Node at the end of pool
    pool.push_back(Node{});

    // return index of newly created node
    return (int)pool.size() - 1;
}


// marks a pool slot as reusable
void OrderBook::freeNode(int slot) {
    freeSlots.push_back(slot);
}


// adds pool[slot] to the back of its own price level queue
void OrderBook::insertTail(int slot) {
    Order& order = pool[slot].order;

    Side side = order.side;
    int price = order.price;

    // pick YES arrays or NO arrays based on order side
    int* head = headArr(side);
    int* tail = tailArr(side);

    // new node is going at the back, so there is no node after it yet
    pool[slot].next = -1;

    // previous node is the old tail at this price
    pool[slot].prev = tail[price];

    // CASE 1: this price level was empty
    if (tail[price] == -1) {
        head[price] = slot;
    }

    // CASE 2: this price level already had orders
    else {
        pool[tail[price]].next = slot;
    }

    // this new node is now the tail of this price level
    tail[price] = slot;

    // mark this price level as non-empty in the bitmask
    setBit(maskArr(side), price);
}


// removes pool[slot] from its price level queue
void OrderBook::unlink(int slot) {
    Order& order = pool[slot].order;

    Side side = order.side;
    int price = order.price;

    // pick YES arrays or NO arrays based on order side
    int* head = headArr(side);
    int* tail = tailArr(side);

    int prevSlot = pool[slot].prev;
    int nextSlot = pool[slot].next;

    // fix previous node's next pointer
    // if there is no previous node, this node was the head
    if (prevSlot != -1) {
        pool[prevSlot].next = nextSlot;
    } else {
        head[price] = nextSlot;
    }

    // fix next node's prev pointer
    // if there is no next node, this node was the tail
    if (nextSlot != -1) {
        pool[nextSlot].prev = prevSlot;
    } else {
        tail[price] = prevSlot;
    }

    // if the level became empty, turn its bit OFF
    if (head[price] == -1) {
        clearBit(maskArr(side), price);
    }
}


// returns the best available price on a given side
int OrderBook::best_price(Side side) const {
    
    //pick the side
    const uint64_t* mask = (side == Side::Yes) ? yes_levels : no_levels;

    // prices 64..99 live inside mask[1]
    // if anything exists here, best price must be in this upper bucket
    // start from mask 1 because thats where the hihger prices live.
    if (mask[1]) {
        int highestBit = 63 - __builtin_clzll(mask[1]);
        return 64 + highestBit;
    }

    // prices 0..63 live inside mask[0]
    if (mask[0]) {
        int highestBit = 63 - __builtin_clzll(mask[0]);
        return highestBit;
    }

    // no available price on this side
    return 0;
}


// total quantity available to fill an order immediately
// mainly used for FOK orders
int OrderBook::availableToFill(const Order& order) const {
    // if order is YES, it matches against NO
    // if order is NO, it matches against YES
    const int* oppHead = (order.side == Side::Yes) ? no_head.data() : yes_head.data();

    int total = 0;

    // scan opposite prices from best to worst
    for (int price = 99; price >= 1; --price) {
        // once this level doesn't cross, lower prices won't cross either
        if (order.price + price < 100) break;

        // walk this price level using linked-list slots
        for (int slot = oppHead[price]; slot != -1; slot = pool[slot].next) {
            const Order& resting = pool[slot].order;

            // self-trades do not count as fillable quantity
            if (resting.owner != order.owner) {
                total += resting.qty;
            }
        }
    }

    return total;
}


// main order processing function
Result OrderBook::execute(const Command& cmd) {
    // CASE: Cancel order
    if (cmd.kind == Kind::Cancel) {
        // search for order id in hashmap -> O(1)
        auto found = index.find(cmd.cancel_id);
        if (found == index.end()) {
            return Result{ Status::NotFound, 0, {}, 0 };
        }
        int slot = found->second;

        // remove from linked price queue
        unlink(slot);
        // mark the pool slot as reusable
        freeNode(slot);
        // remove from cancel lookup map
        index.erase(found);
        return Result{ Status::Cancelled, cmd.cancel_id, {}, 0 };
    }


    // CASE: Submit order

    // validate price and quantity
    // price must be between 1 and 99
    // qty must be positive
    if (cmd.price < 1 || cmd.price > 99 || cmd.qty <= 0) {
        return Result{ Status::Rejected, 0, {}, 0 };
    }

    // create incoming order from command
    Order order;
    order.id  = next_id++;
    order.owner = cmd.owner;
    order.price = cmd.price;
    order.qty = cmd.qty;
    order.side = cmd.side;
    order.type = cmd.type;

    // match against the opposite side
    Side matchAgainst = (order.side == Side::Yes) ? Side::No : Side::Yes;

    std::vector<Fill> fills;


    // CASE: PostOnly
    // PostOnly should never immediately match.
    // If it crosses the current opposite best price, cancel it.
    if (order.type == Type::PostOnly) {
        int bestOppPrice = best_price(matchAgainst);

        if (bestOppPrice != 0 && order.price + bestOppPrice >= 100) {
            return Result{ Status::Cancelled, order.id, {}, order.qty };
        }
    }


    // CASE: FOK
    // Fill-or-kill should only execute if the full quantity is available right now.
    if (order.type == Type::Fok && availableToFill(order) < order.qty) {
        return Result{ Status::Cancelled, order.id, {}, order.qty };
    }


    // match against opposite side, best price first
    while (order.qty > 0) {
        int bestOppPrice = best_price(matchAgainst);

        // no opposite orders available
        if (bestOppPrice == 0) break;

        // best opposite price still does not cross
        if (order.price + bestOppPrice < 100) break;

        // get the first resting order at this price level
        int slot = headArr(matchAgainst)[bestOppPrice];

        // walk this price level FIFO
        while (order.qty > 0 && slot != -1) {
            // save next first, because current slot might get unlinked/freed
            int nextSlot = pool[slot].next;

            Order& resting = pool[slot].order;

            // self-trade prevention
            // current rule: cancel the resting order and continue
            if (resting.owner == order.owner) {
                index.erase(resting.id);
                unlink(slot);
                freeNode(slot);

                slot = nextSlot;
                continue;
            }

            // quantity traded is limited by smaller of incoming/resting qty
            int tradeQty = std::min(order.qty, resting.qty);

            Fill fill;
            fill.qty = tradeQty;

            // incoming YES order takes resting NO
            if (order.side == Side::Yes) {
                fill.yes_owner = order.owner;
                fill.no_owner  = resting.owner;

                // resting NO price is converted into YES price
                fill.yes_price = 100 - resting.price;
            }

            // incoming NO order takes resting YES
            else {
                fill.yes_owner = resting.owner;
                fill.no_owner = order.owner;

                // resting YES price is already YES price
                fill.yes_price = resting.price;
            }

            fills.push_back(fill);

            // reduce quantities after trade
            order.qty -= tradeQty;
            resting.qty -= tradeQty;

            // if resting order is fully filled, remove it from the book
            if (resting.qty == 0) {
                index.erase(resting.id);
                unlink(slot);
                freeNode(slot);
            }

            slot = nextSlot;
        }
    }


    // if leftover quantity remains, rest it on the book
    // IOC orders do not rest
    // FOK orders only reach here if fully fillable, so leftover should not remain normally
    if (order.qty > 0 &&
        (order.type == Type::Limit || order.type == Type::PostOnly)) {
        int slot = allocNode();
        pool[slot].order = order;
        // add to the back of its own price queue
        insertTail(slot);
        // store id -> slot for O(1) cancel later
        index[order.id] = slot;
    }


    // decide final status
    Status status;

    if (order.qty == 0) {
        status = Status::Filled;
    } 
    
    else if (!fills.empty()) {
        status = Status::PartiallyFilled;
    } 
    
    else if (order.type == Type::Ioc) {
        status = Status::Cancelled;
    } 
    
    else {
        status = Status::Accepted;
    }

    return Result{ status, order.id, fills, order.qty };
}


// returns aggregated book state for the server/UI
BookSnapshot OrderBook::snapshot() const {
    BookSnapshot snap;

    // YES side, best price first
    for (int price = 99; price >= 1; --price) {
        if (yes_head[price] == -1) continue;

        int total = 0;

        // walk linked list for this price level
        for (int slot = yes_head[price]; slot != -1; slot = pool[slot].next) {
            total += pool[slot].order.qty;
        }

        snap.yes.push_back({ price, total });
    }

    // NO side, best price first
    for (int price = 99; price >= 1; --price) {
        if (no_head[price] == -1) continue;
        int total = 0;
        // walk linked list for this price level
        for (int slot = no_head[price]; slot != -1; slot = pool[slot].next) {
            total += pool[slot].order.qty;
        }
        snap.no.push_back({ price, total });
    }
    return snap;
}
