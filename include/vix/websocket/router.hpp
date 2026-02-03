/**
 *
 *  @file router.hpp
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
#ifndef VIX_WEBSOCKET_ROUTER_HPP
#define VIX_WEBSOCKET_ROUTER_HPP

#include <functional>
#include <string>
#include <boost/system/error_code.hpp>

namespace vix::websocket
{
  class Session;

  /**
   * @brief Lightweight event router for WebSocket sessions.
   *
   * Acts as a simple dispatch layer between the low-level WebSocket engine
   * and user-defined callbacks (open, close, error, message).
   *
   * The router itself contains no protocol logic and is intentionally minimal.
   */
  class Router
  {
  public:
    using OpenHandler = std::function<void(Session &)>;
    using CloseHandler = std::function<void(Session &)>;
    using ErrorHandler = std::function<void(Session &, const boost::system::error_code &)>;
    using MessageHandler = std::function<void(Session &, std::string)>;

    Router() = default;

    /** @brief Register callback invoked when a session is opened. */
    void on_open(OpenHandler cb) { openHandler_ = std::move(cb); }

    /** @brief Register callback invoked when a session is closed. */
    void on_close(CloseHandler cb) { closeHandler_ = std::move(cb); }

    /** @brief Register callback invoked on WebSocket error. */
    void on_error(ErrorHandler cb) { errorHandler_ = std::move(cb); }

    /** @brief Register callback invoked on incoming text message. */
    void on_message(MessageHandler cb) { messageHandler_ = std::move(cb); }

    /** @brief Dispatch open event to the registered handler. */
    void handle_open(Session &session) const;

    /** @brief Dispatch close event to the registered handler. */
    void handle_close(Session &session) const;

    /** @brief Dispatch error event to the registered handler. */
    void handle_error(Session &session, const boost::system::error_code &ec) const;

    /** @brief Dispatch message event to the registered handler. */
    void handle_message(Session &session, std::string payload) const;

  private:
    OpenHandler openHandler_{};
    CloseHandler closeHandler_{};
    ErrorHandler errorHandler_{};
    MessageHandler messageHandler_{};
  };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_ROUTER_HPP
