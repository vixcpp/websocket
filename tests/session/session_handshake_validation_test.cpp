/**
 *
 * @file session_handshake_validation_test.cpp
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
        std::size_t maxReadChunk = 8192u)
        : input_{
              std::move(input)},
          maxReadChunk_{
              std::max<std::size_t>(
                  1u,
                  maxReadChunk)}
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
          buffer.empty() ||
          readOffset_ >= input_.size())
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

      output_.append(
          reinterpret_cast<
              const char *>(
              buffer.data()),
          buffer.size());

      writeCalls_ += 1u;

      co_return buffer.size();
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

    std::size_t readCalls_{0u};
    std::size_t writeCalls_{0u};
    std::size_t closeCalls_{0u};

    bool open_{true};
  };

  struct ValidationResult
  {
    std::string response{};

    std::size_t openCalls{0u};
    std::size_t closeCalls{0u};
    std::size_t errorCalls{0u};

    std::vector<std::string>
        errors{};

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

  static ValidationResult execute_request(
      std::string request,
      std::size_t maxReadChunk = 8192u)
  {
    Config config;

    auto context =
        std::make_shared<
            io_context>();

    auto router =
        std::make_shared<
            Router>();

    ValidationResult result;

    router->on_open(
        [&result](
            Session &)
        {
          result.openCalls += 1u;
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

    auto stream =
        std::make_unique<
            ScriptedTcpStream>(
            std::move(request),
            maxReadChunk);

    ScriptedTcpStream *streamPointer =
        stream.get();

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

  static void assert_rejected(
      const ValidationResult &result,
      const std::string &expectedError)
  {
    assert(result.openCalls == 0u);
    assert(result.closeCalls == 1u);
    assert(result.errorCalls == 1u);

    assert(result.errors.size() == 1u);

    assert(
        result.errors[0] ==
        expectedError);

    assert(result.response.empty());

    assert(!result.finalOpenState);
    assert(!result.streamOpen);

    assert(result.streamWriteCalls == 0u);
    assert(result.streamCloseCalls == 1u);
  }

  static void test_post_method_is_rejected()
  {
    const std::string request =
        "POST /chat HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const ValidationResult result =
        execute_request(request);

    assert_rejected(
        result,
        "websocket handshake must use GET");
  }

  static void test_put_method_is_rejected()
  {
    const std::string request =
        "PUT /chat HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const ValidationResult result =
        execute_request(request);

    assert_rejected(
        result,
        "websocket handshake must use GET");
  }

  static void test_get_prefix_without_separator_is_rejected()
  {
    const std::string request =
        "GETTING /chat HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const ValidationResult result =
        execute_request(request);

    assert_rejected(
        result,
        "websocket handshake must use GET");
  }

  static void test_leading_space_before_get_is_rejected()
  {
    const std::string request =
        " GET /chat HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const ValidationResult result =
        execute_request(request);

    assert_rejected(
        result,
        "websocket handshake must use GET");
  }

  static void test_missing_upgrade_header_is_rejected()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const ValidationResult result =
        execute_request(request);

    assert_rejected(
        result,
        "missing Upgrade: websocket");
  }

  static void test_empty_upgrade_header_is_rejected()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Upgrade:   \r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const ValidationResult result =
        execute_request(request);

    assert_rejected(
        result,
        "missing Upgrade: websocket");
  }

  static void test_wrong_upgrade_value_is_rejected()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Upgrade: h2c\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const ValidationResult result =
        execute_request(request);

    assert_rejected(
        result,
        "missing Upgrade: websocket");
  }

  static void test_upgrade_value_list_is_rejected()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Upgrade: websocket, h2c\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const ValidationResult result =
        execute_request(request);

    assert_rejected(
        result,
        "missing Upgrade: websocket");
  }

  static void test_similar_upgrade_header_name_is_ignored()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "X-Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const ValidationResult result =
        execute_request(request);

    assert_rejected(
        result,
        "missing Upgrade: websocket");
  }

  static void test_missing_connection_header_is_rejected()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const ValidationResult result =
        execute_request(request);

    assert_rejected(
        result,
        "missing Connection: Upgrade");
  }

  static void test_empty_connection_header_is_rejected()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection:   \r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const ValidationResult result =
        execute_request(request);

    assert_rejected(
        result,
        "missing Connection: Upgrade");
  }

  static void test_connection_without_upgrade_token_is_rejected()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: keep-alive, close\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const ValidationResult result =
        execute_request(request);

    assert_rejected(
        result,
        "missing Connection: Upgrade");
  }

  static void test_partial_connection_token_is_rejected()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrader\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const ValidationResult result =
        execute_request(request);

    assert_rejected(
        result,
        "missing Connection: Upgrade");
  }

  static void test_missing_websocket_key_is_rejected()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const ValidationResult result =
        execute_request(request);

    assert_rejected(
        result,
        "missing Sec-WebSocket-Key");
  }

  static void test_empty_websocket_key_is_rejected()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key:\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const ValidationResult result =
        execute_request(request);

    assert_rejected(
        result,
        "missing Sec-WebSocket-Key");
  }

  static void test_whitespace_websocket_key_is_rejected()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key:       \r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const ValidationResult result =
        execute_request(request);

    assert_rejected(
        result,
        "missing Sec-WebSocket-Key");
  }

  static void test_similar_key_header_name_is_ignored()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "X-Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const ValidationResult result =
        execute_request(request);

    assert_rejected(
        result,
        "missing Sec-WebSocket-Key");
  }

  static void test_version_12_is_rejected()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 12\r\n"
        "\r\n";

    const ValidationResult result =
        execute_request(request);

    assert_rejected(
        result,
        "unsupported Sec-WebSocket-Version");
  }

  static void test_version_14_is_rejected()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 14\r\n"
        "\r\n";

    const ValidationResult result =
        execute_request(request);

    assert_rejected(
        result,
        "unsupported Sec-WebSocket-Version");
  }

  static void test_version_list_is_rejected()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13, 8\r\n"
        "\r\n";

    const ValidationResult result =
        execute_request(request);

    assert_rejected(
        result,
        "unsupported Sec-WebSocket-Version");
  }

  static void test_version_is_case_sensitive_as_text()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: v13\r\n"
        "\r\n";

    const ValidationResult result =
        execute_request(request);

    assert_rejected(
        result,
        "unsupported Sec-WebSocket-Version");
  }

  static void test_oversized_http_head_is_rejected()
  {
    std::string request =
        "GET /chat HTTP/1.1\r\n"
        "X-Large: ";

    request.append(
        70u * 1024u,
        'x');

    const ValidationResult result =
        execute_request(
            std::move(request),
            8192u);

    assert_rejected(
        result,
        "websocket HTTP head too large");

    assert(result.streamReadCalls > 1u);
  }

  static void test_incomplete_http_head_does_not_open_session()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n";

    const ValidationResult result =
        execute_request(request);

    assert(result.openCalls == 0u);
    assert(result.closeCalls == 1u);

    assert(result.errorCalls == 0u);
    assert(result.errors.empty());

    assert(result.response.empty());

    assert(!result.finalOpenState);
    assert(!result.streamOpen);

    assert(result.streamWriteCalls == 0u);
    assert(result.streamCloseCalls == 1u);
  }

  static void test_empty_stream_does_not_open_session()
  {
    const ValidationResult result =
        execute_request("");

    assert(result.openCalls == 0u);
    assert(result.closeCalls == 1u);

    assert(result.errorCalls == 0u);
    assert(result.errors.empty());

    assert(result.response.empty());

    assert(!result.finalOpenState);
    assert(!result.streamOpen);

    assert(result.streamReadCalls == 0u);
    assert(result.streamWriteCalls == 0u);
    assert(result.streamCloseCalls == 1u);
  }

  static void test_invalid_request_with_partial_reads_is_rejected()
  {
    const std::string request =
        "POST /chat HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const ValidationResult result =
        execute_request(
            request,
            1u);

    assert_rejected(
        result,
        "websocket handshake must use GET");

    assert(result.streamReadCalls > 100u);
  }

  static void test_validation_stops_at_first_error()
  {
    const std::string request =
        "POST /chat HTTP/1.1\r\n"
        "Upgrade: invalid\r\n"
        "Connection: invalid\r\n"
        "Sec-WebSocket-Key:\r\n"
        "Sec-WebSocket-Version: 12\r\n"
        "\r\n";

    const ValidationResult result =
        execute_request(request);

    assert_rejected(
        result,
        "websocket handshake must use GET");
  }

} // namespace

int main()
{
  test_post_method_is_rejected();
  test_put_method_is_rejected();

  test_get_prefix_without_separator_is_rejected();
  test_leading_space_before_get_is_rejected();

  test_missing_upgrade_header_is_rejected();
  test_empty_upgrade_header_is_rejected();
  test_wrong_upgrade_value_is_rejected();
  test_upgrade_value_list_is_rejected();
  test_similar_upgrade_header_name_is_ignored();

  test_missing_connection_header_is_rejected();
  test_empty_connection_header_is_rejected();
  test_connection_without_upgrade_token_is_rejected();
  test_partial_connection_token_is_rejected();

  test_missing_websocket_key_is_rejected();
  test_empty_websocket_key_is_rejected();
  test_whitespace_websocket_key_is_rejected();
  test_similar_key_header_name_is_ignored();

  test_version_12_is_rejected();
  test_version_14_is_rejected();
  test_version_list_is_rejected();
  test_version_is_case_sensitive_as_text();

  test_oversized_http_head_is_rejected();

  test_incomplete_http_head_does_not_open_session();
  test_empty_stream_does_not_open_session();

  test_invalid_request_with_partial_reads_is_rejected();
  test_validation_stops_at_first_error();

  return 0;
}
