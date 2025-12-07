/**
 * @file server.cpp
 * @brief Advanced WebSocket server example for Vix.cpp
 *
 * This example demonstrates a fully–featured, production-style WebSocket
 * server using the Vix.cpp runtime. It showcases how to combine:
 *
 *  • Asynchronous WebSocket server (Beast + Asio)
 *  • ThreadPoolExecutor integration (high-performance scheduling)
 *  • Room-based messaging (join, leave, broadcast)
 *  • Typed JSON protocol ("type" + "payload")
 *  • Persistent message storage using SQLite (WAL enabled)
 *  • Automatic replay of chat history on join
 *  • Prometheus-compatible metrics server (/metrics endpoint)
 *  • Structured system events for room lifecycle (join/leave)
 *
 * Key Concepts Illustrated
 * -------------------------
 * 1. WebSocketMetrics:
 *      A lightweight metrics collector exposed via an HTTP endpoint.
 *      Metrics include connections, messages in/out, and error counts.
 *
 * 2. run_metrics_server():
 *      A minimal Beast-based HTTP server exposing Prometheus text format.
 *      Runs independently from the WebSocket server in a detached thread.
 *
 * 3. SqliteMessageStore:
 *      Provides durable message persistence with replay support.
 *      Uses WAL mode for crash safety and high write throughput.
 *
 * 4. Typed Message Handling:
 *      The WebSocket server routes messages based on their "type" field:
 *          • chat.join    – joins a room + replays history + broadcasts notice
 *          • chat.leave   – leaves a room + broadcasts notice
 *          • chat.message – persists and broadcasts user messages
 *          • other types  – fallback handler for custom events
 *
 * 5. Room Broadcasts:
 *      Room membership is automatically managed by Vix.cpp.
 *      The example shows how to broadcast to a single room or globally.
 *
 * 6. Offline-First Design:
 *      Messages are appended to SQLite before being broadcast,
 *      enabling reliable replay, reconnect recovery, and audit logging.
 *
 * Intended Usage
 * --------------
 * This example is designed for developers building:
 *
 *  • Real-time chat systems
 *  • Collaboration tools
 *  • Event-driven dashboards
 *  • IoT or telemetry streaming
 *  • Any system requiring durable WebSocket channels
 *
 * It demonstrates recommended architectural patterns for Vix.cpp
 * WebSocket applications, including:
 *
 *  • async I/O separation
 *  • metrics visibility
 *  • persistence layering
 *  • structured JSON protocols
 *  • minimalistic yet production-ready design
 *
 * How to Run
 * ----------
 *  1. Ensure dependencies are built: Vix.cpp, nlohmann/json, SQLite3.
 *  2. Create a config file: config/config.json (with websocket.port, etc.)
 *  3. Compile the example:
 *         cmake -S . -B build && cmake --build build -j
 *  4. Run:
 *         ./build/examples/advanced/server
 *  5. Connect using a WebSocket client:
 *         websocat ws://127.0.0.1:9090/
 *  6. Scrape metrics:
 *         curl http://127.0.0.1:9100/metrics
 *
 * This file is part of the Vix.cpp WebSocket module examples and is meant
 * to serve as a reference for building robust real-time systems in C++20.
 */

#include <iostream>
#include <string>

#include <nlohmann/json.hpp>
#include <vix/config/Config.hpp>
#include <vix/experimental/ThreadPoolExecutor.hpp>

#include <vix/websocket.hpp>

#include <atomic>
#include <cstdint>
#include <sstream>

#include <boost/beast/http.hpp>
#include <boost/beast/core.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <thread>

namespace bb = boost::beast;
namespace http = bb::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

// ─────────────────────────────────────────────
// 1) Struct WebSocketMetrics AVANT la fonction
// ─────────────────────────────────────────────
struct WebSocketMetrics
{
    std::atomic<std::uint64_t> connections_total{0};
    std::atomic<std::uint64_t> connections_active{0};
    std::atomic<std::uint64_t> messages_in_total{0};
    std::atomic<std::uint64_t> messages_out_total{0};
    std::atomic<std::uint64_t> errors_total{0};

    std::string render_prometheus() const
    {
        std::ostringstream os;

        os << "# HELP vix_ws_connections_total Total WebSocket connections created\n";
        os << "# TYPE vix_ws_connections_total counter\n";
        os << "vix_ws_connections_total " << connections_total.load() << "\n\n";

        os << "# HELP vix_ws_connections_active Current active WebSocket connections\n";
        os << "# TYPE vix_ws_connections_active gauge\n";
        os << "vix_ws_connections_active " << connections_active.load() << "\n\n";

        os << "# HELP vix_ws_messages_in_total Total number of messages received\n";
        os << "# TYPE vix_ws_messages_in_total counter\n";
        os << "vix_ws_messages_in_total " << messages_in_total.load() << "\n\n";

        os << "# HELP vix_ws_messages_out_total Total number of messages sent\n";
        os << "# TYPE vix_ws_messages_out_total counter\n";
        os << "vix_ws_messages_out_total " << messages_out_total.load() << "\n\n";

        os << "# HELP vix_ws_errors_total Total number of WebSocket errors\n";
        os << "# TYPE vix_ws_errors_total counter\n";
        os << "vix_ws_errors_total " << errors_total.load() << "\n";

        return os.str();
    }
};

