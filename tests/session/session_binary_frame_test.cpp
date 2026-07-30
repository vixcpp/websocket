/**
 *
 * @file session_binary_frame_test.cpp
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
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
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
    return "GET /binary HTTP/1.1\r\n"
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
  make_binary_frame(
      const std::vector<std::byte> &payload)
  {
    return vix::websocket::detail::
        build_frame(
            Opcode::Binary,
            payload,
            true,
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

  static void assert_binary_payload(
      const std::string &actual,
      const std::vector<std::byte> &expected)
  {
    assert(
        actual.size() ==
        expected.size());

    for (std::size_t index = 0u;
         index < expected.size();
         ++index)
    {
      const auto actualByte =
          static_cast<unsigned char>(
              actual[index]);

      const auto expectedByte =
          std::to_integer<
              unsigned char>(
              expected[index]);

      assert(
          actualByte ==
          expectedByte);
    }
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

  static void test_masked_binary_frame_is_dispatched()
  {
    const auto expected =
        bytes(
            {
                0x01u,
                0x02u,
                0x03u,
                0x04u,
            });

    const SessionResult result =
        execute_frames(
            {
                make_binary_frame(
                    expected),
            });

    assert_clean_session(result);

    assert(result.messages.size() == 1u);

    assert_binary_payload(
        result.messages[0],
        expected);

    assert_all_messages_received_while_open(
        result);
  }

  static void test_empty_binary_frame_is_dispatched()
  {
    const std::vector<std::byte>
        expected{};

    const SessionResult result =
        execute_frames(
            {
                make_binary_frame(
                    expected),
            });

    assert_clean_session(result);

    assert(result.messages.size() == 1u);
    assert(result.messages[0].empty());

    assert_all_messages_received_while_open(
        result);
  }

  static void test_binary_payload_preserves_null_bytes()
  {
    const auto expected =
        bytes(
            {
                0x00u,
                0x01u,
                0x00u,
                0x02u,
                0x00u,
                0x03u,
            });

    const SessionResult result =
        execute_frames(
            {
                make_binary_frame(
                    expected),
            });

    assert_clean_session(result);

    assert(result.messages.size() == 1u);

    assert_binary_payload(
        result.messages[0],
        expected);
  }

  static void test_binary_payload_preserves_high_bytes()
  {
    const auto expected =
        bytes(
            {
                0x80u,
                0x81u,
                0xFEu,
                0xFFu,
                0x7Fu,
            });

    const SessionResult result =
        execute_frames(
            {
                make_binary_frame(
                    expected),
            });

    assert_clean_session(result);

    assert(result.messages.size() == 1u);

    assert_binary_payload(
        result.messages[0],
        expected);
  }

  static void test_binary_payload_preserves_all_byte_values()
  {
    std::vector<std::byte> expected;
    expected.reserve(256u);

    for (std::size_t value = 0u;
         value < 256u;
         ++value)
    {
      expected.push_back(
          static_cast<std::byte>(
              static_cast<unsigned char>(
                  value)));
    }

    const SessionResult result =
        execute_frames(
            {
                make_binary_frame(
                    expected),
            });

    assert_clean_session(result);

    assert(result.messages.size() == 1u);

    assert_binary_payload(
        result.messages[0],
        expected);
  }

  static void test_multiple_binary_frames_preserve_order()
  {
    const auto first =
        bytes(
            {
                0x01u,
                0x02u,
            });

    const auto second =
        bytes(
            {
                0x10u,
                0x20u,
                0x30u,
            });

    const auto third =
        bytes(
            {
                0xFFu,
                0x00u,
                0xAAu,
                0x55u,
            });

    const SessionResult result =
        execute_frames(
            {
                make_binary_frame(first),
                make_binary_frame(second),
                make_binary_frame(third),
            });

    assert_clean_session(result);

    assert(result.messages.size() == 3u);

    assert_binary_payload(
        result.messages[0],
        first);

    assert_binary_payload(
        result.messages[1],
        second);

    assert_binary_payload(
        result.messages[2],
        third);

    assert_all_messages_received_while_open(
        result);
  }

  static void test_extended_16_bit_binary_payload()
  {
    std::vector<std::byte> expected;
    expected.reserve(300u);

    for (std::size_t index = 0u;
         index < 300u;
         ++index)
    {
      expected.push_back(
          static_cast<std::byte>(
              static_cast<unsigned char>(
                  index % 256u)));
    }

    const SessionResult result =
        execute_frames(
            {
                make_binary_frame(
                    expected),
            });

    assert_clean_session(result);

    assert(result.messages.size() == 1u);

    assert_binary_payload(
        result.messages[0],
        expected);
  }

  static void test_large_binary_payload_preserves_pattern()
  {
    std::vector<std::byte> expected;
    expected.reserve(4096u);

    for (std::size_t index = 0u;
         index < 4096u;
         ++index)
    {
      const unsigned char value =
          static_cast<unsigned char>(
              (index * 17u) %
              256u);

      expected.push_back(
          static_cast<std::byte>(
              value));
    }

    const SessionResult result =
        execute_frames(
            {
                make_binary_frame(
                    expected),
            });

    assert_clean_session(result);

    assert(result.messages.size() == 1u);

    assert_binary_payload(
        result.messages[0],
        expected);
  }

  static void test_partial_reads_preserve_binary_payload()
  {
    const auto expected =
        bytes(
            {
                0x00u,
                0x10u,
                0x20u,
                0x30u,
                0x40u,
                0x50u,
                0x60u,
                0x70u,
                0x80u,
                0x90u,
                0xA0u,
                0xB0u,
                0xC0u,
                0xD0u,
                0xE0u,
                0xF0u,
                0xFFu,
            });

    const SessionResult result =
        execute_frames(
            {
                make_binary_frame(
                    expected),
            },
            3u);

    assert_clean_session(result);

    assert(result.messages.size() == 1u);

    assert_binary_payload(
        result.messages[0],
        expected);

    assert(result.streamReadCalls > 1u);
  }

  static void test_single_byte_reads_preserve_binary_payload()
  {
    const auto expected =
        bytes(
            {
                0x00u,
                0x01u,
                0x02u,
                0x03u,
                0x04u,
                0x05u,
                0x06u,
                0x07u,
                0x08u,
                0x09u,
                0xFFu,
            });

    const SessionResult result =
        execute_frames(
            {
                make_binary_frame(
                    expected),
            },
            1u);

    assert_clean_session(result);

    assert(result.messages.size() == 1u);

    assert_binary_payload(
        result.messages[0],
        expected);

    assert(result.streamReadCalls > 20u);
  }

  static void test_binary_frame_can_follow_handshake_in_same_read()
  {
    const auto expected =
        bytes(
            {
                0xDEu,
                0xADu,
                0xBEu,
                0xEFu,
            });

    const SessionResult result =
        execute_frames(
            {
                make_binary_frame(
                    expected),
            },
            8192u);

    assert_clean_session(result);

    assert(result.messages.size() == 1u);

    assert_binary_payload(
        result.messages[0],
        expected);

    assert(result.streamReadCalls == 1u);
  }

  static void test_many_binary_frames_are_dispatched()
  {
    constexpr std::size_t frameCount = 100u;

    std::vector<
        std::vector<std::byte>>
        payloads;

    std::vector<
        std::vector<std::byte>>
        frames;

    payloads.reserve(frameCount);
    frames.reserve(frameCount);

    for (std::size_t index = 0u;
         index < frameCount;
         ++index)
    {
      payloads.push_back(
          bytes(
              {
                  static_cast<unsigned int>(
                      index % 256u),
                  static_cast<unsigned int>(
                      (index + 1u) % 256u),
                  static_cast<unsigned int>(
                      (index + 2u) % 256u),
              }));

      frames.push_back(
          make_binary_frame(
              payloads.back()));
    }

    const SessionResult result =
        execute_frames(frames);

    assert_clean_session(result);

    assert(
        result.messages.size() ==
        frameCount);

    for (std::size_t index = 0u;
         index < frameCount;
         ++index)
    {
      assert_binary_payload(
          result.messages[index],
          payloads[index]);
    }

    assert_all_messages_received_while_open(
        result);
  }

  static void test_binary_opcode_uses_raw_message_handler()
  {
    const auto expected =
        bytes(
            {
                0x56u,
                0x69u,
                0x78u,
                0x00u,
                0xFFu,
            });

    const auto frame =
        vix::websocket::detail::
            build_frame(
                Opcode::Binary,
                expected,
                true,
                true);

    const SessionResult result =
        execute_frames(
            {
                frame,
            });

    assert_clean_session(result);

    assert(result.messages.size() == 1u);

    assert_binary_payload(
        result.messages[0],
        expected);
  }

  static void test_binary_frame_is_not_text_transformed()
  {
    const auto expected =
        bytes(
            {
                0x41u,
                0x00u,
                0x42u,
                0x0Au,
                0x43u,
                0xFFu,
            });

    const SessionResult result =
        execute_frames(
            {
                make_binary_frame(
                    expected),
            });

    assert_clean_session(result);

    assert(result.messages.size() == 1u);

    assert(
        result.messages[0].size() ==
        expected.size());

    assert_binary_payload(
        result.messages[0],
        expected);
  }

} // namespace

int main()
{
  test_masked_binary_frame_is_dispatched();
  test_empty_binary_frame_is_dispatched();

  test_binary_payload_preserves_null_bytes();
  test_binary_payload_preserves_high_bytes();
  test_binary_payload_preserves_all_byte_values();

  test_multiple_binary_frames_preserve_order();

  test_extended_16_bit_binary_payload();
  test_large_binary_payload_preserves_pattern();

  test_partial_reads_preserve_binary_payload();
  test_single_byte_reads_preserve_binary_payload();

  test_binary_frame_can_follow_handshake_in_same_read();
  test_many_binary_frames_are_dispatched();

  test_binary_opcode_uses_raw_message_handler();
  test_binary_frame_is_not_text_transformed();

  return 0;
}
