/**
 *
 * @file metrics_server_request_test.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira. All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license that can be found in the License file.
 *
 * Vix.cpp
 *
 */

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <vix/websocket/Metrics.hpp>

namespace
{
  using WebSocketMetrics =
      vix::websocket::WebSocketMetrics;

#if defined(_WIN32)
  using NativeSocket = SOCKET;

  static constexpr NativeSocket invalidSocket =
      INVALID_SOCKET;
#else
  using NativeSocket = int;

  static constexpr NativeSocket invalidSocket =
      -1;
#endif

  class SocketHandle
  {
  public:
    SocketHandle() noexcept = default;

    explicit SocketHandle(
        NativeSocket socket) noexcept
        : socket_{
              socket}
    {
    }

    SocketHandle(
        const SocketHandle &) = delete;

    SocketHandle &operator=(
        const SocketHandle &) = delete;

    SocketHandle(
        SocketHandle &&other) noexcept
        : socket_{
              std::exchange(
                  other.socket_,
                  invalidSocket)}
    {
    }

    SocketHandle &operator=(
        SocketHandle &&other) noexcept
    {
      if (this != &other)
      {
        close();

        socket_ =
            std::exchange(
                other.socket_,
                invalidSocket);
      }

      return *this;
    }

    ~SocketHandle()
    {
      close();
    }

    [[nodiscard]]
    bool valid() const noexcept
    {
      return socket_ != invalidSocket;
    }

    [[nodiscard]]
    NativeSocket get() const noexcept
    {
      return socket_;
    }

    void close() noexcept
    {
      if (!valid())
      {
        return;
      }

#if defined(_WIN32)
      closesocket(socket_);
#else
      ::close(socket_);
#endif

      socket_ = invalidSocket;
    }

  private:
    NativeSocket socket_{
        invalidSocket};
  };

  struct SocketRuntime
  {
    SocketRuntime()
    {
#if defined(_WIN32)
      WSADATA data{};

      const int result =
          WSAStartup(
              MAKEWORD(2, 2),
              &data);

      assert(result == 0);
#endif
    }

    ~SocketRuntime()
    {
#if defined(_WIN32)
      WSACleanup();
#endif
    }
  };

  struct HttpResponse
  {
    std::string statusLine{};
    int statusCode{0};

    std::unordered_map<
        std::string,
        std::string>
        headers{};

    std::string body{};
  };

  struct MetricsServerState
  {
    WebSocketMetrics metrics{};

    std::atomic<bool> returned{
        false};
  };

  static void set_env_var(
      const char *name,
      const std::string &value)
  {
#if defined(_WIN32)
    const std::string assignment =
        std::string{name} +
        "=" +
        value;

    const int result =
        _putenv(
            assignment.c_str());
#else
    const int result =
        setenv(
            name,
            value.c_str(),
            1);
#endif

    assert(result == 0);
  }

  static void prepare_test_environment()
  {
    set_env_var(
        "VIX_ENV_SILENT",
        "true");

    set_env_var(
        "VIX_INTERNAL_LOGS",
        "false");

    set_env_var(
        "VIX_ACCESS_LOGS",
        "false");

    set_env_var(
        "VIX_LOG_ASYNC",
        "false");

    set_env_var(
        "VIX_LOG_LEVEL",
        "critical");
  }

  static std::uint16_t test_port()
  {
    const char *value =
        std::getenv(
            "VIX_WEBSOCKET_TEST_PORT");

    if (value == nullptr ||
        *value == '\0')
    {
      return 19201u;
    }

    try
    {
      const unsigned long parsed =
          std::stoul(value);

      assert(parsed > 0u);
      assert(parsed <= 65535u);

      return static_cast<std::uint16_t>(
          parsed);
    }
    catch (...)
    {
      assert(false);
      return 19201u;
    }
  }

  static std::string trim(
      std::string value)
  {
    const auto isSpace =
        [](unsigned char character)
    {
      return std::isspace(
                 character) !=
             0;
    };

    while (!value.empty() &&
           isSpace(
               static_cast<unsigned char>(
                   value.front())))
    {
      value.erase(
          value.begin());
    }

    while (!value.empty() &&
           isSpace(
               static_cast<unsigned char>(
                   value.back())))
    {
      value.pop_back();
    }

    return value;
  }

