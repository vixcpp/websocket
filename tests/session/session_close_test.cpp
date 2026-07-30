/**
 *
 * @file session_close_test.cpp
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
    return "GET /close HTTP/1.1\r\n"
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
    wire.append(
        reinterpret_cast<
            const char *>(
            frame.data()),
        frame.size());
  }

  static std::vector<std::byte>
  close_payload(
      std::uint16_t status,
      std::string_view reason)
  {
    std::vector<std::byte> payload;
    payload.reserve(
        2u +
        reason.size());

    payload.push_back(
        static_cast<std::byte>(
            static_cast<unsigned char>(
                (status >> 8u) &
                0xFFu)));

    payload.push_back(
        static_cast<std::byte>(
            static_cast<unsigned char>(
                status &
                0xFFu)));

    for (const char character :
         reason)
    {
      payload.push_back(
          static_cast<std::byte>(
              static_cast<unsigned char>(
                  character)));
    }

    return payload;
  }

  static std::vector<std::byte>
  make_close_frame(
      const std::vector<std::byte> &payload = {})
  {
    return vix::websocket::detail::
        build_frame(
            Opcode::Close,
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
      std::size_t maxWriteChunk = 8192u,
      bool attachRouter = true,
      bool closeAgainAfterRun = false)
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

    auto context =
        std::make_shared<
            io_context>();

    SessionResult result;

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
    }

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

    if (closeAgainAfterRun)
    {
      session->close(
          "already closed");
    }

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

  static void assert_clean_client_close(
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
        result.output ==
        handshake_response());
  }

  static void test_empty_close_frame_closes_session()
  {
    const SessionResult result =
        execute_frames(
            {
                make_close_frame(),
            });

    assert_clean_client_close(result);
    assert(result.messages.empty());
  }

  static void test_close_status_and_reason_are_accepted()
  {
    const SessionResult result =
        execute_frames(
            {
                make_close_frame(
                    close_payload(
                        1000u,
                        "normal closure")),
            });

    assert_clean_client_close(result);
    assert(result.messages.empty());
  }

  static void test_application_close_code_is_accepted()
  {
    const SessionResult result =
        execute_frames(
            {
                make_close_frame(
                    close_payload(
                        4001u,
                        "application shutdown")),
            });

    assert_clean_client_close(result);
    assert(result.messages.empty());
  }

  static void test_close_after_text_preserves_previous_message()
  {
    const SessionResult result =
        execute_frames(
            {
                vix::websocket::detail::
                    build_text_frame(
                        "before-close",
                        true),
                make_close_frame(),
            });

    assert_clean_client_close(result);

    assert(result.messages.size() == 1u);

    assert(
        result.messages[0] ==
        "before-close");
  }

  static void test_frames_after_close_are_not_dispatched()
  {
    const SessionResult result =
        execute_frames(
            {
                make_close_frame(),
                vix::websocket::detail::
                    build_text_frame(
                        "after-close",
                        true),
            });

    assert_clean_client_close(result);

    assert(result.messages.empty());
  }

  static void test_only_first_close_frame_is_processed()
  {
    const SessionResult result =
        execute_frames(
            {
                make_close_frame(
                    close_payload(
                        1000u,
                        "first")),
                make_close_frame(
                    close_payload(
                        1001u,
                        "second")),
                make_close_frame(
                    close_payload(
                        1002u,
                        "third")),
            });

    assert_clean_client_close(result);

    assert(result.closeCalls == 1u);
    assert(result.streamCloseCalls == 1u);
  }

  static void test_close_callback_observes_closed_state()
  {
    const SessionResult result =
        execute_frames(
            {
                make_close_frame(),
            });

    assert_clean_client_close(result);

    assert(!result.closeCallbackState);
  }

  static void test_close_supports_partial_reads()
  {
    const SessionResult result =
        execute_frames(
            {
                make_close_frame(
                    close_payload(
                        1000u,
                        "partial read")),
            },
            1u);

    assert_clean_client_close(result);

    assert(result.streamReadCalls > 20u);
  }

  static void test_close_supports_partial_handshake_writes()
  {
    const SessionResult result =
        execute_frames(
            {
                make_close_frame(),
            },
            8192u,
            1u);

    assert_clean_client_close(result);

    assert(result.streamWriteCalls > 100u);
  }

  static void test_close_can_follow_handshake_in_same_read()
  {
    const SessionResult result =
        execute_frames(
            {
                make_close_frame(),
            });

    assert_clean_client_close(result);

    assert(result.streamReadCalls == 1u);
  }

  static void test_close_without_router()
  {
    const SessionResult result =
        execute_frames(
            {
                make_close_frame(),
            },
            8192u,
            8192u,
            false);

    assert(result.openCalls == 0u);
    assert(result.closeCalls == 0u);
    assert(result.errorCalls == 0u);

    assert(result.messages.empty());
    assert(result.errors.empty());

    assert(!result.finalOpenState);
    assert(!result.streamOpen);

    assert(result.streamCloseCalls == 1u);

    assert(
        result.output ==
        handshake_response());
  }

  static void test_close_after_run_is_idempotent()
  {
    const SessionResult result =
        execute_frames(
            {
                make_close_frame(),
            },
            8192u,
            8192u,
            true,
            true);

    assert_clean_client_close(result);

    assert(result.closeCalls == 1u);
    assert(result.streamCloseCalls == 1u);
  }

  static void test_shutdown_now_closes_transport()
  {
    auto context =
        std::make_shared<
            io_context>();

    auto router =
        std::make_shared<
            Router>();

    std::size_t closeCalls = 0u;
    std::size_t errorCalls = 0u;

    router->on_close(
        [&closeCalls](
            Session &)
        {
          closeCalls += 1u;
        });

    router->on_error(
        [&errorCalls](
            Session &,
            const std::string &)
        {
          errorCalls += 1u;
        });

    auto stream =
        std::make_unique<
            ScriptedTcpStream>("");

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

    assert(streamPointer->is_open());
    assert(!session->is_open());

    session->shutdown_now();

    assert(!streamPointer->is_open());
    assert(!session->is_open());

    assert(
        streamPointer->close_calls() ==
        1u);

    assert(closeCalls == 0u);
    assert(errorCalls == 0u);
  }

  static void test_shutdown_now_is_idempotent()
  {
    auto context =
        std::make_shared<
            io_context>();

    auto stream =
        std::make_unique<
            ScriptedTcpStream>("");

    ScriptedTcpStream *streamPointer =
        stream.get();

    Config config;

    auto session =
        std::make_shared<
            Session>(
            std::move(stream),
            config,
            nullptr,
            nullptr,
            context);

    session->shutdown_now();
    session->shutdown_now();
    session->shutdown_now();

    assert(!session->is_open());
    assert(!streamPointer->is_open());

    assert(
        streamPointer->close_calls() ==
        1u);
  }

} // namespace

int main()
{
  test_empty_close_frame_closes_session();

  test_close_status_and_reason_are_accepted();
  test_application_close_code_is_accepted();

  test_close_after_text_preserves_previous_message();
  test_frames_after_close_are_not_dispatched();

  test_only_first_close_frame_is_processed();
  test_close_callback_observes_closed_state();

  test_close_supports_partial_reads();
  test_close_supports_partial_handshake_writes();

  test_close_can_follow_handshake_in_same_read();
  test_close_without_router();

  test_close_after_run_is_idempotent();

  test_shutdown_now_closes_transport();
  test_shutdown_now_is_idempotent();

  return 0;
}
