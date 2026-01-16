/**
 *
 *  @file LongPollingBridge.hpp
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
#ifndef VIX_LONG_POLLING_BRIDGE_HPP
#define VIX_LONG_POLLING_BRIDGE_HPP

#include <vix/websocket/LongPolling.hpp>
#include <vix/websocket/protocol.hpp>

#include <functional>
#include <string>
#include <vector>
#include <chrono>

namespace vix::websocket
{
  /**
   * @brief Bridge from WebSocket events to long-polling sessions.
   *
   * Typical usage (external manager + metrics already wired) :
   *
   *   WebSocketMetrics metrics;
   *
   *   LongPollingManager lpManager{
   *       std::chrono::seconds{60},
   *       256,
   *       &metrics
   *   };
   *
   *   auto bridge = std::make_shared<LongPollingBridge>(
   *       lpManager,
   *       [](const JsonMessage &msg) {
   *           if (!msg.room.empty())
   *               return std::string{"room:"} + msg.room;
   *           return std::string{"broadcast"};
   *       },
   *       [&wsServer](const JsonMessage &msg) {
   *           if (!msg.room.empty())
   *               wsServer.broadcast_room_json(msg.room, msg.type, msg.payload);
   *           else
   *               wsServer.broadcast_json(msg.type, msg.payload);
   *       });
   *
   * Variante où le bridge possède son propre LongPollingManager :
   *
   *   WebSocketMetrics metrics;
   *
   *   auto bridge = std::make_shared<LongPollingBridge>(
   *       &metrics,
   *       std::chrono::seconds{60},
   *       256,
   *       [](const JsonMessage &msg) {
   *           if (!msg.room.empty())
   *               return std::string{"room:"} + msg.room;
   *           return std::string{"broadcast"};
   *       },
   *       [&wsServer](const JsonMessage &msg) {
   *           if (!msg.room.empty())
   *               wsServer.broadcast_room_json(msg.room, msg.type, msg.payload);
   *           else
   *               wsServer.broadcast_json(msg.type, msg.payload);
   *       });
   */
  class LongPollingBridge
  {
  public:
    using SessionId = std::string;

    /// Resolver: decides which long-polling session should receive a WS message.
    ///
    /// For example:
    ///   - map by room:  "room:" + msg.room
    ///   - map by type:  "type:" + msg.type
    ///   - map globally: "broadcast"
    using Resolver = std::function<SessionId(const JsonMessage &)>;
    /// Optional hook for HTTP → WebSocket propagation.
    ///
    /// Typical use:
    ///   - when /ws/send receives an HTTP message that should also be
    ///     forwarded to WebSocket clients (broadcast or room).
    using HttpToWsForward = std::function<void(const JsonMessage &)>;

    LongPollingBridge(
        LongPollingManager &manager,
        Resolver resolver = {},
        HttpToWsForward httpToWs = {})
        : managerOwned_(),
          manager_(manager),
          resolver_(std::move(resolver)),
          httpToWs_(std::move(httpToWs))
    {
    }

    LongPollingBridge(
        WebSocketMetrics *metrics,
        std::chrono::seconds sessionTtl = std::chrono::seconds{60},
        std::size_t maxBufferPerSession = 256,
        Resolver resolver = {},
        HttpToWsForward httpToWs = {})
        : managerOwned_(sessionTtl, maxBufferPerSession, metrics),
          manager_(managerOwned_),
          resolver_(std::move(resolver)),
          httpToWs_(std::move(httpToWs))
    {
    }

    LongPollingBridge(const LongPollingBridge &) = delete;
    LongPollingBridge &operator=(const LongPollingBridge &) = delete;
    LongPollingBridge(LongPollingBridge &&) = delete;
    LongPollingBridge &operator=(LongPollingBridge &&) = delete;

    void on_ws_message(const JsonMessage &msg)
    {
      const SessionId sid = resolve_session_id(msg);
      manager_.push_to(sid, msg);
    }

    std::vector<JsonMessage> poll(
        const SessionId &sessionId,
        std::size_t maxMessages = 50,
        bool createIfMissing = true)
    {
      return manager_.poll(sessionId, maxMessages, createIfMissing);
    }

    void send_from_http(
        const SessionId &sessionId,
        const JsonMessage &msg)
    {
      manager_.push_to(sessionId, msg);

      if (httpToWs_)
      {
        httpToWs_(msg);
      }
    }

    LongPollingManager &manager() noexcept { return manager_; }
    const LongPollingManager &manager() const noexcept { return manager_; }
    std::size_t session_count() const { return manager_.session_count(); }
    std::size_t buffer_size(const SessionId &sid) const { return manager_.buffer_size(sid); }

  private:
    SessionId resolve_session_id(const JsonMessage &msg) const
    {
      if (resolver_)
        return resolver_(msg);

      if (!msg.room.empty())
        return std::string{"room:"} + msg.room;
      return std::string{"broadcast"};
    }

  private:
    LongPollingManager managerOwned_;
    LongPollingManager &manager_;
    Resolver resolver_;
    HttpToWsForward httpToWs_;
  };

} // namespace vix::websocket

#endif
