#pragma once

/**
 * @file LongPollingBridge.hpp
 * @brief Bridge between WebSocket JsonMessage events and long-polling sessions.
 *
 * This component is intentionally HTTP-agnostic:
 *   - it receives JsonMessage from the WebSocket layer
 *   - it forwards them into LongPollingManager sessions
 *   - it exposes a small poll()/send_from_http() API for the HTTP layer
 *
 * The mapping between JsonMessage and "long-poll session id" is done via
 * a pluggable Resolver functor.
 */

#include <vix/websocket/LongPolling.hpp> // LongPollingManager, LongPollingSession
#include <vix/websocket/protocol.hpp>    // JsonMessage

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

        /// Construct a bridge from an existing LongPollingManager (non-owning).
        ///
        /// The manager can already be configured with a WebSocketMetrics*.
        LongPollingBridge(
            LongPollingManager &manager,
            Resolver resolver = {},
            HttpToWsForward httpToWs = {})
            : managerOwned_(),   // unused in this mode
              manager_(manager), // reference to external manager
              resolver_(std::move(resolver)),
              httpToWs_(std::move(httpToWs))
        {
        }

        /// Construct a bridge that owns its LongPollingManager, wired to metrics.
        ///
        /// @param metrics              Optional pointer to metrics
        /// @param sessionTtl           TTL of long-polling sessions
        /// @param maxBufferPerSession  Max buffer size per session
        /// @param resolver             Mapping JsonMessage -> session_id
        /// @param httpToWs             Optional HTTP -> WS forward
        LongPollingBridge(
            WebSocketMetrics *metrics,
            std::chrono::seconds sessionTtl = std::chrono::seconds{60},
            std::size_t maxBufferPerSession = 256,
            Resolver resolver = {},
            HttpToWsForward httpToWs = {})
            : managerOwned_(sessionTtl, maxBufferPerSession, metrics),
              manager_(managerOwned_), // reference bound to owned manager
              resolver_(std::move(resolver)),
              httpToWs_(std::move(httpToWs))
        {
        }

        // Non-copyable / non-movable (because LongPollingManager is non-copyable)
        LongPollingBridge(const LongPollingBridge &) = delete;
        LongPollingBridge &operator=(const LongPollingBridge &) = delete;
        LongPollingBridge(LongPollingBridge &&) = delete;
        LongPollingBridge &operator=(LongPollingBridge &&) = delete;

        /// Called by WebSocket::Server when a JsonMessage is received.
        ///
        /// This forwards the message to the resolved long-polling session.
        void on_ws_message(const JsonMessage &msg)
        {
            const SessionId sid = resolve_session_id(msg);
            manager_.push_to(sid, msg);
        }

        /// HTTP handler helper for /poll.
        ///
        /// The HTTP layer will:
        ///   - read session_id from query / cookie / header
        ///   - call this method
        ///   - serialize returned JsonMessage array to JSON
        std::vector<JsonMessage> poll(const SessionId &sessionId,
                                      std::size_t maxMessages = 50,
                                      bool createIfMissing = true)
        {
            return manager_.poll(sessionId, maxMessages, createIfMissing);
        }

        /// HTTP handler helper for /send.
        ///
        /// This:
        ///   - enqueues the message in the target long-polling session
        ///   - optionally forwards it to WebSocket clients (if httpToWs_ is set)
        void send_from_http(const SessionId &sessionId,
                            const JsonMessage &msg)
        {
            // 1) Store in long-polling buffer so other LP clients can see it
            manager_.push_to(sessionId, msg);

            // 2) Optional: forward to WebSocket world (rooms, broadcast, etc.)
            if (httpToWs_)
            {
                httpToWs_(msg);
            }
        }

        /// Allow external code (metrics / admin) to inspect the manager.
        LongPollingManager &manager() noexcept { return manager_; }
        const LongPollingManager &manager() const noexcept { return manager_; }

        /// Access to session / buffer stats.
        std::size_t session_count() const { return manager_.session_count(); }
        std::size_t buffer_size(const SessionId &sid) const { return manager_.buffer_size(sid); }

    private:
        SessionId resolve_session_id(const JsonMessage &msg) const
        {
            if (resolver_)
                return resolver_(msg);

            // Default fallback: per-room if available, else global "broadcast"
            if (!msg.room.empty())
                return std::string{"room:"} + msg.room;

            return std::string{"broadcast"};
        }

    private:
        // Owned manager (used only in the "metrics + ttl + buffer" constructor).
        // In the "external manager" constructor, this is default-constructed
        // and effectively unused.
        LongPollingManager managerOwned_;

        // Reference to the effective manager (either external or managerOwned_).
        LongPollingManager &manager_;

        Resolver resolver_;
        HttpToWsForward httpToWs_;
    };

} // namespace vix::websocket
