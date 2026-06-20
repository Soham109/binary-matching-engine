#pragma once

#include <cstdint>
#include <vector>

enum class Side : uint8_t { Yes, No };
enum class Type : uint8_t { Limit, Ioc, Fok, PostOnly };
enum class Status : uint8_t { Accepted, PartiallyFilled, Filled, Rejected, Cancelled, NotFound };
enum class Kind : uint8_t { Submit, Cancel };

struct Order {
    uint64_t id;
    uint32_t owner;
    int price;
    int qty;
    Side side;
    Type type;
};

struct Fill {   
    uint32_t yes_owner;
    uint32_t no_owner;
    int yes_price;
    int qty;
    bool operator==(const Fill& other) const {
    return yes_owner == other.yes_owner
        && no_owner  == other.no_owner
        && yes_price == other.yes_price
        && qty       == other.qty;
    }
};

struct Command {
    Kind kind;
    Side side;
    Type type;
    int price;
    int qty;
    uint32_t owner;
    uint64_t cancel_id;
};

struct Result {
    Status status;
    uint64_t order_id;
    std::vector<Fill> fills;
    int remaining;
    bool operator == (const Result& other) const {
    return status == other.status
        && order_id == other.order_id
        && fills == other.fills
        && remaining == other.remaining;
    }
};

struct Level { 
    int price; 
    int qty; 
};

struct BookSnapshot { 
    std::vector<Level> yes; 
    std::vector<Level> no; 
};