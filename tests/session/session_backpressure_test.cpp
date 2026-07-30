/**
 *
 * @file session_backpressure_test.cpp
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
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <vix/async/core/cancel.hpp>
#include <vix/async/core/io_context.hpp>
#include <vix/async/core/task.hpp>
#include <vix/async/net/tcp.hpp>
#include <vix/executor/RuntimeExecutor.hpp>
#include <vix/utils/Logger.hpp>
#include <vix/websocket/config.hpp>
#include <vix/websocket/protocol.hpp>
#include <vix/websocket/router.hpp>

#define private public
#include <vix/websocket/session.hpp>
#undef private

namespace
{
  using Config =
      vix::websocket::Config;

  using Router =
      vix::websocket::Router;

  using Session =
      vix::websocket::Session;

  using io_context =
      vix::async::core::io_context;

  using tcp_stream =
      vix::async::net::tcp_stream;

  class BackpressureFixture
  {
  public:
    BackpressureFixture()
        : context{
              std::make_shared<
                  io_context>()},
          router{
              std::make_shared<
                  Router>()}
    {
      router->on_close(
          [this](
              Session &)
          {
            closeCalls += 1u;
          });

      router->on_error(
          [this](
              Session &,
              const std::string &error)
          {
            errors.push_back(
                error);
          });

      session =
          std::make_shared<
              Session>(
              std::unique_ptr<
                  tcp_stream>{},
              config,
              router,
              nullptr,
              context);

      /*
       * Disable asynchronous flushing so the private queue can be
       * exercised deterministically without transport timing.
       */
      session->ioc_.reset();
    }

    [[nodiscard]]
    std::size_t queue_size() const
    {
      return session->writeQueue_.size();
    }

    [[nodiscard]]
    std::size_t queued_bytes() const
    {
      return session->queuedWriteBytes_;
    }

    [[nodiscard]]
    bool closing() const
    {
      return session->closing_.load(
          std::memory_order_acquire);
    }

    void enqueue_text(
        std::string payload)
    {
      session->do_enqueue_message(
          false,
          std::move(payload));
    }

    void enqueue_binary(
        std::string payload)
    {
      session->do_enqueue_message(
          true,
          std::move(payload));
    }

    Config config{};

    std::shared_ptr<io_context>
        context{};

    std::shared_ptr<Router>
        router{};

    std::shared_ptr<Session>
        session{};

    std::size_t closeCalls{0u};

    std::vector<std::string>
        errors{};
  };

  static void test_backpressure_constants()
  {
    static_assert(
        Session::
            MAX_PENDING_WRITE_MESSAGES ==
        1024u);

    static_assert(
        Session::
            MAX_PENDING_WRITE_BYTES ==
        4u * 1024u * 1024u);

    static_assert(
        std::is_same_v<
            decltype(Session::
                         MAX_PENDING_WRITE_MESSAGES),
            const std::size_t>);

    static_assert(
        std::is_same_v<
            decltype(Session::
                         MAX_PENDING_WRITE_BYTES),
            const std::size_t>);
  }

  static void test_new_session_has_empty_write_queue()
  {
    BackpressureFixture fixture;

    assert(fixture.queue_size() == 0u);
    assert(fixture.queued_bytes() == 0u);

    assert(!fixture.closing());

    assert(
        fixture.session->writeInProgress_ ==
        false);

    assert(fixture.closeCalls == 0u);
    assert(fixture.errors.empty());
  }

  static void test_exact_message_limit_is_accepted()
  {
    BackpressureFixture fixture;

    constexpr std::size_t limit =
        Session::
            MAX_PENDING_WRITE_MESSAGES;

    for (std::size_t index = 0u;
         index < limit;
         ++index)
    {
      fixture.enqueue_text("");
    }

    assert(
        fixture.queue_size() ==
        limit);

    assert(
        fixture.queued_bytes() ==
        0u);

    assert(!fixture.closing());

    assert(
        fixture.session->writeInProgress_ ==
        false);

    assert(fixture.closeCalls == 0u);
    assert(fixture.errors.empty());
  }

  static void test_message_limit_overflow_closes_session()
  {
    BackpressureFixture fixture;

    constexpr std::size_t limit =
        Session::
            MAX_PENDING_WRITE_MESSAGES;

    for (std::size_t index = 0u;
         index < limit;
         ++index)
    {
      fixture.enqueue_text("");
    }

    fixture.enqueue_text("");

    assert(fixture.closing());

    assert(
        fixture.queue_size() ==
        limit);

    assert(
        fixture.queued_bytes() ==
        0u);

    assert(
        fixture.session->writeInProgress_ ==
        false);

    /*
     * Backpressure is a close condition rather than a router error.
     * The fixture has no active io_context inside the session, so the
     * asynchronous close callback is intentionally not scheduled.
     */
    assert(fixture.closeCalls == 0u);
    assert(fixture.errors.empty());
  }

  static void test_message_overflow_does_not_enqueue_extra_item()
  {
    BackpressureFixture fixture;

    constexpr std::size_t limit =
        Session::
            MAX_PENDING_WRITE_MESSAGES;

    for (std::size_t index = 0u;
         index < limit;
         ++index)
    {
      fixture.enqueue_text(
          "message-" +
          std::to_string(index));
    }

    const std::string lastAccepted =
        fixture.session->writeQueue_.back().data;

    fixture.enqueue_text(
        "overflow-message");

    assert(fixture.closing());

    assert(
        fixture.queue_size() ==
        limit);

    assert(
        fixture.session->writeQueue_.back().data ==
        lastAccepted);

    for (const auto &pending :
         fixture.session->writeQueue_)
    {
      assert(
          pending.data !=
          "overflow-message");
    }
  }

  static void test_exact_byte_limit_is_accepted()
  {
    BackpressureFixture fixture;

    constexpr std::size_t limit =
        Session::
            MAX_PENDING_WRITE_BYTES;

    fixture.enqueue_text(
        std::string(
            limit,
            'x'));

    assert(!fixture.closing());

    assert(fixture.queue_size() == 1u);

    assert(
        fixture.queued_bytes() ==
        limit);

    assert(
        fixture.session->writeQueue_.front().data.size() ==
        limit);

    assert(
        fixture.session->writeQueue_.front().isBinary ==
        false);

    assert(fixture.errors.empty());
  }

  static void test_single_text_payload_over_byte_limit_closes()
  {
    BackpressureFixture fixture;

    constexpr std::size_t limit =
        Session::
            MAX_PENDING_WRITE_BYTES;

    fixture.enqueue_text(
        std::string(
            limit + 1u,
            'x'));

    assert(fixture.closing());

    assert(fixture.queue_size() == 0u);
    assert(fixture.queued_bytes() == 0u);

    assert(fixture.closeCalls == 0u);
    assert(fixture.errors.empty());
  }

  static void test_single_binary_payload_over_byte_limit_closes()
  {
    BackpressureFixture fixture;

    constexpr std::size_t limit =
        Session::
            MAX_PENDING_WRITE_BYTES;

    fixture.enqueue_binary(
        std::string(
            limit + 1u,
            '\x7F'));

    assert(fixture.closing());

    assert(fixture.queue_size() == 0u);
    assert(fixture.queued_bytes() == 0u);

    assert(fixture.errors.empty());
  }

  static void test_cumulative_byte_limit_is_enforced()
  {
    BackpressureFixture fixture;

    constexpr std::size_t limit =
        Session::
            MAX_PENDING_WRITE_BYTES;

    constexpr std::size_t firstSize =
        1024u * 1024u;

    constexpr std::size_t secondSize =
        limit -
        firstSize;

    fixture.enqueue_text(
        std::string(
            firstSize,
            'a'));

    fixture.enqueue_binary(
        std::string(
            secondSize,
            'b'));

    assert(!fixture.closing());

    assert(fixture.queue_size() == 2u);

    assert(
        fixture.queued_bytes() ==
        limit);

    fixture.enqueue_text(
        "x");

    assert(fixture.closing());

    assert(fixture.queue_size() == 2u);

    assert(
        fixture.queued_bytes() ==
        limit);

    assert(
        fixture.session->writeQueue_[0].data.size() ==
        firstSize);

    assert(
        fixture.session->writeQueue_[1].data.size() ==
        secondSize);
  }

  static void test_mixed_payloads_share_byte_budget()
  {
    BackpressureFixture fixture;

    constexpr std::size_t limit =
        Session::
            MAX_PENDING_WRITE_BYTES;

    const std::size_t quarter =
        limit / 4u;

    fixture.enqueue_text(
        std::string(
            quarter,
            'a'));

    fixture.enqueue_binary(
        std::string(
            quarter,
            'b'));

    fixture.enqueue_text(
        std::string(
            quarter,
            'c'));

    fixture.enqueue_binary(
        std::string(
            quarter,
            'd'));

    assert(!fixture.closing());

    assert(fixture.queue_size() == 4u);

    assert(
        fixture.queued_bytes() ==
        limit);

    assert(
        fixture.session->writeQueue_[0].isBinary ==
        false);

    assert(
        fixture.session->writeQueue_[1].isBinary ==
        true);

    assert(
        fixture.session->writeQueue_[2].isBinary ==
        false);

    assert(
        fixture.session->writeQueue_[3].isBinary ==
        true);

    fixture.enqueue_binary(
        std::string(
            1u,
            'e'));

    assert(fixture.closing());

    assert(fixture.queue_size() == 4u);

    assert(
        fixture.queued_bytes() ==
        limit);
  }

  static void test_empty_messages_consume_message_budget_only()
  {
    BackpressureFixture fixture;

    constexpr std::size_t count = 500u;

    for (std::size_t index = 0u;
         index < count;
         ++index)
    {
      if (index % 2u == 0u)
      {
        fixture.enqueue_text("");
      }
      else
      {
        fixture.enqueue_binary("");
      }
    }

    assert(!fixture.closing());

    assert(
        fixture.queue_size() ==
        count);

    assert(
        fixture.queued_bytes() ==
        0u);
  }

  static void test_closing_session_rejects_follow_up_messages()
  {
    BackpressureFixture fixture;

    constexpr std::size_t limit =
        Session::
            MAX_PENDING_WRITE_BYTES;

    fixture.enqueue_text(
        std::string(
            limit + 1u,
            'x'));

    assert(fixture.closing());

    const std::size_t queueSize =
        fixture.queue_size();

    const std::size_t queuedBytes =
        fixture.queued_bytes();

    fixture.enqueue_text(
        "ignored text");

    fixture.enqueue_binary(
        "ignored binary");

    assert(
        fixture.queue_size() ==
        queueSize);

    assert(
        fixture.queued_bytes() ==
        queuedBytes);

    assert(fixture.errors.empty());
  }

  static void test_manual_close_prevents_enqueue()
  {
    BackpressureFixture fixture;

    fixture.session->close(
        "manual close");

    assert(fixture.closing());

    fixture.enqueue_text(
        "ignored");

    fixture.enqueue_binary(
        "ignored");

    assert(fixture.queue_size() == 0u);
    assert(fixture.queued_bytes() == 0u);

    assert(fixture.errors.empty());
  }

  static void test_disabled_flusher_does_not_start_write()
  {
    BackpressureFixture fixture;

    fixture.enqueue_text(
        "queued");

    fixture.enqueue_binary(
        "binary");

    assert(fixture.queue_size() == 2u);

    assert(
        fixture.queued_bytes() ==
        std::string_view{
            "queued"}
                .size() +
            std::string_view{
                "binary"}
                .size());

    assert(
        fixture.session->writeInProgress_ ==
        false);

    assert(
        fixture.session->ioc_ ==
        nullptr);
  }

} // namespace

int main()
{
  test_backpressure_constants();

  test_new_session_has_empty_write_queue();

  test_exact_message_limit_is_accepted();
  test_message_limit_overflow_closes_session();
  test_message_overflow_does_not_enqueue_extra_item();

  test_exact_byte_limit_is_accepted();

  test_single_text_payload_over_byte_limit_closes();
  test_single_binary_payload_over_byte_limit_closes();

  test_cumulative_byte_limit_is_enforced();
  test_mixed_payloads_share_byte_budget();

  test_empty_messages_consume_message_budget_only();

  test_closing_session_rejects_follow_up_messages();
  test_manual_close_prevents_enqueue();

  test_disabled_flusher_does_not_start_write();

  return 0;
}
