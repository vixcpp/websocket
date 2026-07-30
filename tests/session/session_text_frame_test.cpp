/**
 *
 * @file session_text_frame_test.cpp
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

  using Opcode =
      vix::websocket::detail::Opcode;

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

    std::size_t readOffset_{0u};

    std::size_t maxReadChunk_{8192u};
    std::size_t maxWriteChunk_{8192u};

    std::size_t readCalls_{0u};
    std::size_t writeCalls_{0u};
    std::size_t closeCalls_{0u};

    bool open_{true};
  };

  struct SessionResult
  {
    std::vector<std::string> messages{};
    std::vector<bool> messageOpenStates{};
    std::vector<std::string> errors{};

    std::size_t openCalls{0u};
    std::size_t closeCalls{0u};
    std::size_t errorCalls{0u};

    bool openCallbackState{false};
    bool closeCallbackState{true};
    bool finalOpenState{false};
    bool streamOpen{false};

    std::size_t streamReadCalls{0u};
    std::size_t streamWriteCalls{0u};
    std::size_t streamCloseCalls{0u};

    std::string response{};
  };

  static std::string handshake_request()
  {
    return "GET /chat HTTP/1.1\r\n"
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

  static std::vector<std::byte>
  make_text_frame(
      std::string_view message)
  {
    return vix::websocket::detail::
        build_text_frame(
            message,
            true);
  }

  static task_void run_session(
      const std::shared_ptr<Session> &session,
      io_context &context)
  {
    co_await session->run();

    context.stop();

    co_return;
  }

  static SessionResult execute_frames(
      const std::vector<
          std::vector<std::byte>> &frames,
      std::size_t maxReadChunk = 8192u,
      std::size_t maxWriteChunk = 8192u)
  {
    std::string wire =
        handshake_request();

    for (const auto &frame :
         frames)
    {
      append_frame(
          wire,
          frame);
    }

    append_frame(
        wire,
        vix::websocket::detail::
            build_close_frame(true));

    auto context =
        std::make_shared<
            io_context>();

    auto router =
        std::make_shared<
            Router>();

    SessionResult result;

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
            std::move(wire),
            maxReadChunk,
            maxWriteChunk);

    ScriptedTcpStream *streamPointer =
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

    result.response =
        streamPointer->output();

    return result;
  }

  static void assert_clean_session(
      const SessionResult &result)
  {
    assert(result.openCalls == 1u);
    assert(result.closeCalls == 1u);

    assert(result.errorCalls == 0u);
    assert(result.errors.empty());

    assert(result.openCallbackState);
    assert(!result.closeCallbackState);

    assert(!result.finalOpenState);
    assert(!result.streamOpen);

    assert(result.streamCloseCalls == 1u);

    assert(
        result.response ==
        handshake_response());
  }

  static void assert_all_messages_received_while_open(
      const SessionResult &result)
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

  static void test_masked_text_frame_is_dispatched()
  {
    const SessionResult result =
        execute_frames(
            {
                make_text_frame(
                    "hello"),
            });

    assert_clean_session(result);

    assert(result.messages.size() == 1u);
    assert(result.messages[0] == "hello");

    assert_all_messages_received_while_open(
        result);
  }

  static void test_empty_text_frame_is_dispatched()
  {
    const SessionResult result =
        execute_frames(
            {
                make_text_frame(""),
            });

    assert_clean_session(result);

    assert(result.messages.size() == 1u);
    assert(result.messages[0].empty());

    assert_all_messages_received_while_open(
        result);
  }

  static void test_multiple_text_frames_preserve_order()
  {
    const SessionResult result =
        execute_frames(
            {
                make_text_frame("first"),
                make_text_frame("second"),
                make_text_frame("third"),
                make_text_frame("fourth"),
            });

    assert_clean_session(result);

    assert(result.messages.size() == 4u);

    assert(result.messages[0] == "first");
    assert(result.messages[1] == "second");
    assert(result.messages[2] == "third");
    assert(result.messages[3] == "fourth");

    assert_all_messages_received_while_open(
        result);
  }

  static void test_text_frame_preserves_spaces()
  {
    const std::string expected =
        "  hello from Vix  ";

    const SessionResult result =
        execute_frames(
            {
                make_text_frame(expected),
            });

    assert_clean_session(result);

    assert(result.messages.size() == 1u);
    assert(result.messages[0] == expected);
  }

  static void test_text_frame_preserves_newlines()
  {
    const std::string expected =
        "line one\n"
        "line two\r\n"
        "line three";

    const SessionResult result =
        execute_frames(
            {
                make_text_frame(expected),
            });

    assert_clean_session(result);

    assert(result.messages.size() == 1u);
    assert(result.messages[0] == expected);
  }

  static void test_text_frame_preserves_embedded_null()
  {
    const std::string expected{
        "hello\0vix",
        9u};

    const SessionResult result =
        execute_frames(
            {
                make_text_frame(expected),
            });

    assert_clean_session(result);

    assert(result.messages.size() == 1u);

    assert(
        result.messages[0].size() ==
        expected.size());

    assert(result.messages[0] == expected);
  }

  static void test_text_frame_preserves_utf8_bytes()
  {
    const std::string expected =
        "Bonjour, WebSocket! "
        "\xF0\x9F\x8C\x8D";

    const SessionResult result =
        execute_frames(
            {
                make_text_frame(expected),
            });

    assert_clean_session(result);

    assert(result.messages.size() == 1u);
    assert(result.messages[0] == expected);
  }

  static void test_extended_16_bit_text_payload()
  {
    const std::string expected(
        300u,
        'x');

    const SessionResult result =
        execute_frames(
            {
                make_text_frame(expected),
            });

    assert_clean_session(result);

    assert(result.messages.size() == 1u);
    assert(result.messages[0] == expected);
  }

  static void test_extended_text_payload_preserves_pattern()
  {
    std::string expected;
    expected.reserve(1024u);

    for (std::size_t index = 0u;
         index < 1024u;
         ++index)
    {
      expected.push_back(
          static_cast<char>(
              'a' +
              index % 26u));
    }

    const SessionResult result =
        execute_frames(
            {
                make_text_frame(expected),
            });

    assert_clean_session(result);

    assert(result.messages.size() == 1u);
    assert(result.messages[0] == expected);
  }

  static void test_partial_reads_preserve_text_frame()
  {
    const std::string expected =
        "text received through partial transport reads";

    const SessionResult result =
        execute_frames(
            {
                make_text_frame(expected),
            },
            3u);

    assert_clean_session(result);

    assert(result.messages.size() == 1u);
    assert(result.messages[0] == expected);

    assert(result.streamReadCalls > 1u);
  }

  static void test_single_byte_reads_preserve_text_frame()
  {
    const std::string expected =
        "single-byte-read";

    const SessionResult result =
        execute_frames(
            {
                make_text_frame(expected),
            },
            1u);

    assert_clean_session(result);

    assert(result.messages.size() == 1u);
    assert(result.messages[0] == expected);

    assert(result.streamReadCalls > 20u);
  }

  static void test_frames_can_follow_handshake_in_same_read()
  {
    const SessionResult result =
        execute_frames(
            {
                make_text_frame("buffered-frame"),
            },
            8192u);

    assert_clean_session(result);

    assert(result.messages.size() == 1u);

    assert(
        result.messages[0] ==
        "buffered-frame");

    assert(result.streamReadCalls == 1u);
  }

  static void test_many_text_frames_are_dispatched()
  {
    constexpr std::size_t messageCount = 100u;

    std::vector<
        std::vector<std::byte>>
        frames;

    frames.reserve(messageCount);

    for (std::size_t index = 0u;
         index < messageCount;
         ++index)
    {
      frames.push_back(
          make_text_frame(
              "message-" +
              std::to_string(index)));
    }

    const SessionResult result =
        execute_frames(frames);

    assert_clean_session(result);

    assert(
        result.messages.size() ==
        messageCount);

    for (std::size_t index = 0u;
         index < messageCount;
         ++index)
    {
      assert(
          result.messages[index] ==
          "message-" +
              std::to_string(index));
    }

    assert_all_messages_received_while_open(
        result);
  }

  static void test_text_opcode_is_dispatched_as_message()
  {
    const std::string expected =
        "explicit-text-opcode";

    std::vector<std::byte> payload;
    payload.reserve(expected.size());

    for (const char character :
         expected)
    {
      payload.push_back(
          static_cast<std::byte>(
              static_cast<unsigned char>(
                  character)));
    }

    const auto frame =
        vix::websocket::detail::
            build_frame(
                Opcode::Text,
                payload,
                true,
                true);

    const SessionResult result =
        execute_frames(
            {
                frame,
            });

    assert_clean_session(result);

    assert(result.messages.size() == 1u);
    assert(result.messages[0] == expected);
  }

} // namespace

int main()
{
  test_masked_text_frame_is_dispatched();
  test_empty_text_frame_is_dispatched();

  test_multiple_text_frames_preserve_order();

  test_text_frame_preserves_spaces();
  test_text_frame_preserves_newlines();
  test_text_frame_preserves_embedded_null();
  test_text_frame_preserves_utf8_bytes();

  test_extended_16_bit_text_payload();
  test_extended_text_payload_preserves_pattern();

  test_partial_reads_preserve_text_frame();
  test_single_byte_reads_preserve_text_frame();

  test_frames_can_follow_handshake_in_same_read();
  test_many_text_frames_are_dispatched();

  test_text_opcode_is_dispatched_as_message();

  return 0;
}