  static std::string lower_copy(
      std::string value)
  {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character)
        {
          return static_cast<char>(
              std::tolower(character));
        });

    return value;
  }

  static void set_socket_timeouts(
      NativeSocket socket)
  {
#if defined(_WIN32)
    const DWORD timeout = 3000u;

    const int receiveResult =
        setsockopt(
            socket,
            SOL_SOCKET,
            SO_RCVTIMEO,
            reinterpret_cast<
                const char *>(&timeout),
            sizeof(timeout));

    const int sendResult =
        setsockopt(
            socket,
            SOL_SOCKET,
            SO_SNDTIMEO,
            reinterpret_cast<
                const char *>(&timeout),
            sizeof(timeout));
#else
    const timeval timeout{
        3,
        0};

    const int receiveResult =
        setsockopt(
            socket,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &timeout,
            sizeof(timeout));

    const int sendResult =
        setsockopt(
            socket,
            SOL_SOCKET,
            SO_SNDTIMEO,
            &timeout,
            sizeof(timeout));
#endif

    assert(receiveResult == 0);
    assert(sendResult == 0);
  }

  static SocketHandle connect_to_server(
      std::uint16_t port)
  {
    NativeSocket socket =
        ::socket(
            AF_INET,
            SOCK_STREAM,
            IPPROTO_TCP);

    if (socket == invalidSocket)
    {
      return {};
    }

    SocketHandle handle{
        socket};

    set_socket_timeouts(
        socket);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    const int conversionResult =
        inet_pton(
            AF_INET,
            "127.0.0.1",
            &address.sin_addr);

    assert(conversionResult == 1);

    const int connectionResult =
        ::connect(
            socket,
            reinterpret_cast<
                const sockaddr *>(&address),
            sizeof(address));

    if (connectionResult != 0)
    {
      return {};
    }

    return handle;
  }

  static void send_all(
      NativeSocket socket,
      std::string_view data)
  {
    std::size_t written = 0u;

    while (written < data.size())
    {
      const char *current =
          data.data() + written;

      const std::size_t remaining =
          data.size() - written;

#if defined(_WIN32)
      const int count =
          ::send(
              socket,
              current,
              static_cast<int>(
                  remaining),
              0);

      assert(count != SOCKET_ERROR);
#else
#ifdef MSG_NOSIGNAL
      constexpr int flags =
          MSG_NOSIGNAL;
#else
      constexpr int flags = 0;
#endif

      const ssize_t count =
          ::send(
              socket,
              current,
              remaining,
              flags);

      assert(count >= 0);
#endif

      assert(count > 0);

      written +=
          static_cast<std::size_t>(
              count);
    }
  }

  static void shutdown_writes(
      NativeSocket socket)
  {
#if defined(_WIN32)
    const int result =
        ::shutdown(
            socket,
            SD_SEND);
#else
    const int result =
        ::shutdown(
            socket,
            SHUT_WR);
#endif

    assert(result == 0);
  }

  static std::string read_all(
      NativeSocket socket)
  {
    std::string response;
    response.reserve(8192u);

    char buffer[4096]{};

    while (true)
    {
#if defined(_WIN32)
      const int count =
          ::recv(
              socket,
              buffer,
              static_cast<int>(
                  sizeof(buffer)),
              0);

      assert(count != SOCKET_ERROR);
#else
      const ssize_t count =
          ::recv(
              socket,
              buffer,
              sizeof(buffer),
              0);

      assert(count >= 0);
#endif

      if (count == 0)
      {
        break;
      }

      response.append(
          buffer,
          static_cast<std::size_t>(
              count));
    }

    return response;
  }

  static std::string send_request(
      std::uint16_t port,
      std::string_view request)
  {
    SocketHandle socket =
        connect_to_server(port);

    assert(socket.valid());

    send_all(
        socket.get(),
        request);

    shutdown_writes(
        socket.get());

    return read_all(
        socket.get());
  }

  static std::string send_request_chunks(
      std::uint16_t port,
      const std::vector<std::string> &chunks)
  {
    SocketHandle socket =
        connect_to_server(port);

    assert(socket.valid());

    for (const std::string &chunk :
         chunks)
    {
      send_all(
          socket.get(),
          chunk);

      std::this_thread::sleep_for(
          std::chrono::milliseconds{2});
    }

    shutdown_writes(
        socket.get());

    return read_all(
        socket.get());
  }

  static void wait_until_server_is_ready(
      std::uint16_t port)
  {
    constexpr std::size_t attempts = 500u;

    for (std::size_t attempt = 0u;
         attempt < attempts;
         ++attempt)
    {
      SocketHandle socket =
          connect_to_server(port);

      if (socket.valid())
      {
        return;
      }

      std::this_thread::sleep_for(
          std::chrono::milliseconds{10});
    }

    assert(false);
  }

  static HttpResponse parse_response(
      const std::string &wire)
  {
    const std::size_t headEnd =
        wire.find(
            "\r\n\r\n");

    assert(
        headEnd !=
        std::string::npos);

    const std::string head =
        wire.substr(
            0u,
            headEnd);

    HttpResponse response;
    response.body =
        wire.substr(
            headEnd + 4u);

    std::istringstream input{
        head};

    std::getline(
        input,
        response.statusLine);

    if (!response.statusLine.empty() &&
        response.statusLine.back() == '\r')
    {
      response.statusLine.pop_back();
    }

    {
      std::istringstream statusInput{
          response.statusLine};

      std::string version;

      statusInput >>
          version >>
          response.statusCode;

      assert(!version.empty());
      assert(response.statusCode > 0);
    }

    std::string line;

    while (std::getline(
        input,
        line))
    {
      if (!line.empty() &&
          line.back() == '\r')
      {
        line.pop_back();
      }

      const std::size_t separator =
          line.find(':');

      assert(
          separator !=
          std::string::npos);

      const std::string name =
          lower_copy(
              trim(
                  line.substr(
                      0u,
                      separator)));

      const std::string value =
          trim(
              line.substr(
                  separator + 1u));

      response.headers[name] =
          value;
    }

    return response;
  }

  static MetricsServerState *start_server(
      std::uint16_t port)
  {
    auto *state =
        new MetricsServerState{};

    state->metrics.connections_total.store(
        100u,
        std::memory_order_relaxed);

    state->metrics.connections_active.store(
        7u,
        std::memory_order_relaxed);

    state->metrics.messages_in_total.store(
        250u,
        std::memory_order_relaxed);

    state->metrics.messages_out_total.store(
        200u,
        std::memory_order_relaxed);

    state->metrics.errors_total.store(
        3u,
        std::memory_order_relaxed);

    state->metrics.lp_sessions_total.store(
        12u,
        std::memory_order_relaxed);

    state->metrics.lp_sessions_active.store(
        4u,
        std::memory_order_relaxed);

    state->metrics.lp_polls_total.store(
        80u,
        std::memory_order_relaxed);

    state->metrics.lp_messages_buffered.store(
        9u,
        std::memory_order_relaxed);

    state->metrics.lp_messages_enqueued_total.store(
        140u,
        std::memory_order_relaxed);

    state->metrics.lp_messages_drained_total.store(
        131u,
        std::memory_order_relaxed);

    std::thread(
        [state, port]()
        {
          vix::websocket::
              run_metrics_http_exporter(
                  state->metrics,
                  "127.0.0.1",
                  port);

          state->returned.store(
              true,
              std::memory_order_release);
        })
        .detach();

    wait_until_server_is_ready(
        port);

    return state;
  }

  static std::string header(
      const HttpResponse &response,
      const std::string &name)
  {
    const auto it =
        response.headers.find(
            lower_copy(name));

    assert(
        it !=
        response.headers.end());

    return it->second;
  }

  static void assert_common_headers(
      const HttpResponse &response)
  {
    assert(
        header(
            response,
            "Server") ==
        "vix-ws-metrics");

    assert(
        header(
            response,
            "Cache-Control") ==
        "no-store");

    assert(
        lower_copy(
            header(
                response,
                "Connection")) ==
        "close");

    const std::string contentLength =
        header(
            response,
            "Content-Length");

    assert(
        std::stoull(
            contentLength) ==
        response.body.size());
  }

  static void assert_not_found(
      std::uint16_t port,
      std::string_view request)
  {
    const HttpResponse response =
        parse_response(
            send_request(
                port,
                request));

    assert(response.statusCode == 404);
    assert(response.body == "Not Found\n");

    assert_common_headers(response);

    assert(
        header(
            response,
            "Content-Type") ==
        "text/plain; charset=utf-8");
  }

  static void test_get_metrics_returns_ok(
      const MetricsServerState &state,
      std::uint16_t port)
  {
    const HttpResponse response =
        parse_response(
            send_request(
                port,
                "GET /metrics HTTP/1.1\r\n"
                "Host: localhost\r\n"
                "\r\n"));

    assert(response.statusCode == 200);

    assert(
        response.statusLine.find(
            "HTTP/1.1 200") ==
        0u);

    assert_common_headers(response);

    assert(
        header(
            response,
            "Content-Type") ==
        "text/plain; version=0.0.4; charset=utf-8");

    assert(
        response.body ==
        state.metrics.render_prometheus());
  }

  static void test_lowercase_get_is_accepted(
      const MetricsServerState &state,
      std::uint16_t port)
  {
    const HttpResponse response =
        parse_response(
            send_request(
                port,
                "get /metrics HTTP/1.1\r\n"
                "Host: localhost\r\n"
                "\r\n"));

    assert(response.statusCode == 200);

    assert(
        response.body ==
        state.metrics.render_prometheus());
  }

  static void test_mixed_case_get_is_accepted(
      std::uint16_t port)
  {
    const HttpResponse response =
        parse_response(
            send_request(
                port,
                "GeT /metrics HTTP/1.1\r\n"
                "Host: localhost\r\n"
                "\r\n"));

    assert(response.statusCode == 200);
  }

  static void test_http_1_0_is_accepted(
      std::uint16_t port)
  {
    const HttpResponse response =
        parse_response(
            send_request(
                port,
                "GET /metrics HTTP/1.0\r\n"
                "\r\n"));

    assert(response.statusCode == 200);
  }

  static void test_nonstandard_nonempty_version_is_accepted(
      std::uint16_t port)
  {
    const HttpResponse response =
        parse_response(
            send_request(
                port,
                "GET /metrics CUSTOM/42\r\n"
                "\r\n"));

    assert(response.statusCode == 200);
  }

  static void test_additional_headers_are_ignored(
      std::uint16_t port)
  {
    const HttpResponse response =
        parse_response(
            send_request(
                port,
                "GET /metrics HTTP/1.1\r\n"
                "Host: localhost\r\n"
                "Accept: application/json\r\n"
                "X-Custom: value\r\n"
                "Connection: keep-alive\r\n"
                "\r\n"));

    assert(response.statusCode == 200);

    assert(
        lower_copy(
            header(
                response,
                "Connection")) ==
        "close");
  }

  static void test_request_can_arrive_in_chunks(
      const MetricsServerState &state,
      std::uint16_t port)
  {
    const std::vector<std::string> chunks{
        "GET /met",
        "rics HTTP/1.1\r\n",
        "Host: local",
        "host\r\n",
        "\r\n"};

    const HttpResponse response =
        parse_response(
            send_request_chunks(
                port,
                chunks));

    assert(response.statusCode == 200);

    assert(
        response.body ==
        state.metrics.render_prometheus());
  }

  static void test_response_contains_current_values(
      std::uint16_t port)
  {
    const HttpResponse response =
        parse_response(
            send_request(
                port,
                "GET /metrics HTTP/1.1\r\n"
                "\r\n"));

    assert(
        response.body.find(
            "vix_ws_connections_total 100\n") !=
        std::string::npos);

    assert(
        response.body.find(
            "vix_ws_connections_active 7\n") !=
        std::string::npos);

    assert(
        response.body.find(
            "vix_ws_messages_in_total 250\n") !=
        std::string::npos);

    assert(
        response.body.find(
            "vix_ws_messages_out_total 200\n") !=
        std::string::npos);

    assert(
        response.body.find(
            "vix_ws_errors_total 3\n") !=
        std::string::npos);

    assert(
        response.body.find(
            "vix_ws_lp_sessions_total 12\n") !=
        std::string::npos);

    assert(
        response.body.find(
            "vix_ws_lp_sessions_active 4\n") !=
        std::string::npos);

    assert(
        response.body.find(
            "vix_ws_lp_polls_total 80\n") !=
        std::string::npos);

    assert(
        response.body.find(
            "vix_ws_lp_messages_buffered 9\n") !=
        std::string::npos);

    assert(
        response.body.find(
            "vix_ws_lp_messages_enqueued_total 140\n") !=
        std::string::npos);

    assert(
        response.body.find(
            "vix_ws_lp_messages_drained_total 131\n") !=
        std::string::npos);
  }

  static void test_other_target_returns_not_found(
      std::uint16_t port)
  {
    assert_not_found(
        port,
        "GET /health HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n");
  }

  static void test_metrics_query_returns_not_found(
      std::uint16_t port)
  {
    assert_not_found(
        port,
        "GET /metrics?format=prometheus HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n");
  }

  static void test_metrics_trailing_slash_returns_not_found(
      std::uint16_t port)
  {
    assert_not_found(
        port,
        "GET /metrics/ HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n");
  }

  static void test_metrics_path_is_case_sensitive(
      std::uint16_t port)
  {
    assert_not_found(
        port,
        "GET /METRICS HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n");
  }

  static void test_post_metrics_returns_not_found(
      std::uint16_t port)
  {
    assert_not_found(
        port,
        "POST /metrics HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 0\r\n"
        "\r\n");
  }

  static void test_head_metrics_returns_not_found(
      std::uint16_t port)
  {
    assert_not_found(
        port,
        "HEAD /metrics HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n");
  }

  static void test_options_metrics_returns_not_found(
      std::uint16_t port)
  {
    assert_not_found(
        port,
        "OPTIONS /metrics HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n");
  }

  static void test_malformed_request_line_returns_not_found(
      std::uint16_t port)
  {
    assert_not_found(
        port,
        "invalid-request\r\n"
        "\r\n");
  }

  static void test_empty_request_line_returns_not_found(
      std::uint16_t port)
  {
    assert_not_found(
        port,
        "\r\n"
        "\r\n");
  }

  static void test_missing_version_returns_not_found(
      std::uint16_t port)
  {
    assert_not_found(
        port,
        "GET /metrics\r\n"
        "\r\n");
  }

  static void test_missing_target_returns_not_found(
      std::uint16_t port)
  {
    assert_not_found(
        port,
        "GET  HTTP/1.1\r\n"
        "\r\n");
  }

  static void test_leading_space_returns_not_found(
      std::uint16_t port)
  {
    assert_not_found(
        port,
        " GET /metrics HTTP/1.1\r\n"
        "\r\n");
  }

  static void test_absolute_target_returns_not_found(
      std::uint16_t port)
  {
    assert_not_found(
        port,
        "GET http://localhost/metrics HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n");
  }

  static void test_only_first_pipelined_request_is_processed(
      const MetricsServerState &state,
      std::uint16_t port)
  {
    const std::string wire =
        send_request(
            port,
            "GET /metrics HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "\r\n"
            "GET /unknown HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "\r\n");

    const HttpResponse response =
        parse_response(wire);

    assert(response.statusCode == 200);

    assert(
        response.body ==
        state.metrics.render_prometheus());

    const std::size_t firstStatus =
        wire.find(
            "HTTP/1.1 ");

    assert(
        firstStatus !=
        std::string::npos);

    assert(
        wire.find(
            "HTTP/1.1 ",
            firstStatus + 1u) ==
        std::string::npos);
  }

  static void test_server_remains_running_after_invalid_requests(
      const MetricsServerState &state,
      std::uint16_t port)
  {
    assert_not_found(
        port,
        "bad\r\n\r\n");

    const HttpResponse response =
        parse_response(
            send_request(
                port,
                "GET /metrics HTTP/1.1\r\n"
                "\r\n"));

    assert(response.statusCode == 200);

    assert(
        response.body ==
        state.metrics.render_prometheus());

    assert(
        state.returned.load(
            std::memory_order_acquire) ==
        false);
  }

} // namespace

