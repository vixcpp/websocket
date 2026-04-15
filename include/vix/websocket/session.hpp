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
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <atomic>

#include <vix/async/core/cancel.hpp>
#include <vix/async/core/io_context.hpp>
#include <vix/async/core/task.hpp>
#include <vix/async/net/tcp.hpp>
#include <vix/executor/RuntimeExecutor.hpp>
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
   * A Session owns a native Vix TCP stream and manages:
   * - HTTP Upgrade handshake
   * - asynchronous frame read/write
   * - idle timeout handling
   * - heartbeat lifecycle
   * - dispatch to the WebSocket router
   *
   * This implementation is native to Vix and independent of Boost.
   * It is runtime-based and uses RuntimeExecutor instead of the old
   * generic threadpool-oriented RuntimeExecutor abstraction.
   */
  class Session : public std::enable_shared_from_this<Session>
  {
  public:
    /**
     * @brief Construct a WebSocket session.
     *
     * @param stream Accepted native TCP stream.
     * @param cfg WebSocket runtime configuration.
     * @param router Event router for open, close, message, and error callbacks.
     * @param executor Runtime executor used for async scheduling and continuations.
     */
    Session(
        std::unique_ptr<tcp_stream> stream,
        const Config &cfg,
        std::shared_ptr<Router> router,
        std::shared_ptr<vix::executor::RuntimeExecutor> executor,
        std::shared_ptr<io_context> ioc);

    ~Session() = default;

    /**
     * @brief Start the WebSocket handshake and begin asynchronous IO.
     *
     * @return Task representing the session lifecycle startup.
     */
    task<void> run();

    /**
     * @brief Send a text frame to the client.
     *
     * @param text UTF-8 text payload.
     */
    void send_text(std::string_view text);

    /**
     * @brief Send a binary frame to the client.
     *
     * @param data Pointer to binary payload.
     * @param size Binary payload size in bytes.
     */
    void send_binary(const void *data, std::size_t size);

    /**
     * @brief Close the session with an optional close reason.
     *
     * @param reason Optional application-level close reason.
     */
    void close(std::string reason = {});

    /**
     * @brief Return whether the session is currently open.
     *
     * @return True if the WebSocket session is open.
     */
    bool is_open() const noexcept
    {
      return open_;
    }

    /**
     * @brief Emit an error to the router error handler.
     *
     * @param message Error description.
     */
    void emit_error(const std::string &message);

  private:
    /**
     * @brief Perform the HTTP Upgrade handshake.
     *
     * @return Task representing the handshake operation.
     */
    task<void> do_accept();

    /**
     * @brief Start the main frame read loop.
     *
     * @return Task representing the read loop.
     */
    task<void> do_read_loop();

    /**
     * @brief Read one WebSocket frame from the stream.
     *
     * @return Parsed frame.
     */
    task<detail::Frame> read_frame();

    /**
     * @brief Arm the idle timeout scope.
     */
    void arm_idle_timer();

    /**
     * @brief Cancel the idle timeout scope.
     */
    void cancel_idle_timer();

    /**
     * @brief Handle idle timeout expiry.
     *
     * @return Task representing timeout handling.
     */
    task<void> on_idle_timeout();

    /**
     * @brief Enqueue an outgoing message.
     *
     * @param isBinary True for a binary frame, false for a text frame.
     * @param payload Frame payload to queue.
     */
    void do_enqueue_message(bool isBinary, std::string payload);

    /**
     * @brief Write the next queued outgoing message if any.
     *
     * @return Task representing one write step.
     */
    task<void> do_write_next();

    /**
     * @brief Trigger flushing of queued outgoing messages.
     */
    void trigger_write_flush();

    /**
     * @brief Write one raw frame to the underlying TCP stream.
     *
     * @param frame Serialized raw WebSocket frame bytes.
     * @return Task representing the write operation.
     */
    task<void> write_raw_frame(const std::vector<std::byte> &frame);

    /**
     * @brief Read one HTTP request head for the Upgrade request.
     *
     * @return Raw HTTP request head.
     */
    task<std::string> read_http_head();

    /**
     * @brief Ensure at least @p n bytes are available in the internal read buffer.
     *
     * @param n Minimum number of bytes required.
     * @return Task representing the buffering operation.
     */
    task<void> ensure_bytes(std::size_t n);

    /**
     * @brief Send the HTTP Upgrade success response.
     *
     * @param accept_key Computed Sec-WebSocket-Accept value.
     * @return Task representing the response write.
     */
    task<void> send_upgrade_response(const std::string &accept_key);

    /**
     * @brief Close only the underlying TCP stream.
     *
     * @return Task representing the low-level close operation.
     */
    task<void> close_stream_only();

    /**
     * @brief Start the ping heartbeat if enabled by config.
     */
    void maybe_start_heartbeat();

    /**
     * @brief Stop the ping heartbeat.
     */
    void stop_heartbeat();

  private:
    /** @brief Accepted native TCP stream owned by this session. */
    std::unique_ptr<tcp_stream> stream_;

    /** @brief Immutable WebSocket session configuration. */
    Config cfg_;

    /** @brief Shared router used for lifecycle and message callbacks. */
    std::shared_ptr<Router> router_;

    /** @brief Runtime executor used by this session. */
    std::shared_ptr<vix::executor::RuntimeExecutor> executor_;

    /** @brief Shared async IO context associated with the session runtime. */
    std::shared_ptr<io_context> ioc_{};

    /** @brief Internal read buffer used for HTTP and frame parsing. */
    std::string readBuffer_{};

    std::atomic<bool> closing_{false};
    std::atomic<bool> open_{false};
    std::atomic<bool> closeNotified_{false};

    /** @brief Cancellation source for idle timeout handling. */
    cancel_source idleCancel_{};

    /** @brief Cancellation source for read operations. */
    cancel_source readCancel_{};

    /** @brief Cancellation source for write operations. */
    cancel_source writeCancel_{};

    /** @brief Cancellation source for close operations. */
    cancel_source closeCancel_{};

    /** @brief Background thread used for heartbeat when enabled. */
    std::thread heartbeatThread_{};

    /** @brief Stop flag for the heartbeat thread. */
    bool heartbeatStop_{false};

    /**
     * @brief Pending outgoing message descriptor.
     */
    struct PendingMessage
    {
      /** @brief True if the payload must be sent as binary. */
      bool isBinary{false};

      /** @brief Raw payload bytes stored as string. */
      std::string data{};
    };

    /** @brief FIFO queue of pending outgoing messages. */
    std::deque<PendingMessage> writeQueue_{};

    /** @brief True while a write flush is already in progress. */
    bool writeInProgress_{false};

    /** @brief Protects write queue and write state. */
    std::mutex writeMutex_{};
  };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_SESSION_HPP
