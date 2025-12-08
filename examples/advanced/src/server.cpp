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

#include <vix/websocket.hpp>
#include <vix/websocket/Metrics.hpp>
#include <vix/websocket/SqliteMessageStore.hpp>
#include <vix/websocket/protocol.hpp>

#include <atomic>
#include <cstdint>
#include <sstream>
#include <thread>

int main()
{
    using vix::websocket::App;
    using vix::websocket::JsonMessage;
    using vix::websocket::Session;
    using vix::websocket::WebSocketMetrics;
    using vix::websocket::detail::ws_kvs_to_nlohmann;

    // ─────────────────────────────────────────────
    // 1) App WebSocket haut niveau (Config + ThreadPool inside)
    // ─────────────────────────────────────────────
    App app{"config/config.json"};

    // Accès au serveur sous-jacent
    auto &ws = app.server();

    // ─────────────────────────────────────────────
    // 2) Metrics + exporter HTTP /metrics
    // ─────────────────────────────────────────────
    WebSocketMetrics metrics;

    std::thread metricsThread([&metrics]()
                              { vix::websocket::run_metrics_http_exporter(
                                    metrics,
                                    "0.0.0.0",
                                    9100); });
    metricsThread.detach();

    // ─────────────────────────────────────────────
    // 3) Store persistant SQLite (WAL activé dans le ctor)
    // ─────────────────────────────────────────────
    vix::websocket::SqliteMessageStore store{"chat_messages.db"};
    constexpr std::size_t HISTORY_LIMIT = 50;

    // ─────────────────────────────────────────────
    // 4) on_open : welcome privé + métriques globales
    // ─────────────────────────────────────────────
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

            // log dans SQLite
            store.append(msg);

            // envoyer juste à ce client
            session.send_text(JsonMessage::serialize(msg));
        });

    // (optionnel, si tu as un hook on_close côté Server)
    // ws.on_close([&metrics](Session&) {
    //     metrics.connections_active.fetch_sub(1, std::memory_order_relaxed);
    // });

    // ─────────────────────────────────────────────
    // 5) Logique applicative via App::ws("/chat", handler)
    // ─────────────────────────────────────────────
    app.ws(
        "/chat",
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

    // ─────────────────────────────────────────────
    // 6) Démarrage bloquant
    // ─────────────────────────────────────────────
    app.run_blocking();
}
