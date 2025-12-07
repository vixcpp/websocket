#ifndef VIX_WEBSOCKET_ROUTER_HPP
#define VIX_WEBSOCKET_ROUTER_HPP

/**
 * @file router.hpp
 * @brief Minimal event-based router for WebSocket sessions.
 *
 * @details
 * The HTTP side already has a full Router with method+path matching.
 * For WebSocket we start with an event-driven router:
 *
 *  - on_open(Session&)
 *  - on_message(Session&, std::string)
 *  - on_close(Session&)
 *  - on_error(Session&, boost::system::error_code)
 *
 * Higher-level protocols (channels, rooms, JSON "type" field, etc.) can be
 * layered on top later without touching the low-level Session implementation.
 *
 * IMPORTANT:
 *  We pass the payload as std::string (by value) to avoid lifetime issues
 *  with std::string_view when messages are dispatched asynchronously.
 */

#include <functional>
#include <string>

#include <boost/system/error_code.hpp>

namespace vix::websocket
{
    class Session; // fwd

    class Router
    {
    public:
        using OpenHandler = std::function<void(Session &)>;
        using CloseHandler = std::function<void(Session &)>;
        using ErrorHandler = std::function<void(Session &, const boost::system::error_code &)>;
        using MessageHandler = std::function<void(Session &, std::string)>;

        Router() = default;

        void on_open(OpenHandler cb) { openHandler_ = std::move(cb); }
        void on_close(CloseHandler cb) { closeHandler_ = std::move(cb); }
        void on_error(ErrorHandler cb) { errorHandler_ = std::move(cb); }
        void on_message(MessageHandler cb) { messageHandler_ = std::move(cb); }

        // Called by Session – safe if callback not set.
        void handle_open(Session &session) const;
        void handle_close(Session &session) const;
        void handle_error(Session &session, const boost::system::error_code &ec) const;

        /**
         * @brief Dispatch an incoming message.
         *
         * The payload is passed by value to ensure it remains valid even if
         * the handler is executed asynchronously.
         */
        void handle_message(Session &session, std::string payload) const;

    private:
        OpenHandler openHandler_;
        CloseHandler closeHandler_;
        ErrorHandler errorHandler_;
        MessageHandler messageHandler_;
    };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_ROUTER_HPP
