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

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <vix/async/core/cancel.hpp>
#include <vix/async/core/io_context.hpp>
#include <vix/async/core/task.hpp>
#include <vix/async/net/tcp.hpp>
#include <vix/executor/IExecutor.hpp>
#include <vix/utils/Logger.hpp>
#include <vix/websocket/config.hpp>
#include <vix/websocket/protocol.hpp>
#include <vix/websocket/router.hpp>

namespace vix::websocket
{
  using vix::async::core::cancel_source;
  using vix::async::core::io_context;
  using vix::async::core::task;
  using vix::async::net::tcp_stream;

  /**
   * @brief Represents a single WebSocket connection.
   *
   * A Session owns a native Vix TCP stream, manages:
   * - HTTP Upgrade handshake
   * - async frame read/write
   * - idle timeout
   * - dispatch to the WebSocket Router
   *
   * This version is independent of Boost.Beast / Boost.Asio.
   */
  class Session : public std::enable_shared_from_this<Session>
  {
  public:
    /**
     * @brief Construct a WebSocket session.
     *
     * @param stream Accepted native TCP stream.
     * @param cfg WebSocket runtime configuration.
     * @param router Event router for open/close/message/error callbacks.
     * @param executor Executor used for async continuations.
     */
    Session(
        std::unique_ptr<tcp_stream> stream,
        const Config &cfg,
        std::shared_ptr<Router> router,
        std::shared_ptr<vix::executor::IExecutor> executor);

    ~Session() = default;

    /** @brief Start the WebSocket handshake and begin IO. */
    task<void> run();

    /** @brief Send a text frame to the client. */
    void send_text(std::string_view text);

    /** @brief Send a binary frame to the client. */
    void send_binary(const void *data, std::size_t size);

    /** @brief Close the session with an optional close reason. */
    void close(std::string reason = {});

    /** @brief Return true if the session is currently open. */
    bool is_open() const noexcept
    {
      return open_;
    }

    void emit_error(const std::string &message);

  private:
    /** @brief Perform the HTTP Upgrade handshake. */
    task<void> do_accept();

    /** @brief Start the frame read loop. */
    task<void> do_read_loop();

    /** @brief Read one WebSocket frame. */
    task<detail::Frame> read_frame();

    /** @brief Arm the idle timeout scope. */
    void arm_idle_timer();

    /** @brief Cancel the idle timeout scope. */
    void cancel_idle_timer();

    /** @brief Handle idle timeout expiry. */
    task<void> on_idle_timeout();

    /** @brief Enqueue an outgoing message. */
    void do_enqueue_message(bool isBinary, std::string payload);

    /** @brief Write the next queued message if any. */
    task<void> do_write_next();

    /** @brief Flush queued outgoing messages. */
    void trigger_write_flush();

    /** @brief Write one raw frame to the stream. */
    task<void> write_raw_frame(const std::vector<std::byte> &frame);

    /** @brief Read one HTTP request head for the Upgrade request. */
    task<std::string> read_http_head();

    /** @brief Ensure at least n bytes exist in the internal read buffer. */
    task<void> ensure_bytes(std::size_t n);

    /** @brief Send the HTTP Upgrade success response. */
    task<void> send_upgrade_response(const std::string &accept_key);

    /** @brief Close only the underlying TCP stream. */
    task<void> close_stream_only();

    /** @brief Start ping heartbeat if enabled by config. */
    void maybe_start_heartbeat();

    /** @brief Stop ping heartbeat. */
    void stop_heartbeat();

  private:
    std::unique_ptr<tcp_stream> stream_;
    Config cfg_;
    std::shared_ptr<Router> router_;
    std::shared_ptr<vix::executor::IExecutor> executor_;

    std::shared_ptr<io_context> ioc_{};
    std::string readBuffer_{};

    bool closing_{false};
    bool open_{false};

    cancel_source idleCancel_{};
    cancel_source readCancel_{};
    cancel_source writeCancel_{};
    cancel_source closeCancel_{};

    std::thread heartbeatThread_{};
    bool heartbeatStop_{false};

    /** @brief Pending outgoing message descriptor. */
    struct PendingMessage
    {
      bool isBinary{false};
      std::string data{};
    };

    std::deque<PendingMessage> writeQueue_{};
    bool writeInProgress_{false};
    std::mutex writeMutex_{};
  };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_SESSION_HPP
