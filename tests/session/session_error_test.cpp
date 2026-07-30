/**
 *
 * @file session_error_test.cpp
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
#include <stdexcept>
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

  struct StreamOptions
  {
    std::size_t maxReadChunk{8192u};
    std::size_t maxWriteChunk{8192u};

    bool throwAfterInput{false};
    std::string readError{
        "transport read failed"};

    std::size_t failWriteAttempt{0u};
    bool throwOnWriteFailure{false};

    std::string writeError{
        "transport write failed"};

    bool initiallyOpen{true};
  };

  class TestTcpStream final
      : public tcp_stream
  {
  public:
    TestTcpStream(
        std::string input,
        StreamOptions options = {})
        : input_{
              std::move(input)},
          options_{
              std::move(options)},
          open_{
              options_.initiallyOpen}
    {
      options_.maxReadChunk =
          std::max<std::size_t>(
              1u,
              options_.maxReadChunk);

      options_.maxWriteChunk =
          std::max<std::size_t>(
              1u,
              options_.maxWriteChunk);
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
      readAttempts_ += 1u;

      if (!open_ ||
          buffer.empty())
      {
        co_return 0u;
      }

      if (readOffset_ <
          input_.size())
      {
        const std::size_t remaining =
            input_.size() -
            readOffset_;

        const std::size_t count =
            std::min(
                {
                    buffer.size(),
                    remaining,
                    options_.maxReadChunk,
                });

        std::memcpy(
            buffer.data(),
            input_.data() +
                readOffset_,
            count);

        readOffset_ += count;
        successfulReads_ += 1u;

        co_return count;
      }

      if (options_.throwAfterInput)
      {
        throw std::runtime_error{
            options_.readError};
      }

      co_return 0u;
    }

    vix::async::core::task<std::size_t>
    async_write(
        std::span<const std::byte> buffer,
        cancel_token = {}) override
    {
      writeAttempts_ += 1u;

      if (!open_ ||
          buffer.empty())
      {
        co_return 0u;
      }

      if (options_.failWriteAttempt != 0u &&
          writeAttempts_ ==
              options_.failWriteAttempt)
      {
        if (options_.throwOnWriteFailure)
        {
          throw std::runtime_error{
              options_.writeError};
        }

        co_return 0u;
      }

      const std::size_t count =
          std::min(
              buffer.size(),
              options_.maxWriteChunk);

      output_.append(
          reinterpret_cast<
              const char *>(
              buffer.data()),
          count);

      successfulWrites_ += 1u;

      co_return count;
    }

    void close() noexcept override
    {
      if (!open_)
      {
        return;
      }

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
    std::size_t read_attempts() const noexcept
    {
      return readAttempts_;
    }

    [[nodiscard]]
    std::size_t successful_reads() const noexcept
    {
      return successfulReads_;
    }

    [[nodiscard]]
    std::size_t write_attempts() const noexcept
    {
      return writeAttempts_;
    }

    [[nodiscard]]
    std::size_t successful_writes() const noexcept
    {
      return successfulWrites_;
    }

    [[nodiscard]]
    std::size_t close_calls() const noexcept
    {
      return closeCalls_;
    }

  private:
    std::string input_{};
    std::string output_{};

    StreamOptions options_{};

    std::size_t readOffset_{0u};

    std::size_t readAttempts_{0u};
    std::size_t successfulReads_{0u};

    std::size_t writeAttempts_{0u};
    std::size_t successfulWrites_{0u};

    std::size_t closeCalls_{0u};

    bool open_{true};
  };

  struct ErrorResult
  {
    std::string output{};

    std::vector<std::string> errors{};
    std::vector<bool> errorOpenStates{};

    std::size_t openCalls{0u};
    std::size_t closeCalls{0u};

    bool openCallbackState{false};
    bool closeCallbackState{true};

    bool finalOpenState{false};
    bool streamOpen{false};

    std::size_t streamReadAttempts{0u};
    std::size_t streamSuccessfulReads{0u};

    std::size_t streamWriteAttempts{0u};
    std::size_t streamSuccessfulWrites{0u};

    std::size_t streamCloseCalls{0u};
  };

  static std::string valid_handshake_request()
  {
    return "GET /errors HTTP/1.1\r\n"
           "Host: localhost\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Sec-WebSocket-Key: "
           "dGhlIHNhbXBsZSBub25jZQ==\r\n"
           "Sec-WebSocket-Version: 13\r\n"
           "\r\n";
  }

  static std::string valid_handshake_response()
  {
    return "HTTP/1.1 101 Switching Protocols\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Sec-WebSocket-Accept: "
           "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
           "Server: Vix.cpp\r\n"
           "\r\n";
  }

  static task_void run_session(
      const std::shared_ptr<Session> &session,
      io_context &context)
  {
    co_await session->run();

    context.stop();

    co_return;
  }

  static ErrorResult execute_session(
      std::string input,
      StreamOptions options = {},
      bool attachRouter = true)
  {
    auto context =
        std::make_shared<
            io_context>();

    std::shared_ptr<Router> router;

    ErrorResult result;

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

            result.openCallbackState =
                session.is_open();
          });

      router->on_close(
          [&result](
              Session &session)
          {
            result.closeCalls += 1u;

            result.closeCallbackState =
                session.is_open();
          });

      router->on_error(
          [&result](
              Session &session,
              const std::string &error)
          {
            result.errors.push_back(
                error);

            result.errorOpenStates.push_back(
                session.is_open());
          });
    }

    auto stream =
        std::make_unique<
            TestTcpStream>(
            std::move(input),
            std::move(options));

    TestTcpStream *streamPointer =
        stream.get();

    Config config;

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

    result.output =
        streamPointer->output();

    result.finalOpenState =
        session->is_open();

    result.streamOpen =
        streamPointer->is_open();

    result.streamReadAttempts =
        streamPointer->read_attempts();

    result.streamSuccessfulReads =
        streamPointer->successful_reads();

    result.streamWriteAttempts =
        streamPointer->write_attempts();

    result.streamSuccessfulWrites =
        streamPointer->successful_writes();

    result.streamCloseCalls =
        streamPointer->close_calls();

    return result;
  }

  static std::shared_ptr<Session>
  make_direct_session(
      const std::shared_ptr<Router> &router,
      const std::shared_ptr<io_context> &context)
  {
    Config config;

    return std::make_shared<
        Session>(
        nullptr,
        config,
        router,
        nullptr,
        context);
  }

  static void test_unknown_error_is_forwarded()
  {
    auto context =
        std::make_shared<
            io_context>();

    auto router =
        std::make_shared<
            Router>();

    std::vector<std::string> errors;

    router->on_error(
        [&errors](
            Session &,
            const std::string &error)
        {
          errors.push_back(error);
        });

    auto session =
        make_direct_session(
            router,
            context);

    session->emit_error(
        "application failure");

    assert(errors.size() == 1u);

    assert(
        errors[0] ==
        "application failure");
  }

  static void test_empty_error_is_forwarded()
  {
    auto context =
        std::make_shared<
            io_context>();

    auto router =
        std::make_shared<
            Router>();

    std::vector<std::string> errors;

    router->on_error(
        [&errors](
            Session &,
            const std::string &error)
        {
          errors.push_back(error);
        });

    auto session =
        make_direct_session(
            router,
            context);

    session->emit_error("");

    assert(errors.size() == 1u);
    assert(errors[0].empty());
  }

  static void test_protocol_errors_are_forwarded()
  {
    auto context =
        std::make_shared<
            io_context>();

    auto router =
        std::make_shared<
            Router>();

    std::vector<std::string> errors;

    router->on_error(
        [&errors](
            Session &,
            const std::string &error)
        {
          errors.push_back(error);
        });

    auto session =
        make_direct_session(
            router,
            context);

    const std::vector<std::string>
        expected{
            "websocket handshake must use GET",
            "missing Upgrade: websocket",
            "missing Connection: Upgrade",
            "unsupported Sec-WebSocket-Version",
            "invalid websocket frame",
            "bad websocket frame",
        };

    for (const std::string &error :
         expected)
    {
      session->emit_error(error);
    }

    assert(
        errors ==
        expected);
  }

  static void test_normal_disconnect_errors_are_suppressed()
  {
    auto context =
        std::make_shared<
            io_context>();

    auto router =
        std::make_shared<
            Router>();

    std::vector<std::string> errors;

    router->on_error(
        [&errors](
            Session &,
            const std::string &error)
        {
          errors.push_back(error);
        });

    auto session =
        make_direct_session(
            router,
            context);

    const std::vector<std::string>
        normalDisconnects{
            "end of file",
            "unexpected EOF",
            "connection reset",
            "connection reset by peer",
            "broken pipe",
            "operation canceled",
            "operation cancelled",
            "read canceled",
            "read cancelled",
            "write canceled",
            "write cancelled",
            "idle timeout",
            "operation timed out",
            "timeout",
        };

    for (const std::string &error :
         normalDisconnects)
    {
      session->emit_error(error);
    }

    assert(errors.empty());
  }

  static void test_disconnect_classification_is_case_insensitive()
  {
    auto context =
        std::make_shared<
            io_context>();

    auto router =
        std::make_shared<
            Router>();

    std::size_t errorCalls = 0u;

    router->on_error(
        [&errorCalls](
            Session &,
            const std::string &)
        {
          errorCalls += 1u;
        });

    auto session =
        make_direct_session(
            router,
            context);

    session->emit_error(
        "CONNECTION RESET BY PEER");

    session->emit_error(
        "BROKEN PIPE");

    session->emit_error(
        "UNEXPECTED EOF");

    session->emit_error(
        "IDLE TIMEOUT");

    assert(errorCalls == 0u);
  }

  static void test_only_non_normal_errors_are_forwarded()
  {
    auto context =
        std::make_shared<
            io_context>();

    auto router =
        std::make_shared<
            Router>();

    std::vector<std::string> errors;

    router->on_error(
        [&errors](
            Session &,
            const std::string &error)
        {
          errors.push_back(error);
        });

    auto session =
        make_direct_session(
            router,
            context);

    session->emit_error(
        "connection reset by peer");

    session->emit_error(
        "first application error");

    session->emit_error(
        "idle timeout");

    session->emit_error(
        "second application error");

    assert(errors.size() == 2u);

    assert(
        errors[0] ==
        "first application error");

    assert(
        errors[1] ==
        "second application error");
  }

  static void test_emit_error_without_router_is_safe()
  {
    auto context =
        std::make_shared<
            io_context>();

    auto session =
        make_direct_session(
            nullptr,
            context);

    session->emit_error(
        "application failure");

    session->emit_error(
        "connection reset by peer");

    assert(!session->is_open());
  }

  static void test_invalid_handshake_reports_error()
  {
    const std::string request =
        "POST /errors HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: "
        "dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const ErrorResult result =
        execute_session(request);

    assert(result.openCalls == 0u);
    assert(result.closeCalls == 1u);

    assert(result.errors.size() == 1u);

    assert(
        result.errors[0] ==
        "websocket handshake must use GET");

    assert(
        result.errorOpenStates.size() ==
        1u);

    assert(!result.errorOpenStates[0]);
    assert(!result.closeCallbackState);

    assert(result.output.empty());

    assert(!result.finalOpenState);
    assert(!result.streamOpen);

    assert(result.streamCloseCalls == 1u);
  }

  static void test_incomplete_handshake_eof_is_suppressed()
  {
    const std::string request =
        "GET /errors HTTP/1.1\r\n"
        "Upgrade: websocket\r\n";

    const ErrorResult result =
        execute_session(request);

    assert(result.openCalls == 0u);
    assert(result.closeCalls == 1u);

    assert(result.errors.empty());
    assert(result.errorOpenStates.empty());

    assert(result.output.empty());

    assert(!result.finalOpenState);
    assert(!result.streamOpen);

    assert(result.streamCloseCalls == 1u);
  }

  static void test_connection_reset_after_handshake_is_suppressed()
  {
    const ErrorResult result =
        execute_session(
            valid_handshake_request());

    assert(result.openCalls == 1u);
    assert(result.openCallbackState);

    assert(result.closeCalls == 1u);
    assert(!result.closeCallbackState);

    assert(result.errors.empty());
    assert(result.errorOpenStates.empty());

    assert(
        result.output ==
        valid_handshake_response());

    assert(!result.finalOpenState);
    assert(!result.streamOpen);

    assert(result.streamCloseCalls == 1u);
  }

  static void test_invalid_frame_header_reports_error()
  {
    std::string input =
        valid_handshake_request();

    input.push_back(
        static_cast<char>(0x81));

    input.push_back(
        static_cast<char>(0x7E));

    const ErrorResult result =
        execute_session(
            std::move(input));

    assert(result.openCalls == 1u);
    assert(result.closeCalls == 1u);

    assert(result.errors.empty());
    assert(result.errorOpenStates.empty());
    assert(!result.closeCallbackState);

    assert(
        result.output ==
        valid_handshake_response());

    assert(!result.finalOpenState);
    assert(!result.streamOpen);

    assert(result.streamCloseCalls == 1u);
  }

  static void test_transport_read_exception_is_forwarded()
  {
    StreamOptions options;

    options.throwAfterInput = true;
    options.readError =
        "transport exploded";

    const ErrorResult result =
        execute_session(
            valid_handshake_request(),
            options);

    assert(result.openCalls == 1u);
    assert(result.closeCalls == 1u);

    assert(result.errors.size() == 1u);

    assert(
        result.errors[0] ==
        "transport exploded");

    assert(
        result.errorOpenStates.size() ==
        1u);

    assert(result.errorOpenStates[0]);
    assert(!result.closeCallbackState);

    assert(
        result.output ==
        valid_handshake_response());

    assert(!result.finalOpenState);
    assert(!result.streamOpen);

    assert(result.streamCloseCalls == 1u);
  }

  static void test_handshake_zero_write_reports_error()
  {
    StreamOptions options;

    options.failWriteAttempt = 1u;
    options.throwOnWriteFailure = false;

    const ErrorResult result =
        execute_session(
            valid_handshake_request(),
            options);

    assert(result.openCalls == 0u);
    assert(result.closeCalls == 1u);

    assert(result.errors.size() == 1u);

    assert(
        result.errors[0] ==
        "websocket handshake write failed");

    assert(
        result.errorOpenStates.size() ==
        1u);

    assert(!result.errorOpenStates[0]);

    assert(result.output.empty());

    assert(!result.finalOpenState);
    assert(!result.streamOpen);

    assert(result.streamWriteAttempts == 1u);
    assert(result.streamSuccessfulWrites == 0u);
    assert(result.streamCloseCalls == 1u);
  }

  static void test_handshake_write_exception_is_forwarded()
  {
    StreamOptions options;

    options.failWriteAttempt = 1u;
    options.throwOnWriteFailure = true;

    options.writeError =
        "handshake transport exploded";

    const ErrorResult result =
        execute_session(
            valid_handshake_request(),
            options);

    assert(result.openCalls == 0u);
    assert(result.closeCalls == 1u);

    assert(result.errors.size() == 1u);

    assert(
        result.errors[0] ==
        "handshake transport exploded");

    assert(result.output.empty());

    assert(!result.finalOpenState);
    assert(!result.streamOpen);

    assert(result.streamCloseCalls == 1u);
  }

  static void test_error_path_without_router_is_safe()
  {
    const std::string request =
        "POST /errors HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: "
        "dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "\r\n";

    const ErrorResult result =
        execute_session(
            request,
            {},
            false);

    assert(result.openCalls == 0u);
    assert(result.closeCalls == 0u);

    assert(result.errors.empty());
    assert(result.errorOpenStates.empty());

    assert(result.output.empty());

    assert(!result.finalOpenState);
    assert(!result.streamOpen);

    assert(result.streamCloseCalls == 1u);
  }

  static void test_closed_transport_during_handshake_is_safe()
  {
    StreamOptions options;
    options.initiallyOpen = false;

    const ErrorResult result =
        execute_session(
            valid_handshake_request(),
            options);

    assert(result.openCalls == 0u);
    assert(result.closeCalls == 1u);

    assert(result.errors.empty());

    assert(result.output.empty());

    assert(!result.finalOpenState);
    assert(!result.streamOpen);

    assert(result.streamCloseCalls == 0u);
  }

} // namespace

int main()
{
  test_unknown_error_is_forwarded();
  test_empty_error_is_forwarded();

  test_protocol_errors_are_forwarded();

  test_normal_disconnect_errors_are_suppressed();
  test_disconnect_classification_is_case_insensitive();

  test_only_non_normal_errors_are_forwarded();
  test_emit_error_without_router_is_safe();

  test_invalid_handshake_reports_error();
  test_incomplete_handshake_eof_is_suppressed();

  test_connection_reset_after_handshake_is_suppressed();
  test_invalid_frame_header_reports_error();

  test_transport_read_exception_is_forwarded();

  test_handshake_zero_write_reports_error();
  test_handshake_write_exception_is_forwarded();

  test_error_path_without_router_is_safe();
  test_closed_transport_during_handshake_is_safe();

  return 0;
}
