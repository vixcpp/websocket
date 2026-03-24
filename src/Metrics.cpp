/**
 *
 *  @file Metrics.cpp
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
#include <vix/websocket/Metrics.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <vix/async/core/io_context.hpp>
#include <vix/async/core/spawn.hpp>
#include <vix/async/core/task.hpp>
#include <vix/async/net/tcp.hpp>
#include <vix/http/Response.hpp>
#include <vix/http/Status.hpp>
#include <vix/utils/Logger.hpp>

namespace vix::websocket
{
  using vix::async::core::io_context;
  using vix::async::core::spawn_detached;
  using vix::async::core::task;
  using vix::async::net::make_tcp_listener;
  using vix::async::net::tcp_endpoint;
  using vix::async::net::tcp_listener;
  using vix::async::net::tcp_stream;

  namespace
  {
    struct ParsedHttpRequest
    {
      std::string method{};
      std::string target{};
      std::string version{"HTTP/1.1"};
    };

    [[nodiscard]] bool is_metrics_request(const ParsedHttpRequest &req) noexcept
    {
      return req.method == "GET" && req.target == "/metrics";
    }

    [[nodiscard]] std::string trim(std::string s)
    {
      auto is_space = [](unsigned char c)
      { return std::isspace(c) != 0; };

      while (!s.empty() && is_space(static_cast<unsigned char>(s.front())))
      {
        s.erase(s.begin());
      }

      while (!s.empty() && is_space(static_cast<unsigned char>(s.back())))
      {
        s.pop_back();
      }

      return s;
    }

    [[nodiscard]] std::string to_upper(std::string s)
    {
      for (char &c : s)
      {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      }
      return s;
    }

    [[nodiscard]] std::optional<ParsedHttpRequest> parse_http_request_head(const std::string &raw)
    {
      const auto line_end = raw.find("\r\n");
      if (line_end == std::string::npos)
      {
        return std::nullopt;
      }

      const std::string_view request_line(raw.data(), line_end);

      const auto sp1 = request_line.find(' ');
      if (sp1 == std::string_view::npos)
      {
        return std::nullopt;
      }

      const auto sp2 = request_line.find(' ', sp1 + 1);
      if (sp2 == std::string_view::npos)
      {
        return std::nullopt;
      }

      ParsedHttpRequest req;
      req.method = to_upper(std::string(request_line.substr(0, sp1)));
      req.target = std::string(request_line.substr(sp1 + 1, sp2 - sp1 - 1));
      req.version = trim(std::string(request_line.substr(sp2 + 1)));

      if (req.method.empty() || req.target.empty() || req.version.empty())
      {
        return std::nullopt;
      }

      return req;
    }

    task<std::string> read_http_head(std::unique_ptr<tcp_stream> &stream)
    {
      std::string buffer;
      buffer.reserve(4096);

      while (true)
      {
        const auto end = buffer.find("\r\n\r\n");
        if (end != std::string::npos)
        {
          co_return buffer.substr(0, end + 4);
        }

        std::array<std::byte, 4096> chunk{};
        const auto n = co_await stream->async_read(
            std::span<std::byte>(chunk.data(), chunk.size()));

        if (n == 0)
        {
          throw std::runtime_error("unexpected EOF while reading metrics HTTP request");
        }

        buffer.append(reinterpret_cast<const char *>(chunk.data()), n);

        if (buffer.size() > 64 * 1024)
        {
          throw std::runtime_error("metrics HTTP request head too large");
        }
      }
    }

    task<void> write_all(std::unique_ptr<tcp_stream> &stream, std::string_view data)
    {
      std::size_t written = 0;

      while (written < data.size())
      {
        const auto n = co_await stream->async_write(
            std::span<const std::byte>(
                reinterpret_cast<const std::byte *>(data.data() + written),
                data.size() - written));

        if (n == 0)
        {
          throw std::runtime_error("metrics HTTP write failed");
        }

        written += n;
      }

      co_return;
    }

    std::string make_response_text(int status, std::string_view content_type, std::string body)
    {
      vix::vhttp::Response res;
      res.set_status(status);
      res.set_header("Server", "vix-ws-metrics");
      res.set_header("Cache-Control", "no-store");
      res.set_header("Connection", "close");
      res.set_header("Content-Type", std::string(content_type));
      res.set_should_close(true);
      res.set_body(std::move(body));
      return res.to_http_string();
    }

    task<void> handle_metrics_client(std::unique_ptr<tcp_stream> stream,
                                     WebSocketMetrics &metrics)
    {
      try
      {
        const std::string raw_req = co_await read_http_head(stream);
        const auto parsed = parse_http_request_head(raw_req);

        std::string wire;
        if (parsed && is_metrics_request(*parsed))
        {
          wire = make_response_text(
              vix::vhttp::OK,
              "text/plain; version=0.0.4; charset=utf-8",
              metrics.render_prometheus());
        }
        else
        {
          wire = make_response_text(
              vix::vhttp::NOT_FOUND,
              "text/plain; charset=utf-8",
              "Not Found\n");
        }

        co_await write_all(stream, wire);
      }
      catch (const std::exception &e)
      {
        vix::utils::Logger::getInstance().log(
            vix::utils::Logger::Level::Debug,
            "[ws] metrics client error ({})",
            e.what());
      }

      try
      {
        if (stream)
        {
          stream->close();
        }
      }
      catch (...)
      {
      }

      co_return;
    }

    task<void> metrics_accept_loop(std::shared_ptr<io_context> ioc,
                                   std::unique_ptr<tcp_listener> listener,
                                   WebSocketMetrics &metrics)
    {
      auto &log = vix::utils::Logger::getInstance();

      while (true)
      {
        try
        {
          auto stream = co_await listener->async_accept();
          if (!stream)
          {
            continue;
          }

          spawn_detached(*ioc, handle_metrics_client(std::move(stream), metrics));
        }
        catch (const std::exception &e)
        {
          log.log(vix::utils::Logger::Level::Debug,
                  "[ws] metrics accept error ({})",
                  e.what());
        }
      }
    }

  } // namespace

  std::string WebSocketMetrics::render_prometheus() const
  {
    std::ostringstream os;

    os << "# HELP vix_ws_connections_total Total WebSocket connections created\n"
       << "# TYPE vix_ws_connections_total counter\n"
       << "vix_ws_connections_total " << connections_total.load() << "\n\n"

       << "# HELP vix_ws_connections_active Current active WebSocket connections\n"
       << "# TYPE vix_ws_connections_active gauge\n"
       << "vix_ws_connections_active " << connections_active.load() << "\n\n"

       << "# HELP vix_ws_messages_in_total Total number of WebSocket messages received\n"
       << "# TYPE vix_ws_messages_in_total counter\n"
       << "vix_ws_messages_in_total " << messages_in_total.load() << "\n\n"

       << "# HELP vix_ws_messages_out_total Total number of WebSocket messages sent\n"
       << "# TYPE vix_ws_messages_out_total counter\n"
       << "vix_ws_messages_out_total " << messages_out_total.load() << "\n\n"

       << "# HELP vix_ws_errors_total Total number of WebSocket errors\n"
       << "# TYPE vix_ws_errors_total counter\n"
       << "vix_ws_errors_total " << errors_total.load() << "\n\n";

    os << "# HELP vix_ws_lp_sessions_total Total long-polling sessions ever created\n"
       << "# TYPE vix_ws_lp_sessions_total counter\n"
       << "vix_ws_lp_sessions_total " << lp_sessions_total.load() << "\n\n"

       << "# HELP vix_ws_lp_sessions_active Current active long-polling sessions\n"
       << "# TYPE vix_ws_lp_sessions_active gauge\n"
       << "vix_ws_lp_sessions_active " << lp_sessions_active.load() << "\n\n"

       << "# HELP vix_ws_lp_polls_total Total /ws/poll HTTP calls\n"
       << "# TYPE vix_ws_lp_polls_total counter\n"
       << "vix_ws_lp_polls_total " << lp_polls_total.load() << "\n\n"

       << "# HELP vix_ws_lp_messages_buffered Current buffered messages for long-polling\n"
       << "# TYPE vix_ws_lp_messages_buffered gauge\n"
       << "vix_ws_lp_messages_buffered " << lp_messages_buffered.load() << "\n\n"

       << "# HELP vix_ws_lp_messages_enqueued_total Total messages enqueued into long-poll buffers\n"
       << "# TYPE vix_ws_lp_messages_enqueued_total counter\n"
       << "vix_ws_lp_messages_enqueued_total " << lp_messages_enqueued_total.load() << "\n\n"

       << "# HELP vix_ws_lp_messages_drained_total Total messages drained via /ws/poll\n"
       << "# TYPE vix_ws_lp_messages_drained_total counter\n"
       << "vix_ws_lp_messages_drained_total " << lp_messages_drained_total.load() << "\n";

    return os.str();
  }

  void run_metrics_http_exporter(WebSocketMetrics &metrics,
                                 const std::string &address,
                                 std::uint16_t port)
  {
    auto &log = vix::utils::Logger::getInstance();

    try
    {
      auto ioc = std::make_shared<io_context>();
      auto listener = make_tcp_listener(*ioc);

      if (!listener)
      {
        throw std::runtime_error("failed to create metrics tcp listener");
      }

      tcp_endpoint ep{};
      ep.host = address;
      ep.port = port;

      auto listen_task = listener->async_listen(ep);
      std::move(listen_task).start(ioc->get_scheduler());

      log.log(vix::utils::Logger::Level::Info,
              "[ws] metrics listening {}:{}  (GET /metrics)",
              address,
              port);

      spawn_detached(*ioc, metrics_accept_loop(ioc, std::move(listener), metrics));
      ioc->run();
    }
    catch (const std::exception &e)
    {
      log.log(vix::utils::Logger::Level::Error,
              "[ws] metrics server error ({})",
              e.what());
    }
  }

} // namespace vix::websocket
