/**
 *
 *  @file HttpApi.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */


#ifndef VIX_HTTP_API_HPP
#define VIX_HTTP_API_HPP

#include <cstddef>
#include <optional>
#include <string>

#include <vix/websocket/server.hpp>
#include <vix/websocket/LongPollingBridge.hpp>
#include <vix/websocket/protocol.hpp>
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

    // max messages: from ?max=...
    std::size_t maxMessages = 50;
    if (auto maxStr = req.query("max"))
    {
      try
      {
        maxMessages = static_cast<std::size_t>(std::stoul(*maxStr));
      }
      catch (...)
      {
      }
    }

    auto messages = bridge->poll(sessionId, maxMessages, /*createIfMissing=*/true);
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
    msg.kind = body.value("kind", std::string{});
    msg.id = body.value("id", std::string{});
    msg.ts = body.value("ts", std::string{});

    if (body.contains("payload"))
    {
      msg.payload = detail::nlohmann_payload_to_kvs(body["payload"]);
    }

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

#endif
