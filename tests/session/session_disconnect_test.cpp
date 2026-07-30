/**
 *
 * @file session_disconnect_test.cpp
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
#include <system_error>
#include <utility>
#include <vector>

#include <vix/async/core/task.hpp>
#include <vix/websocket/protocol.hpp>
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

  enum class ExhaustionBehavior
  {
    ReturnZero,
    ThrowRuntimeError,
    ThrowConnectionReset
  };

  struct StreamOptions
  {
    ExhaustionBehavior exhaustion{
        ExhaustionBehavior::
            ReturnZero};

    std::string runtimeError{
        "transport read failed"};

    std::size_t maxReadChunk{
        8192u};

    std::size_t maxWriteChunk{
        8192u};

    bool closeAfterFirstWrite{
        false};

    bool initiallyOpen{
        true};
  };

  class DisconnectTcpStream final
      : public tcp_stream
  {
  public:
    DisconnectTcpStream(
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
      readCalls_ += 1u;

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

        co_return count;
      }

      switch (options_.exhaustion)
      {
      case ExhaustionBehavior::ReturnZero:
        co_return 0u;

      case ExhaustionBehavior::
          ThrowRuntimeError:
        throw std::runtime_error{
            options_.runtimeError};

      case ExhaustionBehavior::
          ThrowConnectionReset:
        throw std::system_error{
            std::make_error_code(
                std::errc::
                    connection_reset)};
      }

      co_return 0u;
    }

    vix::async::core::task<std::size_t>
    async_write(
        std::span<const std::byte> buffer,
        cancel_token = {}) override
    {
      writeCalls_ += 1u;

      if (!open_ ||
          buffer.empty())
      {
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

      if (options_.closeAfterFirstWrite &&
          writeCalls_ == 1u)
      {
        open_ = false;
      }

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

    StreamOptions options_{};

    std::size_t readOffset_{0u};

    std::size_t readCalls_{0u};
    std::size_t writeCalls_{0u};
    std::size_t closeCalls_{0u};

    bool open_{true};
  };

  struct DisconnectResult
  {
    std::string output{};

    std::vector<std::string>
        messages{};

    std::vector<std::string>
        errors{};

    std::vector<bool>
        messageOpenStates{};

    std::vector<bool>
        errorOpenStates{};

    std::size_t openCalls{0u};
    std::size_t closeCalls{0u};

    bool openCallbackState{false};
    bool closeCallbackState{true};

    bool finalOpenState{false};
    bool streamOpen{false};

    std::size_t streamReadCalls{0u};
    std::size_t streamWriteCalls{0u};
    std::size_t streamCloseCalls{0u};
  };

  static std::string handshake_request()
  {
    return "GET /disconnect HTTP/1.1\r\n"
           "Host: localhost\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Sec-WebSocket-Key: "
           "dGhlIHNhbXBsZSBub25jZQ==\r\n"
           "Sec-WebSocket-Version: 13\r\n"
           "\r\n";
  }

  static std::string handshake_response()
  {
    return "HTTP/1.1 101 Switching Protocols\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Sec-WebSocket-Accept: "
           "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
           "Server: Vix.cpp\r\n"
           "\r\n";
  }

  static void append_frame(
      std::string &wire,
      const std::vector<std::byte> &frame)
  {
    if (frame.empty())
    {
      return;
    }

    wire.append(
        reinterpret_cast<
            const char *>(
            frame.data()),
        frame.size());
  }

  static task_void run_session(
      const std::shared_ptr<Session> &session,
      io_context &context)
  {
    co_await session->run();

    context.stop();

    co_return;
  }

  static DisconnectResult execute_session(
      std::string input,
      StreamOptions options = {},
      bool attachRouter = true)
  {
    auto context =
        std::make_shared<
            io_context>();

    std::shared_ptr<Router> router;

    DisconnectResult result;

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

      router->on_message(
          [&result](
              Session &session,
              const std::string &message)
          {
            result.messages.push_back(
                message);

            result.messageOpenStates.push_back(
                session.is_open());
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

      router->on_close(
          [&result](
              Session &session)
          {
            result.closeCalls += 1u;

            result.closeCallbackState =
                session.is_open();
          });
    }

    auto stream =
        std::make_unique<
            DisconnectTcpStream>(
            std::move(input),
            std::move(options));

    DisconnectTcpStream *streamPointer =
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

    result.streamReadCalls =
        streamPointer->read_calls();

    result.streamWriteCalls =
        streamPointer->write_calls();

    result.streamCloseCalls =
        streamPointer->close_calls();

    return result;
  }

  static void assert_normal_disconnect(
      const DisconnectResult &result)
  {
    assert(result.openCalls == 1u);
    assert(result.closeCalls == 1u);

    assert(result.openCallbackState);
    assert(!result.closeCallbackState);

    assert(result.errors.empty());
    assert(result.errorOpenStates.empty());

    assert(!result.finalOpenState);
    assert(!result.streamOpen);

    assert(
        result.output ==
        handshake_response());
  }

  static void assert_messages_received_while_open(
      const DisconnectResult &result)
  {
    assert(
        result.messageOpenStates.size() ==
        result.messages.size());

    for (const bool open :
         result.messageOpenStates)
    {
      assert(open);
    }
  }

  static void test_eof_after_handshake_is_normal_disconnect()
  {
    const DisconnectResult result =
        execute_session(
            handshake_request());

    assert_normal_disconnect(result);

    assert(result.messages.empty());

    assert(
        result.streamCloseCalls ==
        1u);
  }

  static void test_connection_reset_is_normal_disconnect()
  {
    StreamOptions options;

    options.exhaustion =
        ExhaustionBehavior::
            ThrowConnectionReset;

    const DisconnectResult result =
        execute_session(
            handshake_request(),
            options);

    assert_normal_disconnect(result);

    assert(result.messages.empty());

    assert(
        result.streamCloseCalls ==
        1u);
  }

  static void test_broken_pipe_is_normal_disconnect()
  {
    StreamOptions options;

    options.exhaustion =
        ExhaustionBehavior::
            ThrowRuntimeError;

    options.runtimeError =
        "broken pipe";

    const DisconnectResult result =
        execute_session(
            handshake_request(),
            options);

    assert_normal_disconnect(result);

    assert(result.messages.empty());
  }

  static void test_operation_cancelled_is_normal_disconnect()
  {
    StreamOptions options;

    options.exhaustion =
        ExhaustionBehavior::
            ThrowRuntimeError;

    options.runtimeError =
        "operation cancelled";

    const DisconnectResult result =
        execute_session(
            handshake_request(),
            options);

    assert_normal_disconnect(result);

    assert(result.messages.empty());
  }

  static void test_read_cancelled_is_normal_disconnect()
  {
    StreamOptions options;

    options.exhaustion =
        ExhaustionBehavior::
            ThrowRuntimeError;

    options.runtimeError =
        "read cancelled";

    const DisconnectResult result =
        execute_session(
            handshake_request(),
            options);

    assert_normal_disconnect(result);
  }

  static void test_write_cancelled_message_is_suppressed()
  {
    StreamOptions options;

    options.exhaustion =
        ExhaustionBehavior::
            ThrowRuntimeError;

    options.runtimeError =
        "write cancelled";

    const DisconnectResult result =
        execute_session(
            handshake_request(),
            options);

    assert_normal_disconnect(result);
  }

  static void test_text_message_before_disconnect_is_dispatched()
  {
    std::string input =
        handshake_request();

    append_frame(
        input,
        vix::websocket::detail::
            build_text_frame(
                "before disconnect",
                true));

    const DisconnectResult result =
        execute_session(
            std::move(input));

    assert_normal_disconnect(result);

    assert(result.messages.size() == 1u);

    assert(
        result.messages[0] ==
        "before disconnect");

    assert_messages_received_while_open(
        result);
  }

  static void test_multiple_messages_before_disconnect_preserve_order()
  {
    std::string input =
        handshake_request();

    append_frame(
        input,
        vix::websocket::detail::
            build_text_frame(
                "first",
                true));

    append_frame(
        input,
        vix::websocket::detail::
            build_text_frame(
                "second",
                true));

    append_frame(
        input,
        vix::websocket::detail::
            build_text_frame(
                "third",
                true));

    const DisconnectResult result =
        execute_session(
            std::move(input));

    assert_normal_disconnect(result);

    assert(result.messages.size() == 3u);

    assert(result.messages[0] == "first");
    assert(result.messages[1] == "second");
    assert(result.messages[2] == "third");

    assert_messages_received_while_open(
        result);
  }

  static void test_partial_frame_then_eof_is_normal_disconnect()
  {
    std::string input =
        handshake_request();

    input.push_back(
        static_cast<char>(
            0x81));

    const DisconnectResult result =
        execute_session(
            std::move(input));

    assert_normal_disconnect(result);

    assert(result.messages.empty());
  }

  static void test_partial_payload_then_eof_is_normal_disconnect()
  {
    std::string input =
        handshake_request();

    input.push_back(
        static_cast<char>(
            0x81));

    input.push_back(
        static_cast<char>(
            0x85));

    input.push_back(
        static_cast<char>(
            0x01));

    input.push_back(
        static_cast<char>(
            0x02));

    const DisconnectResult result =
        execute_session(
            std::move(input));

    assert_normal_disconnect(result);

    assert(result.messages.empty());
  }

  static void test_clean_close_frame_is_not_an_error()
  {
    std::string input =
        handshake_request();

    append_frame(
        input,
        vix::websocket::detail::
            build_close_frame(
                true));

    const DisconnectResult result =
        execute_session(
            std::move(input));

    assert_normal_disconnect(result);

    assert(result.messages.empty());

    assert(
        result.streamCloseCalls ==
        1u);
  }

  static void test_clean_close_after_message()
  {
    std::string input =
        handshake_request();

    append_frame(
        input,
        vix::websocket::detail::
            build_text_frame(
                "last message",
                true));

    append_frame(
        input,
        vix::websocket::detail::
            build_close_frame(
                true));

    const DisconnectResult result =
        execute_session(
            std::move(input));

    assert_normal_disconnect(result);

    assert(result.messages.size() == 1u);

    assert(
        result.messages[0] ==
        "last message");

    assert_messages_received_while_open(
        result);
  }

  static void test_transport_closed_after_handshake_is_clean()
  {
    StreamOptions options;

    options.closeAfterFirstWrite =
        true;

    const DisconnectResult result =
        execute_session(
            handshake_request(),
            options);

    assert_normal_disconnect(result);

    assert(result.messages.empty());

    /*
     * The remote side already marked the transport closed, so the
     * session does not call close() on it a second time.
     */
    assert(
        result.streamCloseCalls ==
        0u);
  }

  static void test_unexpected_transport_error_is_forwarded()
  {
    StreamOptions options;

    options.exhaustion =
        ExhaustionBehavior::
            ThrowRuntimeError;

    options.runtimeError =
        "TLS framing failure";

    const DisconnectResult result =
        execute_session(
            handshake_request(),
            options);

    assert(result.openCalls == 1u);
    assert(result.closeCalls == 1u);

    assert(result.openCallbackState);
    assert(!result.closeCallbackState);

    assert(result.errors.size() == 1u);

    assert(
        result.errors[0] ==
        "TLS framing failure");

    assert(
        result.errorOpenStates.size() ==
        1u);

    assert(result.errorOpenStates[0]);

    assert(!result.finalOpenState);
    assert(!result.streamOpen);

    assert(
        result.output ==
        handshake_response());

    assert(
        result.streamCloseCalls ==
        1u);
  }

  static void test_abrupt_eof_during_handshake_is_suppressed()
  {
    const std::string input =
        "GET /disconnect HTTP/1.1\r\n"
        "Upgrade: websocket\r\n";

    const DisconnectResult result =
        execute_session(input);

    assert(result.openCalls == 0u);
    assert(result.closeCalls == 1u);

    assert(result.errors.empty());
    assert(result.messages.empty());

    assert(!result.closeCallbackState);
    assert(!result.finalOpenState);
    assert(!result.streamOpen);

    assert(result.output.empty());

    assert(
        result.streamCloseCalls ==
        1u);
  }

  static void test_empty_transport_is_safe()
  {
    const DisconnectResult result =
        execute_session("");

    assert(result.openCalls == 0u);
    assert(result.closeCalls == 1u);

    assert(result.errors.empty());
    assert(result.messages.empty());

    assert(result.output.empty());

    assert(!result.finalOpenState);
    assert(!result.streamOpen);

    assert(
        result.streamCloseCalls ==
        1u);
  }

  static void test_disconnect_supports_partial_handshake_reads()
  {
    StreamOptions options;

    options.maxReadChunk = 1u;

    const DisconnectResult result =
        execute_session(
            handshake_request(),
            options);

    assert_normal_disconnect(result);

    assert(result.streamReadCalls > 100u);
  }

  static void test_disconnect_without_router_is_safe()
  {
    StreamOptions options;

    options.exhaustion =
        ExhaustionBehavior::
            ThrowRuntimeError;

    options.runtimeError =
        "broken pipe";

    const DisconnectResult result =
        execute_session(
            handshake_request(),
            options,
            false);

    assert(result.openCalls == 0u);
    assert(result.closeCalls == 0u);

    assert(result.messages.empty());
    assert(result.errors.empty());

    assert(!result.finalOpenState);
    assert(!result.streamOpen);

    assert(
        result.output ==
        handshake_response());

    assert(
        result.streamCloseCalls ==
        1u);
  }

  static void test_close_callback_is_emitted_once()
  {
    std::string input =
        handshake_request();

    append_frame(
        input,
        vix::websocket::detail::
            build_close_frame(
                true));

    append_frame(
        input,
        vix::websocket::detail::
            build_close_frame(
                true));

    const DisconnectResult result =
        execute_session(
            std::move(input));

    assert(result.openCalls == 1u);
    assert(result.closeCalls == 1u);

    assert(result.errors.empty());

    assert(!result.finalOpenState);
    assert(!result.streamOpen);

    assert(
        result.streamCloseCalls ==
        1u);
  }

} // namespace

int main()
{
  test_eof_after_handshake_is_normal_disconnect();

  test_connection_reset_is_normal_disconnect();
  test_broken_pipe_is_normal_disconnect();

  test_operation_cancelled_is_normal_disconnect();
  test_read_cancelled_is_normal_disconnect();
  test_write_cancelled_message_is_suppressed();

  test_text_message_before_disconnect_is_dispatched();
  test_multiple_messages_before_disconnect_preserve_order();

  test_partial_frame_then_eof_is_normal_disconnect();
  test_partial_payload_then_eof_is_normal_disconnect();

  test_clean_close_frame_is_not_an_error();
  test_clean_close_after_message();

  test_transport_closed_after_handshake_is_clean();

  test_unexpected_transport_error_is_forwarded();

  test_abrupt_eof_during_handshake_is_suppressed();
  test_empty_transport_is_safe();

  test_disconnect_supports_partial_handshake_reads();

  test_disconnect_without_router_is_safe();
  test_close_callback_is_emitted_once();

  return 0;
}
