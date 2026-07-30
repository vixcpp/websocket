/**
 *
 * @file session_send_queue_test.cpp
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
#include <initializer_list>
#include <memory>
#include <span>
#include <stdexcept>
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

  struct StreamOptions
  {
    std::size_t maxWriteChunk{8192u};

    std::size_t failWriteAttempt{0u};
    bool throwOnFailure{false};

    std::string failureMessage{
        "queue write failed"};

    bool initiallyOpen{true};
  };

  class CaptureTcpStream final
      : public tcp_stream
  {
  public:
    explicit CaptureTcpStream(
        StreamOptions options = {})
        : options_{
              std::move(options)},
          open_{
              options_.initiallyOpen}
    {
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
        std::span<std::byte>,
        cancel_token = {}) override
    {
      co_return 0u;
    }

    vix::async::core::task<std::size_t>
    async_write(
        std::span<const std::byte> buffer,
        cancel_token = {}) override
    {
      writeAttempts_ += 1u;

      requestedWriteSizes_.push_back(
          buffer.size());

      if (!open_ ||
          buffer.empty())
      {
        co_return 0u;
      }

      if (options_.failWriteAttempt != 0u &&
          writeAttempts_ ==
              options_.failWriteAttempt)
      {
        if (options_.throwOnFailure)
        {
          throw std::runtime_error{
              options_.failureMessage};
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

    [[nodiscard]]
    const std::vector<std::size_t> &
    requested_write_sizes() const noexcept
    {
      return requestedWriteSizes_;
    }

  private:
    StreamOptions options_{};

    std::string output_{};

    std::vector<std::size_t>
        requestedWriteSizes_{};

    std::size_t writeAttempts_{0u};
    std::size_t successfulWrites_{0u};
    std::size_t closeCalls_{0u};

    bool open_{true};
  };

  struct QueueResult
  {
    std::string output{};

    std::vector<std::string> errors{};

    std::size_t closeCalls{0u};

    bool finalOpenState{false};
    bool streamOpen{false};

    std::size_t streamWriteAttempts{0u};
    std::size_t streamSuccessfulWrites{0u};
    std::size_t streamCloseCalls{0u};

    std::vector<std::size_t>
        requestedWriteSizes{};
  };

  static std::vector<std::byte>
  make_bytes(
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

  static std::vector<Frame>
  decode_frames(
      const std::string &wire)
  {
    std::vector<std::byte> bytes;
    bytes.resize(wire.size());

    if (!wire.empty())
    {
      std::memcpy(
          bytes.data(),
          wire.data(),
          wire.size());
    }

    std::vector<Frame> frames;

    std::size_t offset = 0u;

    while (offset <
           bytes.size())
    {
      const auto header =
          vix::websocket::detail::
              parse_frame_header(
                  bytes.data() +
                      offset,
                  bytes.size() -
                      offset);

      const std::size_t frameSize =
          header.header_size +
          header.payload_length;

      assert(frameSize >= 2u);

      assert(
          offset + frameSize <=
          bytes.size());

      std::vector<std::byte> frameBytes(
          bytes.begin() +
              static_cast<
                  std::ptrdiff_t>(
                  offset),
          bytes.begin() +
              static_cast<
                  std::ptrdiff_t>(
                  offset +
                  frameSize));

      frames.push_back(
          vix::websocket::detail::
              decode_frame(
                  frameBytes));

      offset += frameSize;
    }

    return frames;
  }

  static void assert_payload(
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

  static void assert_text_frame(
      const Frame &frame,
      std::string_view expected)
  {
    assert(frame.fin);
    assert(frame.opcode == Opcode::Text);
    assert(!frame.masked);

    assert(
        frame.text() ==
        expected);
  }

  static void assert_binary_frame(
      const Frame &frame,
      const std::vector<std::byte> &expected)
  {
    assert(frame.fin);
    assert(frame.opcode == Opcode::Binary);
    assert(!frame.masked);

    assert_payload(
        frame,
        expected);
  }

  static void assert_close_frame(
      const Frame &frame)
  {
    assert(frame.fin);
    assert(frame.opcode == Opcode::Close);
    assert(!frame.masked);
    assert(frame.payload.empty());
  }

  template <typename Action>
  static QueueResult execute_queue(
      Action action,
      StreamOptions options = {},
      bool attachRouter = true)
  {
    auto context =
        std::make_shared<
            io_context>();

    std::shared_ptr<Router> router;

    QueueResult result;

    if (attachRouter)
    {
      router =
          std::make_shared<
              Router>();

      router->on_error(
          [&result](
              Session &,
              const std::string &error)
          {
            result.errors.push_back(
                error);
          });

      router->on_close(
          [&result](
              Session &)
          {
            result.closeCalls += 1u;
          });
    }

    auto stream =
        std::make_unique<
            CaptureTcpStream>(
            std::move(options));

    CaptureTcpStream *streamPointer =
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

    action(
        session,
        context);

    context->stop();
    context->run();

    result.output =
        streamPointer->output();

    result.finalOpenState =
        session->is_open();

    result.streamOpen =
        streamPointer->is_open();

    result.streamWriteAttempts =
        streamPointer->write_attempts();

    result.streamSuccessfulWrites =
        streamPointer->successful_writes();

    result.streamCloseCalls =
        streamPointer->close_calls();

    result.requestedWriteSizes =
        streamPointer->requested_write_sizes();

    return result;
  }

  static void test_text_message_is_queued_and_written()
  {
    const QueueResult result =
        execute_queue(
            [](
                const std::shared_ptr<
                    Session> &session,
                const std::shared_ptr<
                    io_context> &)
            {
              session->send_text(
                  "hello");
            });

    assert(result.errors.empty());
    assert(result.closeCalls == 0u);

    assert(result.streamOpen);
    assert(result.streamCloseCalls == 0u);

    const auto frames =
        decode_frames(
            result.output);

    assert(frames.size() == 1u);

    assert_text_frame(
        frames[0],
        "hello");
  }

  static void test_binary_message_is_queued_and_written()
  {
    const auto expected =
        make_bytes(
            {
                0x00u,
                0x01u,
                0x7Fu,
                0x80u,
                0xFFu,
            });

    const QueueResult result =
        execute_queue(
            [&expected](
                const std::shared_ptr<
                    Session> &session,
                const std::shared_ptr<
                    io_context> &)
            {
              session->send_binary(
                  expected.data(),
                  expected.size());
            });

    assert(result.errors.empty());
    assert(result.closeCalls == 0u);

    const auto frames =
        decode_frames(
            result.output);

    assert(frames.size() == 1u);

    assert_binary_frame(
        frames[0],
        expected);
  }

  static void test_mixed_messages_preserve_fifo_order()
  {
    const auto binary =
        make_bytes(
            {
                0xDEu,
                0xADu,
                0xBEu,
                0xEFu,
            });

    const QueueResult result =
        execute_queue(
            [&binary](
                const std::shared_ptr<
                    Session> &session,
                const std::shared_ptr<
                    io_context> &)
            {
              session->send_text(
                  "first");

              session->send_binary(
                  binary.data(),
                  binary.size());

              session->send_text(
                  "third");
            });

    assert(result.errors.empty());

    const auto frames =
        decode_frames(
            result.output);

    assert(frames.size() == 3u);

    assert_text_frame(
        frames[0],
        "first");

    assert_binary_frame(
        frames[1],
        binary);

    assert_text_frame(
        frames[2],
        "third");
  }

  static void test_empty_messages_are_preserved()
  {
    const QueueResult result =
        execute_queue(
            [](
                const std::shared_ptr<
                    Session> &session,
                const std::shared_ptr<
                    io_context> &)
            {
              session->send_text("");

              session->send_binary(
                  nullptr,
                  0u);
            });

    assert(result.errors.empty());

    const auto frames =
        decode_frames(
            result.output);

    assert(frames.size() == 2u);

    assert_text_frame(
        frames[0],
        "");

    assert_binary_frame(
        frames[1],
        {});
  }

  static void test_null_binary_pointer_with_size_is_empty()
  {
    const QueueResult result =
        execute_queue(
            [](
                const std::shared_ptr<
                    Session> &session,
                const std::shared_ptr<
                    io_context> &)
            {
              session->send_binary(
                  nullptr,
                  32u);
            });

    assert(result.errors.empty());

    const auto frames =
        decode_frames(
            result.output);

    assert(frames.size() == 1u);

    assert_binary_frame(
        frames[0],
        {});
  }

  static void test_text_payload_is_copied_before_dispatch()
  {
    std::string message =
        "original text";

    const QueueResult result =
        execute_queue(
            [&message](
                const std::shared_ptr<
                    Session> &session,
                const std::shared_ptr<
                    io_context> &)
            {
              session->send_text(
                  message);

              message =
                  "modified text";
            });

    const auto frames =
        decode_frames(
            result.output);

    assert(frames.size() == 1u);

    assert_text_frame(
        frames[0],
        "original text");
  }

  static void test_binary_payload_is_copied_before_dispatch()
  {
    std::vector<std::byte> payload =
        make_bytes(
            {
                0x01u,
                0x02u,
                0x03u,
                0x04u,
            });

    const auto expected =
        payload;

    const QueueResult result =
        execute_queue(
            [&payload](
                const std::shared_ptr<
                    Session> &session,
                const std::shared_ptr<
                    io_context> &)
            {
              session->send_binary(
                  payload.data(),
                  payload.size());

              std::fill(
                  payload.begin(),
                  payload.end(),
                  std::byte{0xFF});
            });

    const auto frames =
        decode_frames(
            result.output);

    assert(frames.size() == 1u);

    assert_binary_frame(
        frames[0],
        expected);
  }

  static void test_text_payload_preserves_embedded_null()
  {
    const std::string expected{
        "Vix\0WebSocket",
        13u};

    const QueueResult result =
        execute_queue(
            [&expected](
                const std::shared_ptr<
                    Session> &session,
                const std::shared_ptr<
                    io_context> &)
            {
              session->send_text(
                  std::string_view{
                      expected.data(),
                      expected.size()});
            });

    const auto frames =
        decode_frames(
            result.output);

    assert(frames.size() == 1u);

    assert_text_frame(
        frames[0],
        expected);
  }

  static void test_partial_writes_produce_complete_frame()
  {
    StreamOptions options;
    options.maxWriteChunk = 1u;

    const std::string expected =
        "partial write message";

    const QueueResult result =
        execute_queue(
            [&expected](
                const std::shared_ptr<
                    Session> &session,
                const std::shared_ptr<
                    io_context> &)
            {
              session->send_text(
                  expected);
            },
            options);

    assert(result.errors.empty());

    const auto frames =
        decode_frames(
            result.output);

    assert(frames.size() == 1u);

    assert_text_frame(
        frames[0],
        expected);

    assert(result.streamWriteAttempts > 1u);

    assert(
        result.streamWriteAttempts ==
        result.streamSuccessfulWrites);

    for (const std::size_t requested :
         result.requestedWriteSizes)
    {
      assert(requested >= 1u);
    }
  }

  static void test_large_message_is_written()
  {
    std::string expected;
    expected.reserve(70000u);

    for (std::size_t index = 0u;
         index < 70000u;
         ++index)
    {
      expected.push_back(
          static_cast<char>(
              'a' +
              index % 26u));
    }

    const QueueResult result =
        execute_queue(
            [&expected](
                const std::shared_ptr<
                    Session> &session,
                const std::shared_ptr<
                    io_context> &)
            {
              session->send_text(
                  expected);
            });

    assert(result.errors.empty());

    const auto frames =
        decode_frames(
            result.output);

    assert(frames.size() == 1u);

    assert_text_frame(
        frames[0],
        expected);
  }

  static void test_many_messages_preserve_order()
  {
    constexpr std::size_t messageCount = 32u;

    const QueueResult result =
        execute_queue(
            [](
                const std::shared_ptr<
                    Session> &session,
                const std::shared_ptr<
                    io_context> &)
            {
              for (std::size_t index = 0u;
                   index < messageCount;
                   ++index)
              {
                session->send_text(
                    "message-" +
                    std::to_string(index));
              }
            });

    assert(result.errors.empty());

    const auto frames =
        decode_frames(
            result.output);

    assert(
        frames.size() ==
        messageCount);

    for (std::size_t index = 0u;
         index < messageCount;
         ++index)
    {
      assert_text_frame(
          frames[index],
          "message-" +
              std::to_string(index));
    }
  }

  static void test_close_before_send_ignores_message()
  {
    const QueueResult result =
        execute_queue(
            [](
                const std::shared_ptr<
                    Session> &session,
                const std::shared_ptr<
                    io_context> &)
            {
              session->close(
                  "closing");

              session->send_text(
                  "ignored");

              const auto payload =
                  make_bytes(
                      {
                          0x01u,
                          0x02u,
                      });

              session->send_binary(
                  payload.data(),
                  payload.size());
            });

    assert(result.errors.empty());

    assert(result.closeCalls == 1u);
    assert(!result.streamOpen);
    assert(result.streamCloseCalls == 1u);

    const auto frames =
        decode_frames(
            result.output);

    assert(frames.size() == 1u);

    assert_close_frame(
        frames[0]);
  }

  static void test_close_discards_pending_messages()
  {
    const QueueResult result =
        execute_queue(
            [](
                const std::shared_ptr<
                    Session> &session,
                const std::shared_ptr<
                    io_context> &)
            {
              session->send_text(
                  "first pending");

              session->send_text(
                  "second pending");

              session->send_text(
                  "third pending");

              session->close(
                  "discard pending writes");
            });

    assert(result.errors.empty());

    assert(result.closeCalls == 1u);
    assert(!result.streamOpen);

    const auto frames =
        decode_frames(
            result.output);

    assert(frames.size() == 1u);

    assert_close_frame(
        frames[0]);
  }

  static void test_repeated_close_queues_single_close_frame()
  {
    const QueueResult result =
        execute_queue(
            [](
                const std::shared_ptr<
                    Session> &session,
                const std::shared_ptr<
                    io_context> &)
            {
              session->close("first");
              session->close("second");
              session->close("third");
            });

    assert(result.errors.empty());

    assert(result.closeCalls == 1u);
    assert(result.streamCloseCalls == 1u);

    const auto frames =
        decode_frames(
            result.output);

    assert(frames.size() == 1u);

    assert_close_frame(
        frames[0]);
  }

  static void test_shutdown_now_prevents_queueing()
  {
    const QueueResult result =
        execute_queue(
            [](
                const std::shared_ptr<
                    Session> &session,
                const std::shared_ptr<
                    io_context> &)
            {
              session->shutdown_now();

              session->send_text(
                  "ignored");

              const auto payload =
                  make_bytes(
                      {
                          0xAAu,
                          0xBBu,
                      });

              session->send_binary(
                  payload.data(),
                  payload.size());
            });

    assert(result.errors.empty());

    assert(result.closeCalls == 0u);

    assert(result.output.empty());

    assert(!result.streamOpen);
    assert(result.streamCloseCalls == 1u);
  }

  static void test_zero_length_write_reports_error()
  {
    StreamOptions options;

    options.failWriteAttempt = 1u;
    options.throwOnFailure = false;

    const QueueResult result =
        execute_queue(
            [](
                const std::shared_ptr<
                    Session> &session,
                const std::shared_ptr<
                    io_context> &)
            {
              session->send_text(
                  "will fail");
            },
            options);

    assert(result.errors.size() == 1u);

    assert(
        result.errors[0] ==
        "websocket frame write failed");

    assert(result.closeCalls == 0u);

    assert(result.output.empty());

    assert(!result.streamOpen);
    assert(result.streamCloseCalls == 1u);

    assert(result.streamWriteAttempts == 1u);
    assert(result.streamSuccessfulWrites == 0u);
  }

  static void test_write_exception_is_forwarded()
  {
    StreamOptions options;

    options.failWriteAttempt = 1u;
    options.throwOnFailure = true;

    options.failureMessage =
        "queue transport exploded";

    const QueueResult result =
        execute_queue(
            [](
                const std::shared_ptr<
                    Session> &session,
                const std::shared_ptr<
                    io_context> &)
            {
              session->send_text(
                  "will throw");
            },
            options);

    assert(result.errors.size() == 1u);

    assert(
        result.errors[0] ==
        "queue transport exploded");

    assert(result.closeCalls == 0u);

    assert(result.output.empty());

    assert(!result.streamOpen);
    assert(result.streamCloseCalls == 1u);
  }

  static void test_normal_write_disconnect_is_suppressed()
  {
    StreamOptions options;

    options.failWriteAttempt = 1u;
    options.throwOnFailure = true;

    options.failureMessage =
        "broken pipe";

    const QueueResult result =
        execute_queue(
            [](
                const std::shared_ptr<
                    Session> &session,
                const std::shared_ptr<
                    io_context> &)
            {
              session->send_text(
                  "will disconnect");
            },
            options);

    assert(result.errors.empty());
    assert(result.closeCalls == 0u);

    assert(result.output.empty());

    assert(!result.streamOpen);
    assert(result.streamCloseCalls == 1u);
  }

  static void test_closed_stream_reports_not_open()
  {
    StreamOptions options;
    options.initiallyOpen = false;

    const QueueResult result =
        execute_queue(
            [](
                const std::shared_ptr<
                    Session> &session,
                const std::shared_ptr<
                    io_context> &)
            {
              session->send_text(
                  "closed stream");
            },
            options);

    assert(result.errors.size() == 1u);

    assert(
        result.errors[0] ==
        "stream not open");

    assert(result.closeCalls == 0u);

    assert(result.output.empty());

    assert(!result.streamOpen);
    assert(result.streamCloseCalls == 0u);

    assert(result.streamWriteAttempts == 0u);
  }

  static void test_write_failure_without_router_is_safe()
  {
    StreamOptions options;

    options.failWriteAttempt = 1u;
    options.throwOnFailure = true;

    options.failureMessage =
        "unhandled queue failure";

    const QueueResult result =
        execute_queue(
            [](
                const std::shared_ptr<
                    Session> &session,
                const std::shared_ptr<
                    io_context> &)
            {
              session->send_text(
                  "will fail");
            },
            options,
            false);

    assert(result.errors.empty());
    assert(result.closeCalls == 0u);

    assert(result.output.empty());

    assert(!result.streamOpen);
    assert(result.streamCloseCalls == 1u);
  }

} // namespace

int main()
{
  test_text_message_is_queued_and_written();
  test_binary_message_is_queued_and_written();

  test_mixed_messages_preserve_fifo_order();

  test_empty_messages_are_preserved();
  test_null_binary_pointer_with_size_is_empty();

  test_text_payload_is_copied_before_dispatch();
  test_binary_payload_is_copied_before_dispatch();

  test_text_payload_preserves_embedded_null();

  test_partial_writes_produce_complete_frame();
  test_large_message_is_written();

  test_many_messages_preserve_order();

  test_close_before_send_ignores_message();
  test_close_discards_pending_messages();
  test_repeated_close_queues_single_close_frame();

  test_shutdown_now_prevents_queueing();

  test_zero_length_write_reports_error();
  test_write_exception_is_forwarded();

  test_normal_write_disconnect_is_suppressed();
  test_closed_stream_reports_not_open();

  test_write_failure_without_router_is_safe();

  return 0;
}
