#pragma once

/**
 * @file HttpApi.hpp
 * @brief Convenience helpers to expose /ws/poll and /ws/send over HTTP.
 *
 * This code is intentionally light and assumes a generic HTTP layer
 * with a Request / Response API similar to Vix.cpp:
 *
 *   - req.query("name") -> std::optional<std::string>
 *   - req.json()        -> nlohmann::json (parsed body)
 *   - res.status(int)   -> Response& (fluent)
 *   - res.json(obj)     -> serialize obj as JSON body
 *
 * You can easily adapt these helpers to your actual HTTP types.
 */

#include <cstddef>
#include <optional>
#include <string>

#include <vix/websocket/server.hpp>
#include <vix/websocket/LongPollingBridge.hpp>
#include <vix/websocket/protocol.hpp> // JsonMessage + helpers
#include <nlohmann/json.hpp>

namespace vix::websocket::http
{
    /**
     * @brief Handle GET /ws/poll style endpoint.
     *
     * Assumptions about Request / Response:
     *  - Request:
     *      std::optional<std::string> req.query(const std::string& name) const;
     *  - Response:
     *      Response& res.status(int code);
     *      Response& res.json(const nlohmann::json& j);
     *
     * Typical wiring in your core:
     *
     *   vix::websocket::Server wsServer(config, executor);
     *
     *   app.get("/ws/poll", [&wsServer](auto& req, auto& res) {
     *       vix::websocket::http::handle_ws_poll(req, res, wsServer);
     *   });
     */
    template <typename Request, typename Response>
    void handle_ws_poll(Request &req, Response &res, Server &wsServer)
    {
        auto bridge = wsServer.long_polling_bridge();
        if (!bridge)
        {
            // Long-polling not enabled
            nlohmann::json err{
                {"error", "long-polling bridge not attached"},
            };
            res.status(503).json(err);
            return;
        }

        // session_id: from query ?session_id=... or default "broadcast"
        std::string sessionId = "broadcast";
        if (auto sid = req.query("session_id"))
        {
            if (!sid->empty())
                sessionId = *sid;
        }

        // max messages: from ?max=... (optional)
        std::size_t maxMessages = 50;
        if (auto maxStr = req.query("max"))
        {
            try
            {
                maxMessages = static_cast<std::size_t>(std::stoul(*maxStr));
            }
            catch (...)
            {
                // ignore, keep default
            }
        }

        // Drain messages from long-poll buffer
        auto messages = bridge->poll(sessionId, maxMessages, /*createIfMissing=*/true);

        // Serialize as JSON array
        auto j = json_messages_to_nlohmann_array(messages);

        res.status(200).json(j);
    }

    /**
     * @brief Handle POST /ws/send style endpoint.
     *
     * Expected JSON body shape:
     *
     * {
     *   "session_id": "optional-session-id",
     *   "room": "optional-room-name",
     *   "type": "chat.message",
     *   "payload": {
     *      "user": "alice",
     *      "text": "hello"
     *   }
     * }
     *
     * If session_id is missing but room is present, we will use "room:<room>".
     * Otherwise, fallback to "broadcast".
     *
     * Assumptions:
     *  - Request:
     *      nlohmann::json req.json() const;
     *  - Response:
     *      Response& res.status(int code);
     *      Response& res.json(const nlohmann::json& j);
     *
     * Typical wiring:
     *
     *   app.post("/ws/send", [&wsServer](auto& req, auto& res) {
     *       vix::websocket::http::handle_ws_send(req, res, wsServer);
     *   });
     */
    template <typename Request, typename Response>
    void handle_ws_send(Request &req, Response &res, Server &wsServer)
    {
        auto bridge = wsServer.long_polling_bridge();
        if (!bridge)
        {
            nlohmann::json err{
                {"error", "long-polling bridge not attached"},
            };
            res.status(503).json(err);
            return;
        }

        // Parse JSON body
        nlohmann::json body;
        try
        {
            body = req.json();
        }
        catch (...)
        {
            nlohmann::json err{
                {"error", "invalid JSON body"},
            };
            res.status(400).json(err);
            return;
        }

        const std::string type = body.value("type", std::string{});
        if (type.empty())
        {
            nlohmann::json err{
                {"error", "missing 'type' field"},
            };
            res.status(400).json(err);
            return;
        }

        JsonMessage msg;
        msg.type = type;
        msg.room = body.value("room", std::string{});
        msg.kind = body.value("kind", std::string{}); // optional
        msg.id = body.value("id", std::string{});     // optional
        msg.ts = body.value("ts", std::string{});     // optional

        if (body.contains("payload"))
        {
            msg.payload = detail::nlohmann_payload_to_kvs(body["payload"]);
        }

        // Decide which long-poll session this message goes to.
        std::string sessionId = body.value("session_id", std::string{});

        if (sessionId.empty())
        {
            if (!msg.room.empty())
            {
                sessionId = std::string{"room:"} + msg.room;
            }
            else
            {
                sessionId = "broadcast";
            }
        }

        bridge->send_from_http(sessionId, msg);

        nlohmann::json ok{
            {"status", "queued"},
            {"session_id", sessionId},
        };
        res.status(202).json(ok);
    }

} // namespace vix::websocket::http
