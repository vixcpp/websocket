/**
 *
 * @file session_ping_pong_test.cpp
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

  using Frame =
      vix::websocket::detail::Frame;

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
    std::string output{};

    std::vector<std::string> messages{};
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
  };

  static std::string handshake_request()
  {
    return "GET /ping HTTP/1.1\r\n"
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

  static std::vector<std::byte>
  bytes(
      std::initializer_list<
          unsigned int>
          values)
  {
    std::vector<std::byte> result;
    result.reserve(values.size());

    for (const unsigned int value :
         values)
    {
      assert(value <= 255u);

      result.push_back(
          static_cast<std::byte>(
              static_cast<unsigned char>(
                  value)));
    }

    return result;
  }

  static std::vector<std::byte>
  text_bytes(
      std::string_view text)
  {
    std::vector<std::byte> result;
    result.reserve(text.size());

    for (const char character :
         text)
    {
      result.push_back(
          static_cast<std::byte>(
              static_cast<unsigned char>(
                  character)));
    }

    return result;
  }

  static void append_frame(
      std::string &wire,
      const std::vector<std::byte> &frame)
  {
    wire.append(
        reinterpret_cast<
            const char *>(
            frame.data()),
        frame.size());
  }

  static std::vector<std::byte>
  make_ping_frame(
      const std::vector<std::byte> &payload)
  {
    return vix::websocket::detail::
        build_frame(
            Opcode::Ping,
            payload,
            true,
            true);
  }

  static std::vector<std::byte>
  make_pong_frame(
      const std::vector<std::byte> &payload)
  {
    return vix::websocket::detail::
        build_frame(
            Opcode::Pong,
            payload,
            true,
            true);
  }

  static std::vector<Frame>
  decode_server_frames(
      const std::string &output)
  {
    const std::string prefix =
        handshake_response();

    assert(
        output.size() >=
        prefix.size());

    assert(
        output.compare(
            0u,
            prefix.size(),
            prefix) ==
        0);

    const std::size_t payloadSize =
        output.size() -
        prefix.size();

    std::vector<std::byte> bytes;
    bytes.resize(payloadSize);

    if (payloadSize != 0u)
    {
      std::memcpy(
          bytes.data(),
          output.data() +
              prefix.size(),
          payloadSize);
    }

    std::vector<Frame> frames;

    std::size_t offset = 0u;

    while (offset < bytes.size())
    {
      const auto header =
          vix::websocket::detail::
              parse_frame_header(
                  bytes.data() + offset,
                  bytes.size() - offset);

      const std::size_t frameSize =
          header.header_size +
          header.payload_length;

      assert(frameSize > 0u);

      assert(
          offset + frameSize <=
          bytes.size());

      std::vector<std::byte> frameBytes(
          bytes.begin() +
              static_cast<
                  std::ptrdiff_t>(offset),
          bytes.begin() +
              static_cast<
                  std::ptrdiff_t>(
                  offset + frameSize));

      frames.push_back(
          vix::websocket::detail::
              decode_frame(
                  frameBytes));

      offset += frameSize;
    }

    return frames;
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
      bool autoPingPong = true,
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
            Session &,
            const std::string &message)
        {
          result.messages.push_back(
              message);
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
    config.autoPingPong =
        autoPingPong;

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
  }

  static void assert_payload_equals(
      const Frame &frame,
      const std::vector<std::byte> &expected)
  {
    assert(
        frame.payload.size() ==
        expected.size());

    for (std::size_t index = 0u;
         index < expected.size();
         ++index)
    {
      assert(
          frame.payload[index] ==
          expected[index]);
    }
  }

  static void assert_pong(
      const Frame &frame,
      const std::vector<std::byte> &expected)
  {
    assert(frame.fin);
    assert(frame.opcode == Opcode::Pong);
    assert(!frame.masked);

    assert_payload_equals(
        frame,
        expected);
  }

  static void test_ping_receives_pong()
  {
    const auto payload =
        text_bytes("hello");

    const SessionResult result =
        execute_frames(
            {
                make_ping_frame(
                    payload),
            });

    assert_clean_session(result);
    assert(result.messages.empty());

    const auto frames =
        decode_server_frames(
            result.output);

    assert(frames.size() == 1u);

    assert_pong(
        frames[0],
        payload);
  }

  static void test_empty_ping_receives_empty_pong()
  {
    const std::vector<std::byte>
        payload{};

    const SessionResult result =
        execute_frames(
            {
                make_ping_frame(
                    payload),
            });

    assert_clean_session(result);
    assert(result.messages.empty());

    const auto frames =
        decode_server_frames(
            result.output);

    assert(frames.size() == 1u);

    assert_pong(
        frames[0],
        payload);
  }

  static void test_pong_preserves_binary_payload()
  {
    const auto payload =
        bytes(
            {
                0x00u,
                0x01u,
                0x7Fu,
                0x80u,
                0xFEu,
                0xFFu,
            });

    const SessionResult result =
        execute_frames(
            {
                make_ping_frame(
                    payload),
            });

    assert_clean_session(result);

    const auto frames =
        decode_server_frames(
            result.output);

    assert(frames.size() == 1u);

    assert_pong(
        frames[0],
        payload);
  }

  static void test_multiple_pings_receive_ordered_pongs()
  {
    const auto first =
        text_bytes("first");

    const auto second =
        bytes(
            {
                0x00u,
                0x10u,
                0x20u,
            });

    const auto third =
        text_bytes("third");

    const SessionResult result =
        execute_frames(
            {
                make_ping_frame(first),
                make_ping_frame(second),
                make_ping_frame(third),
            });

    assert_clean_session(result);
    assert(result.messages.empty());

    const auto frames =
        decode_server_frames(
            result.output);

    assert(frames.size() == 3u);

    assert_pong(
        frames[0],
        first);

    assert_pong(
        frames[1],
        second);

    assert_pong(
        frames[2],
        third);
  }

  static void test_incoming_pong_is_ignored()
  {
    const SessionResult result =
        execute_frames(
            {
                make_pong_frame(
                    text_bytes(
                        "already-pong")),
            });

    assert_clean_session(result);
    assert(result.messages.empty());

    const auto frames =
        decode_server_frames(
            result.output);

    assert(frames.empty());
  }

  static void test_multiple_incoming_pongs_are_ignored()
  {
    const SessionResult result =
        execute_frames(
            {
                make_pong_frame(
                    text_bytes("one")),
                make_pong_frame(
                    text_bytes("two")),
                make_pong_frame(
                    text_bytes("three")),
            });

    assert_clean_session(result);
    assert(result.messages.empty());

    const auto frames =
        decode_server_frames(
            result.output);

    assert(frames.empty());
  }

  static void test_auto_ping_pong_can_be_disabled()
  {
    const SessionResult result =
        execute_frames(
            {
                make_ping_frame(
                    text_bytes(
                        "no-response")),
            },
            false);

    assert_clean_session(result);
    assert(result.messages.empty());

    const auto frames =
        decode_server_frames(
            result.output);

    assert(frames.empty());
  }

  static void test_disabled_ping_pong_does_not_stop_session()
  {
    const SessionResult result =
        execute_frames(
            {
                make_ping_frame(
                    text_bytes("ignored")),
                vix::websocket::detail::
                    build_text_frame(
                        "after-ping",
                        true),
            },
            false);

    assert_clean_session(result);

    assert(result.messages.size() == 1u);

    assert(
        result.messages[0] ==
        "after-ping");

    const auto frames =
        decode_server_frames(
            result.output);

    assert(frames.empty());
  }

  static void test_ping_does_not_dispatch_message()
  {
    const SessionResult result =
        execute_frames(
            {
                make_ping_frame(
                    text_bytes("control")),
                vix::websocket::detail::
                    build_text_frame(
                        "application-message",
                        true),
            });

    assert_clean_session(result);

    assert(result.messages.size() == 1u);

    assert(
        result.messages[0] ==
        "application-message");

    const auto frames =
        decode_server_frames(
            result.output);

    assert(frames.size() == 1u);

    assert_pong(
        frames[0],
        text_bytes("control"));
  }

  static void test_ping_after_text_frame_is_processed()
  {
    const auto pingPayload =
        text_bytes("heartbeat");

    const SessionResult result =
        execute_frames(
            {
                vix::websocket::detail::
                    build_text_frame(
                        "before-ping",
                        true),
                make_ping_frame(
                    pingPayload),
                vix::websocket::detail::
                    build_text_frame(
                        "after-ping",
                        true),
            });

    assert_clean_session(result);

    assert(result.messages.size() == 2u);

    assert(
        result.messages[0] ==
        "before-ping");

    assert(
        result.messages[1] ==
        "after-ping");

    const auto frames =
        decode_server_frames(
            result.output);

    assert(frames.size() == 1u);

    assert_pong(
        frames[0],
        pingPayload);
  }

  static void test_ping_supports_partial_reads()
  {
    const auto payload =
        text_bytes(
            "partial-read-ping");

    const SessionResult result =
        execute_frames(
            {
                make_ping_frame(
                    payload),
            },
            true,
            1u,
            8192u);

    assert_clean_session(result);

    const auto frames =
        decode_server_frames(
            result.output);

    assert(frames.size() == 1u);

    assert_pong(
        frames[0],
        payload);

    assert(result.streamReadCalls > 20u);
  }

  static void test_pong_supports_partial_writes()
  {
    const auto payload =
        text_bytes(
            "partial-write-pong");

    const SessionResult result =
        execute_frames(
            {
                make_ping_frame(
                    payload),
            },
            true,
            8192u,
            1u);

    assert_clean_session(result);

    const auto frames =
        decode_server_frames(
            result.output);

    assert(frames.size() == 1u);

    assert_pong(
        frames[0],
        payload);

    assert(result.streamWriteCalls > 100u);
  }

  static void test_ping_can_follow_handshake_in_same_read()
  {
    const auto payload =
        text_bytes(
            "buffered-ping");

    const SessionResult result =
        execute_frames(
            {
                make_ping_frame(
                    payload),
            });

    assert_clean_session(result);

    const auto frames =
        decode_server_frames(
            result.output);

    assert(frames.size() == 1u);

    assert_pong(
        frames[0],
        payload);

    assert(result.streamReadCalls == 1u);
  }

} // namespace

int main()
{
  test_ping_receives_pong();
  test_empty_ping_receives_empty_pong();

  test_pong_preserves_binary_payload();
  test_multiple_pings_receive_ordered_pongs();

  test_incoming_pong_is_ignored();
  test_multiple_incoming_pongs_are_ignored();

  test_auto_ping_pong_can_be_disabled();
  test_disabled_ping_pong_does_not_stop_session();

  test_ping_does_not_dispatch_message();
  test_ping_after_text_frame_is_processed();

  test_ping_supports_partial_reads();
  test_pong_supports_partial_writes();

  test_ping_can_follow_handshake_in_same_read();

  return 0;
}
