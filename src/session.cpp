/**
 *
 *  @file session.cpp
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
#include <vix/websocket/session.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstring>
#include <cerrno>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <vix/async/core/spawn.hpp>

namespace vix::websocket
{
  using Logger = vix::utils::Logger;
  using vix::async::core::spawn_detached;

  namespace
  {
    inline Logger &log()
    {
      return Logger::getInstance();
    }

    inline std::string to_lower_copy(std::string s)
    {
      std::transform(
          s.begin(),
          s.end(),
          s.begin(),
          [](unsigned char c)
          {
            return static_cast<char>(std::tolower(c));
          });

      return s;
    }

    inline std::string trim_copy(std::string s)
    {
      while (!s.empty() &&
             std::isspace(static_cast<unsigned char>(s.front())))
      {
        s.erase(s.begin());
      }

      while (!s.empty() &&
             std::isspace(static_cast<unsigned char>(s.back())))
      {
        s.pop_back();
      }

      return s;
    }

    inline bool starts_with_icase(std::string_view s, std::string_view prefix)
    {
      if (prefix.size() > s.size())
      {
        return false;
      }

      for (std::size_t i = 0; i < prefix.size(); ++i)
      {
        const auto a = static_cast<unsigned char>(s[i]);
        const auto b = static_cast<unsigned char>(prefix[i]);

        if (std::tolower(a) != std::tolower(b))
        {
          return false;
        }
      }

      return true;
    }

    inline std::string get_header_value(
        const std::string &raw_http,
        const std::string &header_name)
    {
      const std::string needle = to_lower_copy(header_name) + ":";

      std::size_t pos = 0;
      while (pos < raw_http.size())
      {
        const std::size_t line_end = raw_http.find("\r\n", pos);
        const std::size_t end =
            (line_end == std::string::npos) ? raw_http.size() : line_end;

        std::string line = raw_http.substr(pos, end - pos);
        std::string lower = to_lower_copy(line);

        if (lower.rfind(needle, 0) == 0)
        {
          return trim_copy(line.substr(needle.size()));
        }

        if (line_end == std::string::npos)
        {
          break;
        }

        pos = line_end + 2;
      }

      return {};
    }

    inline bool header_contains_token(
        const std::string &raw_http,
        const std::string &header_name,
        const std::string &token)
    {
      const std::string value =
          to_lower_copy(get_header_value(raw_http, header_name));
      const std::string wanted = to_lower_copy(token);

      if (value.empty())
      {
        return false;
      }

      std::size_t start = 0;
      while (start < value.size())
      {
        const std::size_t comma = value.find(',', start);
        std::string part =
            (comma == std::string::npos)
                ? value.substr(start)
                : value.substr(start, comma - start);

        part = trim_copy(part);
        if (part == wanted)
        {
          return true;
        }

        if (comma == std::string::npos)
        {
          break;
        }

        start = comma + 1;
      }

      return false;
    }

    inline std::size_t find_http_head_end(const std::string &s)
    {
      const auto pos = s.find("\r\n\r\n");
      if (pos == std::string::npos)
      {
        return std::string::npos;
      }

      return pos + 4;
    }
  } // namespace

  Session::Session(
      std::unique_ptr<tcp_stream> stream,
      const Config &cfg,
      std::shared_ptr<Router> router,
      std::shared_ptr<vix::executor::IExecutor> executor)
      : stream_(std::move(stream)),
        cfg_(cfg),
        router_(std::move(router)),
        executor_(std::move(executor)),
        ioc_(std::make_shared<io_context>())
  {
    if (!executor_)
    {
      throw std::invalid_argument(
          "vix::websocket::Session requires a valid executor");
    }

    auto ctx = ioc_;
    std::thread(
        [ctx]()
        {
          try
          {
            ctx->run();
          }
          catch (...)
          {
          }
        })
        .detach();
  }

  task<void> Session::run()
  {
    bool must_close = false;

    try
    {
      co_await do_accept();

      if (!open_)
      {
        co_return;
      }

      maybe_start_heartbeat();
      co_await do_read_loop();
    }
    catch (const std::exception &e)
    {
      emit_error(e.what());
      must_close = true;
    }

    if (must_close)
    {
      co_await close_stream_only();
    }

    co_return;
  }

  void Session::send_text(std::string_view text)
  {
    do_enqueue_message(false, std::string{text});
  }

  void Session::send_binary(const void *data, std::size_t size)
  {
    if (data == nullptr || size == 0)
    {
      do_enqueue_message(true, {});
      return;
    }

    const auto *ptr = static_cast<const char *>(data);
    do_enqueue_message(true, std::string(ptr, ptr + size));
  }

  void Session::close(std::string)
  {
    if (closing_)
    {
      return;
    }

    closing_ = true;
    cancel_idle_timer();
    stop_heartbeat();

    if (ioc_)
    {
      auto self = shared_from_this();
      spawn_detached(
          *ioc_,
          [self]() -> task<void>
          {
            try
            {
              co_await self->write_raw_frame(
                  detail::build_close_frame(false));
            }
            catch (...)
            {
            }

            co_await self->close_stream_only();
            co_return;
          }());
    }
  }

  task<void> Session::do_accept()
  {
    const std::string raw_head = co_await read_http_head();

    if (raw_head.empty())
    {
      throw std::runtime_error("empty websocket handshake request");
    }

    const std::size_t first_line_end = raw_head.find("\r\n");
    const std::string request_line =
        first_line_end == std::string::npos
            ? raw_head
            : raw_head.substr(0, first_line_end);

    if (!starts_with_icase(request_line, "GET "))
    {
      throw std::runtime_error("websocket handshake must use GET");
    }

    const std::string upgrade =
        to_lower_copy(get_header_value(raw_head, "Upgrade"));
    if (upgrade != "websocket")
    {
      throw std::runtime_error("missing Upgrade: websocket");
    }

    if (!header_contains_token(raw_head, "Connection", "Upgrade"))
    {
      throw std::runtime_error("missing Connection: Upgrade");
    }

    const std::string ws_key =
        trim_copy(get_header_value(raw_head, "Sec-WebSocket-Key"));
    if (ws_key.empty())
    {
      throw std::runtime_error("missing Sec-WebSocket-Key");
    }

    const std::string version =
        trim_copy(get_header_value(raw_head, "Sec-WebSocket-Version"));
    if (!version.empty() && version != "13")
    {
      throw std::runtime_error("unsupported Sec-WebSocket-Version");
    }

    const std::string accept_key = detail::websocket_accept_from_key(ws_key);
    co_await send_upgrade_response(accept_key);

    open_ = true;
    closing_ = false;

    if (router_)
    {
      router_->handle_open(*this);
    }

    arm_idle_timer();
    co_return;
  }

  task<void> Session::do_read_loop()
  {
    while (!closing_ &&
           stream_ &&
           stream_->is_open())
    {
      detail::Frame frame = co_await read_frame();
      cancel_idle_timer();

      switch (frame.opcode)
      {
      case detail::Opcode::Text:
        if (router_)
        {
          router_->handle_message(*this, frame.text());
        }
        arm_idle_timer();
        break;

      case detail::Opcode::Binary:
        if (router_)
        {
          router_->handle_message(*this, frame.text());
        }
        arm_idle_timer();
        break;

      case detail::Opcode::Ping:
        if (cfg_.autoPingPong)
        {
          co_await write_raw_frame(
              detail::build_pong_frame(frame.payload, false));
        }
        arm_idle_timer();
        break;

      case detail::Opcode::Pong:
        arm_idle_timer();
        break;

      case detail::Opcode::Close:
        closing_ = true;
        open_ = false;
        stop_heartbeat();

        if (router_)
        {
          router_->handle_close(*this);
        }

        co_await close_stream_only();
        co_return;

      case detail::Opcode::Continuation:
      default:
        arm_idle_timer();
        break;
      }
    }

    open_ = false;
    stop_heartbeat();

    if (router_)
    {
      router_->handle_close(*this);
    }

    co_await close_stream_only();
    co_return;
  }

  task<detail::Frame> Session::read_frame()
  {
    co_await ensure_bytes(2);

    const detail::FrameHeader h =
        detail::parse_frame_header(
            reinterpret_cast<const std::byte *>(readBuffer_.data()),
            readBuffer_.size());

    const std::size_t frame_size = h.header_size + h.payload_length;
    co_await ensure_bytes(frame_size);

    std::vector<std::byte> bytes(frame_size);
    std::memcpy(bytes.data(), readBuffer_.data(), frame_size);
    readBuffer_.erase(0, frame_size);

    co_return detail::decode_frame(bytes);
  }

  void Session::arm_idle_timer()
  {
    if (cfg_.idleTimeout.count() <= 0)
    {
      return;
    }

    idleCancel_ = cancel_source{};
    const auto ct = idleCancel_.token();
    const auto timeout = cfg_.idleTimeout;
    auto weak_self = weak_from_this();

    std::thread(
        [weak_self, timeout, ct]()
        {
          constexpr auto step = std::chrono::milliseconds(100);
          auto waited = std::chrono::milliseconds(0);

          while (waited < timeout)
          {
            if (ct.is_cancelled())
            {
              return;
            }

            const auto remain = timeout - waited;
            const auto sleep_for = (remain < step) ? remain : step;
            std::this_thread::sleep_for(sleep_for);
            waited += std::chrono::duration_cast<std::chrono::milliseconds>(sleep_for);
          }

          if (ct.is_cancelled())
          {
            return;
          }

          auto self = weak_self.lock();
          if (!self || !self->ioc_)
          {
            return;
          }

          spawn_detached(
              *self->ioc_,
              [self]() -> task<void>
              {
                co_await self->on_idle_timeout();
              }());
        })
        .detach();
  }

  void Session::cancel_idle_timer()
  {
    idleCancel_.request_cancel();
  }

  task<void> Session::on_idle_timeout()
  {
    if (closing_ || !stream_ || !stream_->is_open())
    {
      co_return;
    }

    log().log(Logger::Level::Debug, "[ws] disconnected (idle timeout)");

    closing_ = true;
    open_ = false;
    stop_heartbeat();

    try
    {
      co_await write_raw_frame(detail::build_close_frame(false));
    }
    catch (...)
    {
    }

    if (router_)
    {
      router_->handle_close(*this);
    }

    co_await close_stream_only();
    co_return;
  }

  void Session::do_enqueue_message(bool isBinary, std::string payload)
  {
    if (closing_)
    {
      return;
    }

    {
      std::lock_guard<std::mutex> lock(writeMutex_);
      writeQueue_.push_back(PendingMessage{
          isBinary,
          std::move(payload),
      });
    }

    trigger_write_flush();
  }

  task<void> Session::do_write_next()
  {
    while (true)
    {
      PendingMessage msg{};

      {
        std::lock_guard<std::mutex> lock(writeMutex_);
        if (closing_)
        {
          writeQueue_.clear();
          writeInProgress_ = false;
          co_return;
        }

        if (writeQueue_.empty())
        {
          writeInProgress_ = false;
          co_return;
        }

        msg = std::move(writeQueue_.front());
        writeQueue_.pop_front();
      }

      std::vector<std::byte> frame;
      if (msg.isBinary)
      {
        std::vector<std::byte> payload;
        payload.reserve(msg.data.size());

        for (char ch : msg.data)
        {
          payload.push_back(static_cast<std::byte>(
              static_cast<unsigned char>(ch)));
        }

        frame = detail::build_frame(
            detail::Opcode::Binary,
            payload,
            true,
            false);
      }
      else
      {
        frame = detail::build_text_frame(msg.data, false);
      }

      co_await write_raw_frame(frame);
    }
  }

  void Session::emit_error(const std::string &message)
  {
    std::string lower = message;
    std::transform(
        lower.begin(),
        lower.end(),
        lower.begin(),
        [](unsigned char c)
        {
          return static_cast<char>(std::tolower(c));
        });

    if (lower.find("end of file") != std::string::npos ||
        lower.find("eof") != std::string::npos ||
        lower.find("broken pipe") != std::string::npos ||
        lower.find("connection reset") != std::string::npos)
    {
      log().log(
          Logger::Level::Debug,
          "[ws] client disconnected: {}",
          message);
      return;
    }

    log().log(
        Logger::Level::Error,
        "[ws] {}",
        message);

    if (router_)
    {
      router_->handle_error(*this, message);
    }
  }

  void Session::trigger_write_flush()
  {
    bool should_start = false;

    {
      std::lock_guard<std::mutex> lock(writeMutex_);
      if (!writeInProgress_ && !writeQueue_.empty())
      {
        writeInProgress_ = true;
        should_start = true;
      }
    }

    if (!should_start || !ioc_)
    {
      return;
    }

    auto self = shared_from_this();
    spawn_detached(
        *ioc_,
        [self]() -> task<void>
        {
          bool must_close = false;

          try
          {
            co_await self->do_write_next();
          }
          catch (const std::exception &e)
          {
            {
              std::lock_guard<std::mutex> lock(self->writeMutex_);
              self->writeInProgress_ = false;
            }

            self->emit_error(e.what());
            must_close = true;
          }

          if (must_close)
          {
            co_await self->close_stream_only();
          }

          co_return;
        }());
  }

  task<void> Session::write_raw_frame(const std::vector<std::byte> &frame)
  {
    if (!stream_ || !stream_->is_open())
    {
      throw std::runtime_error("stream not open");
    }

    std::size_t written = 0;
    while (written < frame.size())
    {
      const std::size_t n = co_await stream_->async_write(
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

  task<std::string> Session::read_http_head()
  {
    constexpr std::size_t MAX_HTTP_HEAD = 64 * 1024;

    while (true)
    {
      const auto pos = find_http_head_end(readBuffer_);
      if (pos != std::string::npos)
      {
        std::string out = readBuffer_.substr(0, pos);
        readBuffer_.erase(0, pos);
        co_return out;
      }

      if (readBuffer_.size() > MAX_HTTP_HEAD)
      {
        throw std::runtime_error("websocket HTTP head too large");
      }

      std::array<std::byte, 8192> chunk{};
      const auto n = co_await stream_->async_read(
          std::span<std::byte>(chunk.data(), chunk.size()),
          readCancel_.token());

      if (n == 0)
      {
        throw std::runtime_error("unexpected EOF while reading websocket HTTP head");
      }

      readBuffer_.append(
          reinterpret_cast<const char *>(chunk.data()),
          n);
    }
  }

  task<void> Session::ensure_bytes(std::size_t n)
  {
    while (readBuffer_.size() < n)
    {
      std::array<std::byte, 8192> chunk{};
      const auto r = co_await stream_->async_read(
          std::span<std::byte>(chunk.data(), chunk.size()),
          readCancel_.token());

      if (r == 0)
      {
        throw std::system_error(std::make_error_code(std::errc::connection_reset));
      }

      readBuffer_.append(
          reinterpret_cast<const char *>(chunk.data()),
          r);
    }

    co_return;
  }

  task<void> Session::send_upgrade_response(const std::string &accept_key)
  {
    std::string res;
    res.reserve(256);
    res += "HTTP/1.1 101 Switching Protocols\r\n";
    res += "Upgrade: websocket\r\n";
    res += "Connection: Upgrade\r\n";
    res += "Sec-WebSocket-Accept: " + accept_key + "\r\n";
    res += "Server: Vix.cpp\r\n";
    res += "\r\n";

    const auto *ptr =
        reinterpret_cast<const std::byte *>(res.data());

    std::size_t written = 0;
    while (written < res.size())
    {
      const auto n = co_await stream_->async_write(
          std::span<const std::byte>(ptr + written, res.size() - written),
          writeCancel_.token());

      if (n == 0)
      {
        throw std::runtime_error("websocket handshake write failed");
      }

      written += n;
    }

    co_return;
  }

  task<void> Session::close_stream_only()
  {
    open_ = false;
    closing_ = true;

    cancel_idle_timer();
    stop_heartbeat();

    readCancel_.request_cancel();
    writeCancel_.request_cancel();
    closeCancel_.request_cancel();

    if (stream_ && stream_->is_open())
    {
      stream_->close();
    }

    co_return;
  }

  void Session::maybe_start_heartbeat()
  {
    stop_heartbeat();

    if (cfg_.idleTimeout.count() <= 0)
    {
      return;
    }

    heartbeatStop_ = false;
    auto weak_self = weak_from_this();
    const auto interval = cfg_.idleTimeout / 2;

    if (interval.count() <= 0)
    {
      return;
    }

    heartbeatThread_ = std::thread(
        [weak_self, interval]()
        {
          constexpr auto step = std::chrono::milliseconds(100);
          auto waited = std::chrono::milliseconds(0);

          while (true)
          {
            auto self = weak_self.lock();
            if (!self)
            {
              return;
            }

            while (waited < interval)
            {
              if (self->heartbeatStop_ || self->closing_ || !self->open_)
              {
                return;
              }

              const auto remain = interval - waited;
              const auto sleep_for = (remain < step) ? remain : step;
              std::this_thread::sleep_for(sleep_for);
              waited += std::chrono::duration_cast<std::chrono::milliseconds>(sleep_for);
            }

            waited = std::chrono::milliseconds(0);

            if (self->heartbeatStop_ || self->closing_ || !self->open_)
            {
              return;
            }

            if (!self->ioc_)
            {
              return;
            }

            auto keep = self;
            spawn_detached(
                *self->ioc_,
                [keep]() -> task<void>
                {
                  try
                  {
                    if (keep->cfg_.autoPingPong)
                    {
                      co_await keep->write_raw_frame(
                          detail::build_ping_frame(false));
                    }
                  }
                  catch (const std::exception &e)
                  {
                    keep->emit_error(e.what());
                  }

                  co_return;
                }());
          }
        });
  }

  void Session::stop_heartbeat()
  {
    heartbeatStop_ = true;

    if (heartbeatThread_.joinable())
    {
      heartbeatThread_.join();
    }
  }

} // namespace vix::websocket
