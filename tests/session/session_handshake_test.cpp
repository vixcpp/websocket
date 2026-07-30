/**
 *
 * @file session_handshake_test.cpp
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
#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <vix/async/core/task.hpp>
#include <vix/websocket/session.hpp>

namespace
{
  using Config =
      vix::websocket::Config;

  using Router =
      vix::websocket::Router;

  using Session =
      vix::websocket::Session;

  using cancel_token =
      vix::async::core::cancel_token;

  using io_context =
      vix::async::core::io_context;

  using task_void =
      vix::async::core::task<void>;

  using tcp_endpoint =
      vix::async::net::tcp_endpoint;

  using tcp_stream =
      vix::async::net::tcp_stream;

  class ScriptedTcpStream final
      : public tcp_stream
  {
  public:
    explicit ScriptedTcpStream(
        std::string input,
        std::size_t maxReadChunk = 8192u,
        std::size_t maxWriteChunk = 8192u)
        : input_{
              std::move(input)},
          maxReadChunk_{
              std::max<std::size_t>(
                  1u,
                  maxReadChunk)},
          maxWriteChunk_{
              std::max<std::size_t>(
                  1u,
                  maxWriteChunk)}
    {
    }

    task_void async_connect(
        const tcp_endpoint &,
        cancel_token = {}) override
    {
      open_ = true;
      co_return;
    }

    vix::async::core::task<std::size_t>
    async_read(
        std::span<std::byte> buffer,
        cancel_token = {}) override
    {
      if (!open_ ||
          readOffset_ >= input_.size() ||
          buffer.empty())
      {
        co_return 0u;
      }

      const std::size_t remaining =
          input_.size() -
          readOffset_;

      const std::size_t count =
          std::min(
              {
                  buffer.size(),
                  remaining,
                  maxReadChunk_,
              });

      std::memcpy(
          buffer.data(),
          input_.data() +
              readOffset_,
          count);

      readOffset_ += count;
      readCalls_ += 1u;

      co_return count;
    }

    vix::async::core::task<std::size_t>
    async_write(
        std::span<const std::byte> buffer,
        cancel_token = {}) override
    {
      if (!open_ ||
          buffer.empty())
      {
        co_return 0u;
      }

      const std::size_t count =
          std::min(
              buffer.size(),
              maxWriteChunk_);

      output_.append(
          reinterpret_cast<
              const char *>(
              buffer.data()),
          count);

      writeCalls_ += 1u;

      co_return count;
    }

    void close() noexcept override
    {
      open_ = false;
      closeCalls_ += 1u;
    }

    [[nodiscard]]
    bool is_open() const noexcept override
    {
      return open_;
    }

    [[nodiscard]]
    const std::string &
    output() const noexcept
    {
      return output_;
    }

    [[nodiscard]]
    std::size_t read_calls() const noexcept
    {
      return readCalls_;
    }

    [[nodiscard]]
    std::size_t write_calls() const noexcept
    {
      return writeCalls_;
    }

    [[nodiscard]]
    std::size_t close_calls() const noexcept
    {
      return closeCalls_;
    }

  private:
    std::string input_{};
    std::string output_{};

    std::size_t readOffset_{0u};

    std::size_t maxReadChunk_{8192u};
    std::size_t maxWriteChunk_{8192u};

    std::size_t readCalls_{0u};
    std::size_t writeCalls_{0u};
    std::size_t closeCalls_{0u};

    bool open_{true};
  };

  struct HandshakeResult
  {
    std::string response{};

    std::size_t openCalls{0u};
    std::size_t closeCalls{0u};
    std::size_t errorCalls{0u};

    std::vector<std::string>
        errors{};

    bool openInsideCallback{false};
    bool finalOpenState{false};
    bool streamOpen{false};

    std::size_t streamReadCalls{0u};
    std::size_t streamWriteCalls{0u};
    std::size_t streamCloseCalls{0u};
  };

  static task_void run_session(
      const std::shared_ptr<Session> &session,
      io_context &context)
  {
    co_await session->run();

    context.stop();

    co_return;
  }

  static HandshakeResult execute_handshake(
      std::string request,
      std::size_t maxReadChunk = 8192u,
      std::size_t maxWriteChunk = 8192u,
      bool attachRouter = true)
  {
    Config config;

    auto context =
        std::make_shared<
            io_context>();

    auto stream =
        std::make_unique<
            ScriptedTcpStream>(
            std::move(request),
            maxReadChunk,
            maxWriteChunk);

    ScriptedTcpStream *streamPointer =
        stream.get();

    HandshakeResult result;

    std::shared_ptr<Router> router;

    if (attachRouter)
    {
      router =
          std::make_shared<
              Router>();

      router->on_open(
          [&result](
              Session &session)
          {
            result.openCalls += 1u;

            result.openInsideCallback =
                session.is_open();
          });

      router->on_close(
          [&result](
              Session &)
          {
            result.closeCalls += 1u;
          });

      router->on_error(
          [&result](
              Session &,
              const std::string &error)
          {
            result.errorCalls += 1u;

            result.errors.push_back(
                error);
          });
    }

    auto session =
        std::make_shared<
            Session>(
            std::move(stream),
            config,
            router,
            nullptr,
            context);

    auto task =
        run_session(
            session,
            *context);

    std::move(task).start(
        context->get_scheduler());

    context->run();

    result.response =
        streamPointer->output();

    result.finalOpenState =
        session->is_open();

    result.streamOpen =
        streamPointer->is_open();

    result.streamReadCalls =
        streamPointer->read_calls();

    result.streamWriteCalls =
        streamPointer->write_calls();

    result.streamCloseCalls =
        streamPointer->close_calls();

    return result;
  }

  static std::string valid_request(
      std::string key =
          "dGhlIHNhbXBsZSBub25jZQ==")
  {
    return "GET /chat HTTP/1.1\r\n"
           "Host: server.example.com\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Sec-WebSocket-Key: " +
           std::move(key) +
           "\r\n"
           "Sec-WebSocket-Version: 13\r\n"
           "\r\n";
  }

  static std::string expected_response(
      std::string acceptKey =
          "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=")
  {
    return "HTTP/1.1 101 Switching Protocols\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Sec-WebSocket-Accept: " +
           std::move(acceptKey) +
           "\r\n"
           "Server: Vix.cpp\r\n"
           "\r\n";
  }

  static void assert_successful_handshake(
      const HandshakeResult &result)
  {
    assert(result.openCalls == 1u);
    assert(result.errorCalls == 0u);
    assert(result.closeCalls == 1u);

    assert(result.errors.empty());

    assert(result.openInsideCallback);
    assert(!result.finalOpenState);

    assert(!result.streamOpen);
    assert(result.streamCloseCalls == 1u);

    assert(!result.response.empty());
  }

  static void test_rfc_handshake_response()
  {
    const HandshakeResult result =
        execute_handshake(
            valid_request());

    assert_successful_handshake(
        result);

    assert(
        result.response ==
        expected_response());
  }

  static void test_accept_key_is_computed_from_client_key()
  {
    const HandshakeResult result =
        execute_handshake(
            valid_request(
                "x3JJHMbDL1EzLkh9GBhXDw=="));

    assert_successful_handshake(
        result);

    assert(
        result.response ==
        expected_response(
            "HSmrc0sMlYUkAGmm5OPpG2HaGWk="));
  }

  static void test_handshake_supports_partial_reads()
  {
    const HandshakeResult result =
        execute_handshake(
            valid_request(),
            3u,
            8192u);

    assert_successful_handshake(
        result);

    assert(
        result.response ==
        expected_response());

    assert(result.streamReadCalls > 1u);
  }

  static void test_handshake_supports_partial_writes()
  {
    const HandshakeResult result =
        execute_handshake(
            valid_request(),
            8192u,
            5u);

    assert_successful_handshake(
        result);

    assert(
        result.response ==
        expected_response());

    assert(result.streamWriteCalls > 1u);
  }

  static void test_handshake_supports_partial_reads_and_writes()
  {
    const HandshakeResult result =
        execute_handshake(
            valid_request(),
            1u,
            1u);

    assert_successful_handshake(
        result);

    assert(
        result.response ==
        expected_response());

    assert(result.streamReadCalls > 100u);
    assert(result.streamWriteCalls > 100u);
  }

  static void test_method_is_case_insensitive()
  {
    const std::string request =
        "get /chat HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const HandshakeResult result =
        execute_handshake(request);

    assert_successful_handshake(
        result);

    assert(
        result.response ==
        expected_response());
  }

  static void test_header_names_are_case_insensitive()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "host: localhost\r\n"
        "uPgRaDe: websocket\r\n"
        "cOnNeCtIoN: Upgrade\r\n"
        "sEc-WeBsOcKeT-kEy: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "sEc-WeBsOcKeT-vErSiOn: 13\r\n"
        "\r\n";

    const HandshakeResult result =
        execute_handshake(request);

    assert_successful_handshake(
        result);

    assert(
        result.response ==
        expected_response());
  }

  static void test_upgrade_value_is_case_insensitive()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: WebSocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const HandshakeResult result =
        execute_handshake(request);

    assert_successful_handshake(
        result);

    assert(
        result.response ==
        expected_response());
  }

  static void test_connection_token_is_case_insensitive()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: uPgRaDe\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const HandshakeResult result =
        execute_handshake(request);

    assert_successful_handshake(
        result);
  }

  static void test_connection_header_accepts_token_list()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: keep-alive, Upgrade, close\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const HandshakeResult result =
        execute_handshake(request);

    assert_successful_handshake(
        result);
  }

  static void test_connection_tokens_are_trimmed()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: keep-alive,   Upgrade   , close\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const HandshakeResult result =
        execute_handshake(request);

    assert_successful_handshake(
        result);
  }

  static void test_websocket_key_is_trimmed()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key:    dGhlIHNhbXBsZSBub25jZQ==    \r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const HandshakeResult result =
        execute_handshake(request);

    assert_successful_handshake(
        result);

    assert(
        result.response ==
        expected_response());
  }

  static void test_version_is_trimmed()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version:    13    \r\n"
        "\r\n";

    const HandshakeResult result =
        execute_handshake(request);

    assert_successful_handshake(
        result);
  }

  static void test_version_header_is_optional()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "\r\n";

    const HandshakeResult result =
        execute_handshake(request);

    assert_successful_handshake(
        result);

    assert(
        result.response ==
        expected_response());
  }

  static void test_empty_version_header_is_accepted()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version:   \r\n"
        "\r\n";

    const HandshakeResult result =
        execute_handshake(request);

    assert_successful_handshake(
        result);
  }

  static void test_custom_target_is_accepted()
  {
    const std::string request =
        "GET /api/realtime?token=abc HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const HandshakeResult result =
        execute_handshake(request);

    assert_successful_handshake(
        result);
  }

  static void test_extra_headers_are_ignored()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Origin: https://example.com\r\n"
        "User-Agent: Vix-Test\r\n"
        "X-Custom-Header: value\r\n"
        "Upgrade: websocket\r\n"
        "Connection: keep-alive, Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const HandshakeResult result =
        execute_handshake(request);

    assert_successful_handshake(
        result);
  }

  static void test_handshake_without_router()
  {
    const HandshakeResult result =
        execute_handshake(
            valid_request(),
            8192u,
            8192u,
            false);

    assert(result.openCalls == 0u);
    assert(result.closeCalls == 0u);
    assert(result.errorCalls == 0u);

    assert(result.errors.empty());

    assert(
        result.response ==
        expected_response());

    assert(!result.finalOpenState);
    assert(!result.streamOpen);
    assert(result.streamCloseCalls == 1u);
  }

  static void test_response_contains_required_headers()
  {
    const HandshakeResult result =
        execute_handshake(
            valid_request());

    assert_successful_handshake(
        result);

    assert(
        result.response.find(
            "HTTP/1.1 101 Switching Protocols\r\n") ==
        0u);

    assert(
        result.response.find(
            "Upgrade: websocket\r\n") !=
        std::string::npos);

    assert(
        result.response.find(
            "Connection: Upgrade\r\n") !=
        std::string::npos);

    assert(
        result.response.find(
            "Sec-WebSocket-Accept: "
            "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n") !=
        std::string::npos);

    assert(
        result.response.find(
            "Server: Vix.cpp\r\n") !=
        std::string::npos);

    assert(
        result.response.ends_with(
            "\r\n\r\n"));
  }

} // namespace

int main()
{
  test_rfc_handshake_response();
  test_accept_key_is_computed_from_client_key();

  test_handshake_supports_partial_reads();
  test_handshake_supports_partial_writes();
  test_handshake_supports_partial_reads_and_writes();

  test_method_is_case_insensitive();
  test_header_names_are_case_insensitive();

  test_upgrade_value_is_case_insensitive();
  test_connection_token_is_case_insensitive();

  test_connection_header_accepts_token_list();
  test_connection_tokens_are_trimmed();

  test_websocket_key_is_trimmed();
  test_version_is_trimmed();

  test_version_header_is_optional();
  test_empty_version_header_is_accepted();

  test_custom_target_is_accepted();
  test_extra_headers_are_ignored();

  test_handshake_without_router();
  test_response_contains_required_headers();

  return 0;
}
