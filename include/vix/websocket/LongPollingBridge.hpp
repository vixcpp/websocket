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
   * @brief Bridge between WebSocket traffic and HTTP long-polling sessions.
   *
   * Receives typed WebSocket JsonMessage events and buffers them into a
   * LongPollingManager. Also supports HTTP->WS forwarding when /ws/send is used.
   */
  class LongPollingBridge
  {
  public:
    using SessionId = std::string;

    /**
     * @brief Resolver that decides which long-polling session receives a message.
     *
     * Common strategies:
     *   - by room: "room:" + msg.room
     *   - global:  "broadcast"
     *   - custom:  shard by user/type/tenant
     */
    using Resolver = std::function<SessionId(const JsonMessage &)>;

    /**
     * @brief Optional hook used to forward HTTP messages back to WS clients.
     *
     * Typical usage: /ws/send pushes into long-polling buffers then broadcasts
     * to WS clients (global or room) using this callback.
     */
    using HttpToWsForward = std::function<void(const JsonMessage &)>;

    /**
     * @brief Construct a bridge using an external LongPollingManager.
     */
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

    /**
     * @brief Construct a bridge that owns its LongPollingManager.
     *
     * @param metrics Optional metrics collector (may be null).
     * @param sessionTtl Session inactivity TTL.
     * @param maxBufferPerSession Max buffered messages per session.
     */
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

    /**
     * @brief Handle a WS message: resolve session id and push into long-polling buffer.
     */
    void on_ws_message(const JsonMessage &msg)
    {
      const SessionId sid = resolve_session_id(msg);
      manager_.push_to(sid, msg);
    }

    /**
     * @brief Poll buffered messages for a given session id.
     */
    std::vector<JsonMessage> poll(
        const SessionId &sessionId,
        std::size_t maxMessages = 50,
        bool createIfMissing = true)
    {
      return manager_.poll(sessionId, maxMessages, createIfMissing);
    }

    /**
     * @brief Push a message originating from HTTP into long-polling and optionally WS.
     *
     * This is typically used by /ws/send.
     */
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

    /** @brief Access the underlying manager. */
    LongPollingManager &manager() noexcept { return manager_; }
    /** @brief Access the underlying manager (const). */
    const LongPollingManager &manager() const noexcept { return manager_; }

    /** @brief Number of active long-polling sessions. */
    std::size_t session_count() const { return manager_.session_count(); }

    /** @brief Buffered messages count for a session (0 if missing). */
    std::size_t buffer_size(const SessionId &sid) const { return manager_.buffer_size(sid); }

  private:
    /** @brief Default resolver: room-based if available, otherwise broadcast. */
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
