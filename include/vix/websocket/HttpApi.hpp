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
#include <string_view>
#include <cctype>

#include <vix/websocket/server.hpp>
#include <vix/websocket/LongPollingBridge.hpp>
#include <vix/websocket/protocol.hpp>
#include <nlohmann/json.hpp>

namespace vix::websocket::http
{
  namespace detail
  {
    /** @brief Convert an hex character to its integer value (or -1). */
    inline int hex_val(char c)
    {
      if (c >= '0' && c <= '9')
        return c - '0';
      if (c >= 'a' && c <= 'f')
        return 10 + (c - 'a');
      if (c >= 'A' && c <= 'F')
        return 10 + (c - 'A');
      return -1;
    }

    /** @brief Percent-decode a URL query component (+ and %xx). */
    inline std::string url_decode(std::string_view s)
    {
      std::string out;
      out.reserve(s.size());

      for (std::size_t i = 0; i < s.size(); ++i)
      {
        const char c = s[i];

        if (c == '+')
        {
          out.push_back(' ');
          continue;
        }

        if (c == '%' && i + 2 < s.size())
        {
          const int hi = hex_val(s[i + 1]);
          const int lo = hex_val(s[i + 2]);
          if (hi >= 0 && lo >= 0)
          {
            out.push_back(static_cast<char>((hi << 4) | lo));
            i += 2;
            continue;
          }
        }

        out.push_back(c);
      }

      return out;
    }

    /** @brief Extract a query param by parsing the raw target string. */
    inline std::optional<std::string> query_param_from_target(std::string_view target,
                                                              std::string_view key)
    {
      const std::size_t qpos = target.find('?');
      if (qpos == std::string_view::npos)
        return std::nullopt;

      std::string_view qs = target.substr(qpos + 1);

      while (!qs.empty())
      {
        const std::size_t amp = qs.find('&');
        const std::string_view part = (amp == std::string_view::npos) ? qs : qs.substr(0, amp);

        const std::size_t eq = part.find('=');
        const std::string_view k = (eq == std::string_view::npos) ? part : part.substr(0, eq);
        const std::string_view v = (eq == std::string_view::npos) ? std::string_view{} : part.substr(eq + 1);

        if (k == key)
          return url_decode(v);

        if (amp == std::string_view::npos)
          break;

        qs.remove_prefix(amp + 1);
      }

      return std::nullopt;
    }

    /**
     * @brief Convert a request target to std::string.
     *
     * Works for:
     *  - boost::beast::http::request: req.target() -> string_view
     *  - vix::http::Request: req.target() -> std::string
     */
    template <typename Request>
    inline std::string request_target_string(const Request &req)
    {
      return std::string(req.target().data(), req.target().size());
    }

    /**
     * @brief Get query parameter (supports vix::http::Request or Beast-like target parsing).
     */
    template <typename Request>
    inline std::optional<std::string> get_query_param(const Request &req, std::string_view key)
    {
#if defined(__cpp_concepts) && __cpp_concepts >= 201907L
      if constexpr (requires(const Request &r) { r.query_value(key); })
      {
        const std::string v = req.query_value(key);
        if (v.empty())
          return std::nullopt;
        return v;
      }
      else
      {
        const std::string t = request_target_string(req);
        return query_param_from_target(std::string_view(t), key);
      }
#else
      const std::string t = request_target_string(req);
      return query_param_from_target(std::string_view(t), key);
#endif
    }

    /** @brief Return true if the request has a given query param. */
    template <typename Request>
    inline bool has_query_param(const Request &req, std::string_view key)
    {
      return static_cast<bool>(get_query_param(req, key));
    }

    /**
     * @brief Parse JSON body (supports vix::http::Request::json() when available).
     *
     * Returns an optional JSON; may be empty if body is empty or parsing fails.
     */
    template <typename Request>
    inline std::optional<nlohmann::json> get_json_body(const Request &req)
    {
#if defined(__cpp_concepts) && __cpp_concepts >= 201907L
      if constexpr (requires(const Request &r) { r.json(); })
      {
        return req.json();
      }
      else
      {
        return nlohmann::json::parse(req.body(), nullptr, true, true);
      }
#else
      return nlohmann::json::parse(req.body(), nullptr, true, true);
#endif
    }
  } // namespace detail

  /**
   * @brief Handle GET /ws/poll endpoint (long-polling).
   *
   * Reads query params:
   *  - session_id (default "broadcast")
   *  - max        (default 50)
   *
   * Requires a LongPollingBridge attached on the Server.
   */
  template <typename Request, typename Response>
  void handle_ws_poll(Request &req, Response &res, Server &wsServer)
  {
    auto bridge = wsServer.long_polling_bridge();
    if (!bridge)
    {
      nlohmann::json err{{"error", "long-polling bridge not attached"}};
      res.status(503).json(err);
      return;
    }

    std::string sessionId = "broadcast";
    if (auto sid = detail::get_query_param(req, "session_id"))
    {
      if (!sid->empty())
        sessionId = *sid;
    }

    std::size_t maxMessages = 50;
    if (auto maxStr = detail::get_query_param(req, "max"))
    {
      try
      {
        if (!maxStr->empty())
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
   * @brief Handle POST /ws/send endpoint (HTTP -> long-polling and optional WS forward).
   *
   * Body:
   * {
   *   "session_id": "optional-session-id",
   *   "room": "optional-room-name",
   *   "type": "chat.message",
   *   "payload": { ... }
   * }
   *
   * If session_id is missing but room is present, uses "room:<room>".
   * Otherwise uses "broadcast".
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
      auto maybe = detail::get_json_body(req);
      if (!maybe)
      {
        res.status(400).json({"error", "missing JSON body"});
        return;
      }

      body = std::move(*maybe);

      if (!body.is_object())
      {
        res.status(400).json({"error", "JSON body must be an object"});
        return;
      }
    }
    catch (...)
    {
      res.status(400).json({"error", "invalid JSON body"});
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
      msg.payload = vix::websocket::detail::nlohmann_payload_to_kvs(body["payload"]);
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
