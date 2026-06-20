#include <httplib.h>
#include <iostream>
#include "order_book.hpp"
#include <nlohmann/json.hpp>
#include <mutex>


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
    std::mutex engine_mutex;
    httplib::Server server;

    server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    server.Post("/orders", [&](const httplib::Request& req, httplib::Response& res){
        nlohmann::json body = nlohmann::json::parse(req.body);

        Command cmd{};
        cmd.kind = Kind::Submit;
        cmd.type = Type::Limit;
        cmd.price = body.value("price", 0);
        cmd.qty = body.value("qty", 0);
        cmd.owner = body.value("owner", 0);
        std::string side = body.value("side", std::string("yes"));
        cmd.side = (side == "no") ? Side::No : Side::Yes;

        Result r;
        {
            std::lock_guard<std::mutex> lock(engine_mutex);
            r = book.execute(cmd);
        }

        res.set_content(resultToJson(r).dump(), "application/json");
    });

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