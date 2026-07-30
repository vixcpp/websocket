/**
 *
 * @file metrics_server_lifecycle_test.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira. All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license that can be found in the License file.
 *
 * Vix.cpp
 *
 */

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
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
      return 19200u;
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
      return 19200u;
    }
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

  static std::string read_all(
      NativeSocket socket)
  {
    std::string response;
    response.reserve(4096u);

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

  static MetricsServerState *start_server(
      std::uint16_t port)
  {
    auto *state =
        new MetricsServerState{};

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

  static void test_exporter_function_signature()
  {
    using Exporter =
        void (*)(
            WebSocketMetrics &,
            const std::string &,
            std::uint16_t);

    static_assert(
        std::is_same_v<
            decltype(&vix::websocket::
                         run_metrics_http_exporter),
            Exporter>);
  }

  static void test_server_stays_running_after_start(
      MetricsServerState &state)
  {
    assert(
        state.returned.load(
            std::memory_order_acquire) ==
        false);
  }

  static void test_server_accepts_request(
      MetricsServerState &state,
      std::uint16_t port)
  {
    (void)state;

    const std::string response =
        send_request(
            port,
            "GET /metrics HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "\r\n");

    assert(
        response.find(
            "HTTP/1.1 200") ==
        0u);

    assert(
        response.find(
            "vix_ws_connections_total 0\n") !=
        std::string::npos);
  }

  static void test_server_survives_aborted_connection(
      MetricsServerState &state,
      std::uint16_t port)
  {
    {
      SocketHandle socket =
          connect_to_server(port);

      assert(socket.valid());
    }

    const std::string response =
        send_request(
            port,
            "GET /metrics HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "\r\n");

    assert(
        response.find(
            "HTTP/1.1 200") ==
        0u);

    assert(
        state.returned.load(
            std::memory_order_acquire) ==
        false);
  }

  static void test_server_accepts_repeated_requests(
      MetricsServerState &state,
      std::uint16_t port)
  {
    constexpr std::size_t count = 25u;

    for (std::size_t index = 0u;
         index < count;
         ++index)
    {
      const std::string response =
          send_request(
              port,
              "GET /metrics HTTP/1.1\r\n"
              "Host: localhost\r\n"
              "Connection: close\r\n"
              "\r\n");

      assert(
          response.find(
              "HTTP/1.1 200") ==
          0u);

      assert(
          response.find(
              "vix_ws_errors_total 0\n") !=
          std::string::npos);
    }

    assert(
        state.returned.load(
            std::memory_order_acquire) ==
        false);
  }

  static void test_server_accepts_concurrent_requests(
      MetricsServerState &state,
      std::uint16_t port)
  {
    constexpr std::size_t workerCount = 8u;

    std::atomic<std::size_t> successes{
        0u};

    std::vector<std::thread> workers;
    workers.reserve(workerCount);

    for (std::size_t index = 0u;
         index < workerCount;
         ++index)
    {
      workers.emplace_back(
          [&successes, port]()
          {
            const std::string response =
                send_request(
                    port,
                    "GET /metrics HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "\r\n");

            if (response.find(
                    "HTTP/1.1 200") ==
                    0u &&
                response.find(
                    "vix_ws_connections_active 0\n") !=
                    std::string::npos)
            {
              successes.fetch_add(
                  1u,
                  std::memory_order_relaxed);
            }
          });
    }

    for (std::thread &worker :
         workers)
    {
      worker.join();
    }

    assert(
        successes.load(
            std::memory_order_relaxed) ==
        workerCount);

    assert(
        state.returned.load(
            std::memory_order_acquire) ==
        false);
  }

  static void test_server_reflects_live_counter_changes(
      MetricsServerState &state,
      std::uint16_t port)
  {
    state.metrics.connections_total.store(
        42u,
        std::memory_order_relaxed);

    state.metrics.connections_active.store(
        3u,
        std::memory_order_relaxed);

    state.metrics.messages_in_total.store(
        100u,
        std::memory_order_relaxed);

    const std::string response =
        send_request(
            port,
            "GET /metrics HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "\r\n");

    assert(
        response.find(
            "vix_ws_connections_total 42\n") !=
        std::string::npos);

    assert(
        response.find(
            "vix_ws_connections_active 3\n") !=
        std::string::npos);

    assert(
        response.find(
            "vix_ws_messages_in_total 100\n") !=
        std::string::npos);
  }

  static void test_metrics_requests_do_not_modify_counters(
      MetricsServerState &state,
      std::uint16_t port)
  {
    const auto connectionsTotal =
        state.metrics.connections_total.load(
            std::memory_order_relaxed);

    const auto connectionsActive =
        state.metrics.connections_active.load(
            std::memory_order_relaxed);

    const auto messagesIn =
        state.metrics.messages_in_total.load(
            std::memory_order_relaxed);

    for (std::size_t index = 0u;
         index < 10u;
         ++index)
    {
      const std::string response =
          send_request(
              port,
              "GET /metrics HTTP/1.1\r\n"
              "Host: localhost\r\n"
              "\r\n");

      assert(
          response.find(
              "HTTP/1.1 200") ==
          0u);
    }

    assert(
        state.metrics.connections_total.load(
            std::memory_order_relaxed) ==
        connectionsTotal);

    assert(
        state.metrics.connections_active.load(
            std::memory_order_relaxed) ==
        connectionsActive);

    assert(
        state.metrics.messages_in_total.load(
            std::memory_order_relaxed) ==
        messagesIn);
  }

  static void test_server_remains_available_after_not_found(
      MetricsServerState &state,
      std::uint16_t port)
  {
    const std::string notFound =
        send_request(
            port,
            "GET /unknown HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "\r\n");

    assert(
        notFound.find(
            "HTTP/1.1 404") ==
        0u);

    const std::string metrics =
        send_request(
            port,
            "GET /metrics HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "\r\n");

    assert(
        metrics.find(
            "HTTP/1.1 200") ==
        0u);

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

  test_exporter_function_signature();

  const std::uint16_t port =
      test_port();

  MetricsServerState *state =
      start_server(port);

  assert(state != nullptr);

  test_server_stays_running_after_start(
      *state);

  test_server_accepts_request(
      *state,
      port);

  test_server_survives_aborted_connection(
      *state,
      port);

  test_server_accepts_repeated_requests(
      *state,
      port);

  test_server_accepts_concurrent_requests(
      *state,
      port);

  test_server_reflects_live_counter_changes(
      *state,
      port);

  test_metrics_requests_do_not_modify_counters(
      *state,
      port);

  test_server_remains_available_after_not_found(
      *state,
      port);

  return 0;
}
