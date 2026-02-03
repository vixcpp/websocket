/**
 *
 *  @file session.hpp
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
#ifndef VIX_WEBSOCKET_SESSION_HPP
#define VIX_WEBSOCKET_SESSION_HPP

#include <memory>
#include <string>
#include <string_view>
#include <chrono>
#include <vector>
#include <deque>

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>

#include <vix/websocket/config.hpp>
#include <vix/websocket/router.hpp>
#include <vix/executor/IExecutor.hpp>
#include <vix/utils/Logger.hpp>

namespace vix::websocket
{
  namespace beast = boost::beast;
  namespace ws = boost::beast::websocket;
  namespace net = boost::asio;

  /**
   * @brief Represents a single WebSocket connection.
   *
   * A Session owns the Beast WebSocket stream, manages async read/write,
   * idle timeouts, and dispatches events to the Router.
   *
   * Each session is reference-counted and lives as long as there are
   * outstanding async operations.
   */
  class Session : public std::enable_shared_from_this<Session>
  {
  public:
    /**
     * @brief Construct a WebSocket session.
     *
     * @param socket Accepted TCP socket.
     * @param cfg WebSocket runtime configuration.
     * @param router Event router for open/close/message/error callbacks.
     * @param executor Executor used for async continuations.
     */
    Session(
        net::ip::tcp::socket socket,
        const Config &cfg,
        std::shared_ptr<Router> router,
        std::shared_ptr<vix::executor::IExecutor> executor);

    ~Session() = default;

    /** @brief Start the WebSocket handshake and begin IO. */
    void run();

    /** @brief Send a text frame to the client. */
    void send_text(std::string_view text);

    /** @brief Send a binary frame to the client. */
    void send_binary(const void *data, std::size_t size);

    /** @brief Close the session with an optional close reason. */
    void close(ws::close_reason reason = ws::close_reason{});

  private:
    /** @brief Perform the WebSocket accept/handshake. */
    void do_accept();

    /** @brief Accept completion handler. */
    void on_accept(const boost::system::error_code &ec);

    /** @brief Start an async read operation. */
    void do_read();

    /** @brief Read completion handler. */
    void on_read(const boost::system::error_code &ec, std::size_t bytes);

    /** @brief Arm the idle timeout timer. */
    void arm_idle_timer();

    /** @brief Cancel the idle timeout timer. */
    void cancel_idle_timer();

    /** @brief Idle timeout handler. */
    void on_idle_timeout(const boost::system::error_code &ec);

    /** @brief Write completion handler. */
    void on_write_complete(const boost::system::error_code &ec, std::size_t bytes);

  private:
    ws::stream<net::ip::tcp::socket> ws_;
    Config cfg_;
    std::shared_ptr<Router> router_;
    std::shared_ptr<vix::executor::IExecutor> executor_;
    beast::flat_buffer buffer_;
    net::steady_timer idleTimer_;
    bool closing_ = false;

    /** @brief Pending outgoing message descriptor. */
    struct PendingMessage
    {
      bool isBinary;
      std::string data;
    };

    std::deque<PendingMessage> writeQueue_;
    bool writeInProgress_ = false;

    /** @brief Enqueue an outgoing message. */
    void do_enqueue_message(bool isBinary, std::string payload);

    /** @brief Write the next queued message if any. */
    void do_write_next();
  };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_SESSION_HPP
