/**
 *
 *  @file Client.hpp
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
#ifndef VIX_WEBSOCKET_CLIENT_HPP
#define VIX_WEBSOCKET_CLIENT_HPP

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <vix/async/core/io_context.hpp>
#include <vix/async/core/spawn.hpp>
#include <vix/async/core/task.hpp>
#include <vix/async/net/dns.hpp>
#include <vix/async/net/tcp.hpp>
#include <vix/json/Simple.hpp>
#include <vix/websocket/protocol.hpp>

namespace vix::websocket
{
  using vix::async::core::cancel_source;
  using vix::async::core::io_context;
  using vix::async::core::task;
  using vix::async::net::dns_resolver;
  using vix::async::net::make_dns_resolver;
  using vix::async::net::make_tcp_stream;
  using vix::async::net::resolved_address;
  using vix::async::net::tcp_endpoint;
  using vix::async::net::tcp_stream;

  /**
   * @brief Minimal native WebSocket client with typed JSON messages.
   *
   * Provides:
   * - asynchronous TCP connect via Vix async
   * - HTTP Upgrade handshake to WebSocket
   * - text frame send/read
   * - optional heartbeat ping
   * - optional auto-reconnect
   *
   * This version is independent of Boost.Beast / Boost.Asio.
   */
  class Client : public std::enable_shared_from_this<Client>
  {
  public:
    /** @brief Called after the WebSocket handshake succeeds. */
    using OpenHandler = std::function<void()>;

    /** @brief Called when a text frame is received. */
    using MessageHandler = std::function<void(const std::string &)>;

    /** @brief Called when the connection closes. */
    using CloseHandler = std::function<void()>;

    /** @brief Called on async errors (resolve/connect/handshake/read/write/ping/close). */
    using ErrorHandler = std::function<void(const std::string &)>;

    /**
     * @brief Create a client instance.
     *
     * @param host Hostname or IP.
     * @param port Service/port (e.g. "9090").
     * @param target WebSocket target path (default "/").
     */
    static std::shared_ptr<Client> create(
        std::string host,
        std::string port,
        std::string target = "/")
    {
      return std::shared_ptr<Client>(
          new Client(std::move(host), std::move(port), std::move(target)));
    }

    /** @brief Set the on-open callback. */
    void on_open(OpenHandler cb) { onOpen_ = std::move(cb); }

    /** @brief Set the on-message callback. */
    void on_message(MessageHandler cb) { onMessage_ = std::move(cb); }

    /** @brief Set the on-close callback. */
    void on_close(CloseHandler cb) { onClose_ = std::move(cb); }

    /** @brief Set the on-error callback. */
    void on_error(ErrorHandler cb) { onError_ = std::move(cb); }

    /**
     * @brief Enable or disable auto-reconnect after failures.
     *
     * @param enable Whether to reconnect automatically.
     * @param delay Delay before attempting reconnect.
     */
    void enable_auto_reconnect(
        bool enable,
        std::chrono::seconds delay = std::chrono::seconds{3})
    {
      autoReconnect_.store(enable, std::memory_order_relaxed);
      reconnectDelay_ = delay;
    }

    /**
     * @brief Enable periodic ping heartbeat.
     *
     * @param interval Ping interval. If <= 0, heartbeat is disabled.
     */
    void enable_heartbeat(std::chrono::seconds interval)
    {
      if (interval.count() <= 0)
      {
        heartbeatEnabled_.store(false, std::memory_order_relaxed);
        return;
      }

      heartbeatInterval_ = interval;
      heartbeatEnabled_.store(true, std::memory_order_relaxed);
    }

    /**
     * @brief Connect asynchronously and start the client runtime thread.
     *
     * Safe to call once; subsequent calls are ignored until reconnect logic resets.
     */
    void connect()
    {
      if (!alive_.load(std::memory_order_relaxed) ||
          closing_.load(std::memory_order_relaxed))
      {
        return;
      }

      bool expected = false;
      if (!started_.compare_exchange_strong(expected, true))
      {
        return;
      }

      init_runtime_();
      auto self = shared_from_this();

      ioThread_ = std::thread(
          [self]()
          {
            try
            {
              self->ioc_->run();
            }
            catch (const std::exception &e)
            {
              self->emit_error_("runtime", e.what());
            }
          });

      vix::async::core::spawn_detached(*ioc_, run_connect_flow_());
    }

    /**
     * @brief Queue and send a raw text frame.
     *
     * Thread-safe: enqueues the payload and flushes sequentially.
     */
    void send_text(const std::string &text)
    {
      if (!connected_.load(std::memory_order_relaxed) ||
          closing_.load(std::memory_order_relaxed))
      {
        return;
      }

      {
        std::lock_guard<std::mutex> lock(writeMutex_);
        writeQueue_.push_back(
            detail::build_text_frame(text, true /* mask client frames */));
      }

      trigger_write_flush_();
    }

    /**
     * @brief Send a typed JSON message.
     *
     * Format: {"type": "<type>", "payload": { ... }}.
     */
    void send_json_message(
        const std::string &type,
        const vix::json::kvs &payload)
    {
      nlohmann::json payloadJson = detail::ws_kvs_to_nlohmann(payload);

      nlohmann::json j{
          {"type", type},
          {"payload", payloadJson},
      };

      send_text(j.dump());
    }

    /**
     * @brief Send a typed JSON message using an initializer-list of tokens.
     */
    void send_json_message(
        const std::string &type,
        std::initializer_list<vix::json::token> payloadTokens)
    {
      vix::json::kvs payload{payloadTokens};
      send_json_message(type, payload);
    }

    /**
     * @brief Send a WebSocket ping frame asynchronously.
     */
    void send_ping()
    {
      if (!connected_.load(std::memory_order_relaxed) ||
          closing_.load(std::memory_order_relaxed))
      {
        return;
      }

      {
        std::lock_guard<std::mutex> lock(writeMutex_);
        writeQueue_.push_back(detail::build_ping_frame(true /* masked */));
      }

      trigger_write_flush_();
    }

    /**
     * @brief Close the connection and stop all threads.
     *
     * Safe to call multiple times.
     */
    void close()
    {
      if (closing_.exchange(true))
      {
        return;
      }

      alive_.store(false, std::memory_order_relaxed);
      heartbeatStop_.store(true, std::memory_order_relaxed);
      autoReconnect_.store(false, std::memory_order_relaxed);
      reconnectScheduled_.store(false, std::memory_order_relaxed);

      closeCancel_.request_cancel();
      writeCancel_.request_cancel();
      readCancel_.request_cancel();

      auto self = shared_from_this();
      if (ioc_)
      {
        vix::async::core::spawn_detached(
            *ioc_, [self]() -> task<void>
            {
                                try
                                {
                                  if (self->connected_.load(std::memory_order_relaxed))
                                  {
                                    std::vector<std::byte> closeFrame =
                                        detail::build_close_frame(true /* masked */);

                                    if (self->stream_ && self->stream_->is_open())
                                    {
                                      co_await self->stream_->async_write(
                                          std::span<const std::byte>(
                                              closeFrame.data(),
                                              closeFrame.size()),
                                          self->closeCancel_.token());
                                    }
                                  }
                                }
                                catch (...)
                                {
                                }

                                try
                                {
                                  if (self->stream_)
                                  {
                                    self->stream_->close();
                                  }
                                }
                                catch (...)
                                {
                                }

                                co_return; }());
      }

      if (ioc_)
      {
        ioc_->stop();
      }

      if (ioThread_.joinable() &&
          std::this_thread::get_id() != ioThread_.get_id())
      {
        ioThread_.join();
      }

      if (heartbeatThread_.joinable() &&
          std::this_thread::get_id() != heartbeatThread_.get_id())
      {
        heartbeatThread_.join();
      }

      connected_.store(false, std::memory_order_relaxed);
      started_.store(false, std::memory_order_relaxed);

      if (onClose_)
      {
        onClose_();
      }
    }

    /**
     * @brief Destructor calls close() and swallows exceptions.
     */
    ~Client()
    {
      try
      {
        close();
      }
      catch (...)
      {
      }
    }

    /**
     * @brief Send a typed JSON message.
     */
    void send(
        const std::string &type,
        const vix::json::kvs &payload)
    {
      send_json_message(type, payload);
    }

    /**
     * @brief Send a typed JSON message using an initializer-list of tokens.
     */
    void send(
        const std::string &type,
        std::initializer_list<vix::json::token> payloadTokens)
    {
      vix::json::kvs payload{payloadTokens};
      send_json_message(type, payload);
    }

    /** @brief Returns whether the client is currently connected. */
    bool is_connected() const noexcept
    {
      return connected_.load(std::memory_order_relaxed);
    }

  private:
    Client(std::string host, std::string port, std::string target)
        : host_(std::move(host)),
          port_(std::move(port)),
          target_(std::move(target))
    {
      if (target_.empty())
      {
        target_ = "/";
      }
      else if (target_.front() != '/')
      {
        target_.insert(target_.begin(), '/');
      }
    }

    /** @brief Reset runtime state, stream, resolver and cancellation scopes. */
    void init_runtime_()
    {
      if (ioc_)
      {
        ioc_->stop();
      }

      if (ioThread_.joinable() &&
          std::this_thread::get_id() != ioThread_.get_id())
      {
        ioThread_.join();
      }

      if (heartbeatThread_.joinable() &&
          std::this_thread::get_id() != heartbeatThread_.get_id())
      {
        heartbeatThread_.join();
      }

      ioc_ = std::make_shared<io_context>();
      resolver_ = make_dns_resolver(*ioc_);
      stream_ = make_tcp_stream(*ioc_);

      readBuffer_.clear();

      {
        std::lock_guard<std::mutex> lock(writeMutex_);
        writeQueue_.clear();
        writeInProgress_ = false;
      }

      connected_.store(false, std::memory_order_relaxed);
      closing_.store(false, std::memory_order_relaxed);
      heartbeatStop_.store(false, std::memory_order_relaxed);

      readCancel_ = cancel_source{};
      writeCancel_ = cancel_source{};
      closeCancel_ = cancel_source{};
    }

    /** @brief Full connect flow: resolve, connect, handshake, open callbacks, heartbeat, read loop. */
    task<void> run_connect_flow_()
    {
      try
      {
        co_await do_resolve_();
        co_await do_connect_();
        co_await do_handshake_();

        connected_.store(true, std::memory_order_relaxed);
        reconnectScheduled_.store(false, std::memory_order_relaxed);

        if (onOpen_)
        {
          onOpen_();
        }

        if (heartbeatEnabled_.load(std::memory_order_relaxed))
        {
          start_heartbeat_();
        }

        co_await do_read_loop_();
      }
      catch (const std::exception &e)
      {
        connected_.store(false, std::memory_order_relaxed);
        emit_error_("connect_flow", e.what());
        maybe_schedule_reconnect_("connect_flow");
      }

      co_return;
    }

    /** @brief Resolve host and port via Vix DNS. */
    task<void> do_resolve_()
    {
      if (!resolver_)
      {
        throw std::runtime_error("resolver not initialized");
      }

      const std::uint16_t port_num = static_cast<std::uint16_t>(std::stoi(port_));
      auto results = co_await resolver_->async_resolve(host_, port_num);

      if (results.empty())
      {
        throw std::runtime_error("dns resolve returned no endpoints");
      }

      resolved_ = std::move(results);
      co_return;
    }

    /** @brief Connect the underlying native TCP stream. */
    task<void> do_connect_()
    {
      if (!stream_)
      {
        throw std::runtime_error("tcp stream not initialized");
      }

      if (resolved_.empty())
      {
        throw std::runtime_error("no resolved addresses available");
      }

      const auto &addr = resolved_.front();

      tcp_endpoint ep{};
      ep.host = addr.ip;
      ep.port = addr.port;

      co_await stream_->async_connect(ep);
      co_return;
    }

    /** @brief Perform the HTTP Upgrade handshake to WebSocket. */
    task<void> do_handshake_()
    {
      if (!stream_ || !stream_->is_open())
      {
        throw std::runtime_error("stream is not connected");
      }

      handshakeKey_ = detail::generate_websocket_key();

      std::string req;
      req.reserve(512);
      req += "GET " + target_ + " HTTP/1.1\r\n";
      req += "Host: " + host_ + ":" + port_ + "\r\n";
      req += "Upgrade: websocket\r\n";
      req += "Connection: Upgrade\r\n";
      req += "Sec-WebSocket-Key: " + handshakeKey_ + "\r\n";
      req += "Sec-WebSocket-Version: 13\r\n";
      req += "User-Agent: Vix.cpp\r\n";
      req += "\r\n";

      const std::byte *ptr =
          reinterpret_cast<const std::byte *>(req.data());

      std::size_t written = 0;
      while (written < req.size())
      {
        const auto n = co_await stream_->async_write(
            std::span<const std::byte>(ptr + written, req.size() - written));
        if (n == 0)
        {
          throw std::runtime_error("websocket handshake write failed");
        }
        written += n;
      }

      const std::string raw_response = co_await read_http_head_();
      detail::validate_handshake_response(raw_response, handshakeKey_);
      co_return;
    }

    /** @brief Read loop for incoming WebSocket frames. */
    task<void> do_read_loop_()
    {
      while (alive_.load(std::memory_order_relaxed) &&
             !closing_.load(std::memory_order_relaxed) &&
             stream_ &&
             stream_->is_open())
      {
        try
        {
          detail::Frame frame = co_await read_frame_();

          switch (frame.opcode)
          {
          case detail::Opcode::Text:
            if (onMessage_)
            {
              onMessage_(frame.text());
            }
            break;

          case detail::Opcode::Ping:
            co_await write_raw_frame_(detail::build_pong_frame(frame.payload, true /* masked */));
            break;

          case detail::Opcode::Pong:
            break;

          case detail::Opcode::Close:
            connected_.store(false, std::memory_order_relaxed);
            co_await close_stream_only_();
            if (onClose_)
            {
              onClose_();
            }
            maybe_schedule_reconnect_("close");
            co_return;

          default:
            break;
          }
        }
        catch (const std::exception &e)
        {
          connected_.store(false, std::memory_order_relaxed);
          emit_error_("read", e.what());

          if (onClose_)
          {
            onClose_();
          }

          maybe_schedule_reconnect_("read");
          co_return;
        }
      }

      co_return;
    }

    /** @brief Start heartbeat thread that periodically sends ping. */
    void start_heartbeat_()
    {
      if (heartbeatThread_.joinable())
      {
        return;
      }

      auto self = shared_from_this();
      heartbeatThread_ = std::thread(
          [self]()
          {
            while (!self->heartbeatStop_.load(std::memory_order_relaxed) &&
                   self->alive_.load(std::memory_order_relaxed))
            {
              std::this_thread::sleep_for(self->heartbeatInterval_);

              if (!self->connected_.load(std::memory_order_relaxed) ||
                  self->closing_.load(std::memory_order_relaxed) ||
                  self->heartbeatStop_.load(std::memory_order_relaxed))
              {
                continue;
              }

              self->send_ping();
            }
          });
    }

    /** @brief Schedule a reconnect attempt if enabled and appropriate. */
    void maybe_schedule_reconnect_(const char *stage)
    {
      if (!autoReconnect_.load(std::memory_order_relaxed) ||
          closing_.load(std::memory_order_relaxed) ||
          !alive_.load(std::memory_order_relaxed))
      {
        return;
      }

      bool expected = false;
      if (!reconnectScheduled_.compare_exchange_strong(expected, true))
      {
        return;
      }

      auto self = shared_from_this();
      std::thread(
          [self, stage]()
          {
            (void)stage;
            std::this_thread::sleep_for(self->reconnectDelay_);

            if (!self->alive_.load(std::memory_order_relaxed) ||
                self->closing_.load(std::memory_order_relaxed))
            {
              self->reconnectScheduled_.store(false, std::memory_order_relaxed);
              return;
            }

            self->started_.store(false, std::memory_order_relaxed);
            self->reconnectScheduled_.store(false, std::memory_order_relaxed);
            self->connect();
          })
          .detach();
    }

    /** @brief Trigger async queue flush if not already active. */
    void trigger_write_flush_()
    {
      if (!ioc_)
      {
        return;
      }

      auto self = shared_from_this();
      vix::async::core::spawn_detached(*ioc_, [self]() -> task<void>
                                       {
                                         bool should_start = false;
                                         {
                                           std::lock_guard<std::mutex> lock(self->writeMutex_);
                                           if (!self->writeInProgress_ && !self->writeQueue_.empty())
                                           {
                                             self->writeInProgress_ = true;
                                             should_start = true;
                                           }
                                         }

                                         if (should_start)
                                         {
                                           co_await self->do_write_loop_();
                                         }

                                         co_return; }());
    }

    /** @brief Write loop for queued WebSocket frames. */
    task<void> do_write_loop_()
    {
      while (true)
      {
        std::vector<std::byte> current;

        {
          std::lock_guard<std::mutex> lock(writeMutex_);
          if (writeQueue_.empty())
          {
            writeInProgress_ = false;
            co_return;
          }

          current = std::move(writeQueue_.front());
          writeQueue_.pop_front();
        }

        try
        {
          co_await write_raw_frame_(current);
        }
        catch (const std::exception &e)
        {
          {
            std::lock_guard<std::mutex> lock(writeMutex_);
            writeInProgress_ = false;
          }

          emit_error_("write", e.what());
          maybe_schedule_reconnect_("write");
          co_return;
        }
      }
    }

    /** @brief Write one raw WebSocket frame. */
    task<void> write_raw_frame_(const std::vector<std::byte> &frame)
    {
      if (!stream_ || !stream_->is_open())
      {
        throw std::runtime_error("stream not open");
      }

      std::size_t written = 0;
      while (written < frame.size())
      {
        const auto n = co_await stream_->async_write(
            std::span<const std::byte>(
                frame.data() + written,
                frame.size() - written),
            writeCancel_.token());

        if (n == 0)
        {
          throw std::runtime_error("websocket frame write failed");
        }

        written += n;
      }

      co_return;
    }

    /** @brief Read one WebSocket frame from the stream. */
    task<detail::Frame> read_frame_()
    {
      co_await ensure_bytes_(2);

      detail::FrameHeader h =
          detail::parse_frame_header(
              reinterpret_cast<const std::byte *>(readBuffer_.data()),
              readBuffer_.size());

      const std::size_t frame_size = h.header_size + h.payload_length;
      co_await ensure_bytes_(frame_size);

      std::vector<std::byte> bytes(frame_size);
      std::memcpy(bytes.data(), readBuffer_.data(), frame_size);
      readBuffer_.erase(0, frame_size);

      co_return detail::decode_frame(bytes);
    }

    /** @brief Ensure at least n bytes exist in the read buffer. */
    task<void> ensure_bytes_(std::size_t n)
    {
      while (readBuffer_.size() < n)
      {
        std::array<std::byte, 8192> chunk{};
        const auto r = co_await stream_->async_read(
            std::span<std::byte>(chunk.data(), chunk.size()),
            readCancel_.token());

        if (r == 0)
        {
          throw std::runtime_error("unexpected EOF while reading websocket data");
        }

        readBuffer_.append(
            reinterpret_cast<const char *>(chunk.data()),
            r);
      }

      co_return;
    }

    /** @brief Read one HTTP response head during handshake. */
    task<std::string> read_http_head_()
    {
      while (true)
      {
        const auto pos = readBuffer_.find("\r\n\r\n");
        if (pos != std::string::npos)
        {
          const std::size_t end = pos + 4;
          std::string out = readBuffer_.substr(0, end);
          readBuffer_.erase(0, end);
          co_return out;
        }

        std::array<std::byte, 4096> chunk{};
        const auto r = co_await stream_->async_read(
            std::span<std::byte>(chunk.data(), chunk.size()),
            readCancel_.token());

        if (r == 0)
        {
          throw std::runtime_error("unexpected EOF during websocket handshake");
        }

        readBuffer_.append(
            reinterpret_cast<const char *>(chunk.data()),
            r);

        if (readBuffer_.size() > 64 * 1024)
        {
          throw std::runtime_error("websocket handshake response too large");
        }
      }
    }

    /** @brief Close only the TCP stream. */
    task<void> close_stream_only_()
    {
      try
      {
        if (stream_)
        {
          stream_->close();
        }
      }
      catch (...)
      {
      }

      co_return;
    }

    /** @brief Emit an error through callback. */
    void emit_error_(const char *stage, const std::string &message)
    {
      if (onError_)
      {
        onError_(std::string(stage) + ": " + message);
      }
    }

  private:
    std::string host_;
    std::string port_;
    std::string target_;

    std::shared_ptr<io_context> ioc_{};
    std::unique_ptr<dns_resolver> resolver_{};
    std::unique_ptr<tcp_stream> stream_{};

    std::vector<resolved_address> resolved_{};
    std::string readBuffer_{};
    std::string handshakeKey_{};

    std::thread ioThread_{};
    std::thread heartbeatThread_{};

    std::atomic<bool> started_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> closing_{false};
    std::atomic<bool> alive_{true};
    std::atomic<bool> heartbeatEnabled_{false};
    std::atomic<bool> heartbeatStop_{false};
    std::chrono::seconds heartbeatInterval_{30};
    std::atomic<bool> autoReconnect_{false};
    std::chrono::seconds reconnectDelay_{3};
    std::atomic<bool> reconnectScheduled_{false};

    cancel_source readCancel_{};
    cancel_source writeCancel_{};
    cancel_source closeCancel_{};

    OpenHandler onOpen_{};
    MessageHandler onMessage_{};
    CloseHandler onClose_{};
    ErrorHandler onError_{};

    std::mutex writeMutex_{};
    std::deque<std::vector<std::byte>> writeQueue_{};
    bool writeInProgress_{false};
  };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_CLIENT_HPP
