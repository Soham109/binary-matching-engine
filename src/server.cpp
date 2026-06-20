#include <httplib.h>
#include <iostream>
#include "order_book.hpp"
#include <nlohmann/json.hpp>
#include <mutex>

// helper function for working with REST API (name is self explanatory)
// Status -> String
std::string statusToString(Status s) {
    switch (s) {
        case Status::Accepted: return "accepted";
        case Status::PartiallyFilled: return "partially_filled";
        case Status::Filled: return "filled";
        case Status::Rejected: return "rejected";
        case Status::Cancelled: return "cancelled";
        case Status::NotFound: return "not_found";
    }
    return "unknown";
}

// helper function for working with REST API (name is self explanatory)
// Result -> Json
nlohmann::json resultToJson(const Result& r) {
    nlohmann::json out;
    out["status"] = statusToString(r.status);
    out["order_id"] = r.order_id;
    out["remaining"] = r.remaining;
    out["fills"] = nlohmann::json::array();
    for (const Fill& f : r.fills) {
        out["fills"].push_back({
            {"yes_owner", f.yes_owner},
            {"no_owner", f.no_owner},
            {"yes_price", f.yes_price},
            {"qty", f.qty}
        });
    }
    return out;
}


int main() {
    OrderBook book;
    // mutex object to handle order collisions getting executed during the same time
    std::mutex engine_mutex;
    httplib::Server server;

    // GET query to make sure server is listening (used in initial check)
    server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    //POST query (Command)
    server.Post("/orders", [&](const httplib::Request& req, httplib::Response& res){
        nlohmann::json body = nlohmann::json::parse(req.body);

        Command cmd{};
        cmd.kind = Kind::Submit;
        cmd.type = Type::Limit;
        cmd.price = body.value("price", 0); // 0 is a fallback (same with other 2 below)
        cmd.qty = body.value("qty", 0);
        cmd.owner = body.value("owner", 0);
        std::string side = body.value("side", std::string("yes"));
        cmd.side = (side == "no") ? Side::No : Side::Yes;

        Result r;
        {
            // to make sure only one order is being executed at a given time
             std::lock_guard<std::mutex> lock(engine_mutex);
            r = book.execute(cmd);
        }

        //construct the response
        res.set_content(resultToJson(r).dump(), "application/json");
    });

    // DELETE query (reg ex after orders/)
    server.Delete(R"(/orders/(\d+))", [&](const httplib::Request& req, httplib::Response& res){
        Command cmd{};
        cmd.kind = Kind::Cancel;
        cmd.cancel_id = std::stoull(req.matches[1]);

        Result r;
        {
            std::lock_guard<std::mutex> lock(engine_mutex);
            r = book.execute(cmd);
        }

        res.set_content(resultToJson(r).dump(), "application/json");
    });

    // for server to access the data from order book (as noted, it cannot access)
    // the main order books directly as they are private fields, hence the snapshot.
    server.Get("/book", [&](const httplib::Request&, httplib::Response& res){
        BookSnapshot snap;
        {
            std::lock_guard<std::mutex> lock(engine_mutex);
            snap = book.snapshot();
        }

        nlohmann::json out;
        out["yes"] = nlohmann::json::array();
        for (const Level& lvl : snap.yes) {
            out["yes"].push_back({ {"price", lvl.price}, {"qty", lvl.qty} });
        }
        out["no"] = nlohmann::json::array();
        for (const Level& lvl : snap.no) {
            out["no"].push_back({ {"price", lvl.price}, {"qty", lvl.qty} });
        }

        res.set_content(out.dump(), "application/json");
    });

    server.listen("0.0.0.0", 8080);
    return 0;
}