int main()
{
  prepare_test_environment();

  /*
   * Intentionally leaked because the exporter has a blocking public API
   * without a stop function. The test process owns its lifetime.
   */
  auto *socketRuntime =
      new SocketRuntime{};

  (void)socketRuntime;

  const std::uint16_t port =
      test_port();

  MetricsServerState *state =
      start_server(port);

  assert(state != nullptr);

  test_get_metrics_returns_ok(
      *state,
      port);

  test_lowercase_get_is_accepted(
      *state,
      port);

  test_mixed_case_get_is_accepted(
      port);

  test_http_1_0_is_accepted(
      port);

  test_nonstandard_nonempty_version_is_accepted(
      port);

  test_additional_headers_are_ignored(
      port);

  test_request_can_arrive_in_chunks(
      *state,
      port);

  test_response_contains_current_values(
      port);

  test_other_target_returns_not_found(
      port);

  test_metrics_query_returns_not_found(
      port);

  test_metrics_trailing_slash_returns_not_found(
      port);

  test_metrics_path_is_case_sensitive(
      port);

  test_post_metrics_returns_not_found(
      port);

  test_head_metrics_returns_not_found(
      port);

  test_options_metrics_returns_not_found(
      port);

  test_malformed_request_line_returns_not_found(
      port);

  test_empty_request_line_returns_not_found(
      port);

  test_missing_version_returns_not_found(
      port);

  test_missing_target_returns_not_found(
      port);

  test_leading_space_returns_not_found(
      port);

  test_absolute_target_returns_not_found(
      port);

  test_only_first_pipelined_request_is_processed(
      *state,
      port);

  test_server_remains_running_after_invalid_requests(
      *state,
      port);

  return 0;
}