// ─────────────────────────────────────────────
// 2) Ensuite la fonction run_metrics_server
// ─────────────────────────────────────────────
void run_metrics_server(WebSocketMetrics &metrics,
                        const std::string &address = "0.0.0.0",
                        std::uint16_t port = 9100)
{
    try
    {
        net::io_context ioc{1};
        tcp::acceptor acceptor{ioc, {net::ip::make_address(address), port}};

        for (;;)
        {
            tcp::socket socket{ioc};
            acceptor.accept(socket);

            bb::flat_buffer buffer;
            http::request<http::string_body> req;
            http::read(socket, buffer, req);

            http::response<http::string_body> res;

            if (req.method() == http::verb::get &&
                req.target() == "/metrics")
            {
                std::string body = metrics.render_prometheus();

                res.result(http::status::ok);
                res.version(req.version());
                res.set(http::field::content_type, "text/plain; version=0.0.4");
                res.body() = std::move(body);
                res.prepare_payload();
            }
            else
            {
                res = http::response<http::string_body>(
                    http::status::not_found, req.version());
                res.set(http::field::content_type, "text/plain");
                res.body() = "Not Found\n";
                res.prepare_payload();
            }

            http::write(socket, res);
            socket.shutdown(tcp::socket::shutdown_send);
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "[metrics] server error: " << e.what() << "\n";
    }
}

int main()
{
    using vix::websocket::JsonMessage;
    using vix::websocket::Session;
    using vix::websocket::detail::ws_kvs_to_nlohmann;

    vix::config::Config cfg{"config/config.json"};

    auto exec = vix::experimental::make_threadpool_executor(
        4, // min threads
        8, // max threads
        0  // default prio
    );

    vix::websocket::Server ws(cfg, std::move(exec));

    WebSocketMetrics metrics;

    std::thread metricsThread([&metrics]()
                              { run_metrics_server(metrics, "0.0.0.0", 9100); });
    metricsThread.detach();

    vix::websocket::SqliteMessageStore store{"chat_messages.db"};

    constexpr std::size_t HISTORY_LIMIT = 50;

    ws.on_open(
        [&store, &metrics](Session &session)
        {
            (void)session;

            metrics.connections_total.fetch_add(1, std::memory_order_relaxed);
            metrics.connections_active.fetch_add(1, std::memory_order_relaxed);

            vix::json::kvs payload{
                "user",
                "server",
                "text",
                "Welcome to Softadastra Chat 👋",
            };

            JsonMessage msg;
            msg.kind = "system";
            msg.type = "chat.system";
            msg.room = "";
            msg.payload = payload;

            store.append(msg);
            session.send_text(JsonMessage::serialize(msg));
        });

    ws.on_typed_message(
        [&ws, &store, &metrics](Session &session,
                                const std::string &type,
                                const vix::json::kvs &payload)
        {
            (void)session;

            metrics.messages_in_total.fetch_add(1, std::memory_order_relaxed);

            nlohmann::json j = ws_kvs_to_nlohmann(payload);

            // 1) JOIN
            if (type == "chat.join")
            {
                std::string room = j.value("room", "");
                std::string user = j.value("user", "anonymous");

                if (!room.empty())
                {
                    ws.join_room(session, room);

                    auto history = store.list_by_room(room, HISTORY_LIMIT, std::nullopt);
                    for (auto msg : history)
                    {
                        if (msg.kind.empty())
                            msg.kind = "history";

                        session.send_text(JsonMessage::serialize(msg));
                        metrics.messages_out_total.fetch_add(1, std::memory_order_relaxed);
                    }

                    vix::json::kvs sysPayload{
                        "room",
                        room,
                        "text",
                        user + " joined the room",
                    };

                    JsonMessage sysMsg;
                    sysMsg.kind = "system";
                    sysMsg.type = "chat.system";
                    sysMsg.room = room;
                    sysMsg.payload = sysPayload;

                    store.append(sysMsg);

                    ws.broadcast_room_json(room, sysMsg.type, sysMsg.payload);
                    metrics.messages_out_total.fetch_add(1, std::memory_order_relaxed);
                }
                return;
            }

            // 2) LEAVE
            if (type == "chat.leave")
            {
                std::string room = j.value("room", "");
                std::string user = j.value("user", "anonymous");

                if (!room.empty())
                {
                    ws.leave_room(session, room);

                    vix::json::kvs sysPayload{
                        "room",
                        room,
                        "text",
                        user + " left the room",
                    };

                    JsonMessage msg;
                    msg.kind = "system";
                    msg.type = "chat.system";
                    msg.room = room;
                    msg.payload = sysPayload;

                    store.append(msg);

                    ws.broadcast_room_json(room, msg.type, msg.payload);
                    metrics.messages_out_total.fetch_add(1, std::memory_order_relaxed);
                }
                return;
            }

            // 3) MESSAGE
            if (type == "chat.message")
            {
                std::string room = j.value("room", "");
                std::string user = j.value("user", "anonymous");
                std::string text = j.value("text", "");

                if (!room.empty() && !text.empty())
                {
                    vix::json::kvs msgPayload{
                        "room",
                        room,
                        "user",
                        user,
                        "text",
                        text,
                    };

                    JsonMessage msg;
                    msg.kind = "event";
                    msg.type = "chat.message";
                    msg.room = room;
                    msg.payload = msgPayload;

                    store.append(msg);

                    ws.broadcast_room_json(room, msg.type, msg.payload);
                    metrics.messages_out_total.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
            }

            // 4) Fallback global
            {
                JsonMessage msg;
                msg.kind = "event";
                msg.type = type;
                msg.room = "";
                msg.payload = payload;

                store.append(msg);
                ws.broadcast_json(type, payload);
                metrics.messages_out_total.fetch_add(1, std::memory_order_relaxed);
            }
        });

    ws.listen_blocking();
}
