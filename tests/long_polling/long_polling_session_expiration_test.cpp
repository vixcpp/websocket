/**
 *
 * @file long_polling_session_expiration_test.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira. All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license that can be found in the License file.
 *
 * Vix.cpp
 *
 */

#include <cassert>
#include <chrono>
#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>

#include <vix/websocket/LongPolling.hpp>

namespace
{
  using JsonMessage =
      vix::websocket::JsonMessage;

  using LongPollingSession =
      vix::websocket::LongPollingSession;

  using Clock =
      std::chrono::steady_clock;

  static JsonMessage make_message(
      std::string id,
      std::string type)
  {
    JsonMessage message;

    message.id = std::move(id);
    message.kind = "event";
    message.room = "general";
    message.type = std::move(type);

    return message;
  }

  static void test_expiration_contract()
  {
    static_assert(
        noexcept(
            std::declval<LongPollingSession &>()
                .touch()));

    static_assert(
        noexcept(
            std::declval<const LongPollingSession &>()
                .is_expired(
                    std::chrono::seconds{60},
                    Clock::time_point{})));

    static_assert(
        std::is_same_v<
            decltype(std::declval<const LongPollingSession &>()
                         .is_expired(
                             std::chrono::seconds{60},
                             Clock::time_point{})),
            bool>);
  }

  static void test_default_session_uses_default_time_point()
  {
    LongPollingSession session;

    assert(
        session.lastSeen ==
        Clock::time_point{});
  }

  static void test_identified_session_starts_fresh()
  {
    const auto before =
        Clock::now();

    LongPollingSession session{
        "session-1"};

    const auto after =
        Clock::now();

    assert(session.lastSeen >= before);
    assert(session.lastSeen <= after);
  }

  static void test_session_younger_than_ttl_is_not_expired()
  {
    const Clock::time_point now{
        std::chrono::seconds{1000}};

    LongPollingSession session{
        "young-session"};

    session.lastSeen =
        now - std::chrono::seconds{59};

    assert(
        session.is_expired(
            std::chrono::seconds{60},
            now) == false);
  }

  static void test_exact_ttl_boundary_is_not_expired()
  {
    const Clock::time_point now{
        std::chrono::seconds{1000}};

    LongPollingSession session{
        "boundary-session"};

    session.lastSeen =
        now - std::chrono::seconds{60};

    assert(
        session.is_expired(
            std::chrono::seconds{60},
            now) == false);
  }

  static void test_one_tick_past_ttl_is_expired()
  {
    const Clock::time_point now{
        std::chrono::seconds{1000}};

    LongPollingSession session{
        "expired-session"};

    session.lastSeen =
        now -
        std::chrono::seconds{60} -
        Clock::duration{1};

    assert(
        session.is_expired(
            std::chrono::seconds{60},
            now) == true);
  }

  static void test_session_older_than_ttl_is_expired()
  {
    const Clock::time_point now{
        std::chrono::seconds{1000}};

    LongPollingSession session{
        "old-session"};

    session.lastSeen =
        now - std::chrono::seconds{120};

    assert(
        session.is_expired(
            std::chrono::seconds{60},
            now) == true);
  }

  static void test_zero_ttl_at_same_time_is_not_expired()
  {
    const Clock::time_point now{
        std::chrono::seconds{1000}};

    LongPollingSession session{
        "zero-ttl-session"};

    session.lastSeen = now;

    assert(
        session.is_expired(
            std::chrono::seconds{0},
            now) == false);
  }

  static void test_zero_ttl_one_tick_later_is_expired()
  {
    const Clock::time_point now{
        std::chrono::seconds{1000}};

    LongPollingSession session{
        "zero-ttl-expired"};

    session.lastSeen =
        now - Clock::duration{1};

    assert(
        session.is_expired(
            std::chrono::seconds{0},
            now) == true);
  }

  static void test_negative_ttl_expires_current_session()
  {
    const Clock::time_point now{
        std::chrono::seconds{1000}};

    LongPollingSession session{
        "negative-ttl-session"};

    session.lastSeen = now;

    assert(
        session.is_expired(
            std::chrono::seconds{-1},
            now) == true);
  }

  static void test_future_last_seen_is_not_expired()
  {
    const Clock::time_point now{
        std::chrono::seconds{1000}};

    LongPollingSession session{
        "future-session"};

    session.lastSeen =
        now + std::chrono::seconds{30};

    assert(
        session.is_expired(
            std::chrono::seconds{60},
            now) == false);
  }

  static void test_large_ttl_keeps_session_active()
  {
    const Clock::time_point now{
        std::chrono::hours{24}};

    LongPollingSession session{
        "large-ttl-session"};

    session.lastSeen =
        Clock::time_point{};

    assert(
        session.is_expired(
            std::chrono::seconds{
                std::chrono::hours{48}},
            now) == false);
  }

  static void test_touch_refreshes_expired_session()
  {
    LongPollingSession session{
        "touch-session"};

    session.lastSeen =
        Clock::time_point{};

    assert(
        session.is_expired(
            std::chrono::seconds{60},
            Clock::now()) == true);

    const auto before =
        Clock::now();

    session.touch();

    const auto after =
        Clock::now();

    assert(session.lastSeen >= before);
    assert(session.lastSeen <= after);

    assert(
        session.is_expired(
            std::chrono::seconds{60},
            Clock::now()) == false);
  }

  static void test_enqueue_refreshes_last_seen()
  {
    LongPollingSession session{
        "enqueue-session"};

    session.lastSeen =
        Clock::time_point{};

    const auto before =
        Clock::now();

    session.enqueue(
        make_message(
            "message-1",
            "chat.message"),
        16u);

    const auto after =
        Clock::now();

    assert(session.lastSeen >= before);
    assert(session.lastSeen <= after);

    assert(
        session.is_expired(
            std::chrono::seconds{60},
            Clock::now()) == false);
  }

  static void test_non_empty_drain_refreshes_last_seen()
  {
    LongPollingSession session{
        "drain-session"};

    session.enqueue(
        make_message(
            "message-1",
            "chat.message"),
        16u);

    session.lastSeen =
        Clock::time_point{};

    const auto before =
        Clock::now();

    const auto drained =
        session.drain(1u);

    const auto after =
        Clock::now();

    assert(drained.size() == 1u);

    assert(session.lastSeen >= before);
    assert(session.lastSeen <= after);
  }

  static void test_empty_drain_does_not_refresh_last_seen()
  {
    LongPollingSession session{
        "empty-drain-session"};

    const Clock::time_point original{
        std::chrono::seconds{42}};

    session.lastSeen = original;

    const auto drained =
        session.drain(10u);

    assert(drained.empty());
    assert(session.lastSeen == original);
  }

  static void test_zero_count_drain_does_not_refresh_last_seen()
  {
    LongPollingSession session{
        "zero-drain-session"};

    session.enqueue(
        make_message(
            "message-1",
            "chat.message"),
        16u);

    const Clock::time_point original{
        std::chrono::seconds{42}};

    session.lastSeen = original;

    const auto drained =
        session.drain(0u);

    assert(drained.empty());
    assert(session.buffer.size() == 1u);
    assert(session.lastSeen == original);
  }

  static void test_expiration_check_does_not_modify_session()
  {
    const Clock::time_point now{
        std::chrono::seconds{1000}};

    LongPollingSession session{
        "immutable-session"};

    session.lastSeen =
        now - std::chrono::seconds{120};

    session.buffer.push_back(
        make_message(
            "message-1",
            "chat.message"));

    const std::string originalId =
        session.id;

    const Clock::time_point originalLastSeen =
        session.lastSeen;

    const std::size_t originalBufferSize =
        session.buffer.size();

    const bool expired =
        session.is_expired(
            std::chrono::seconds{60},
            now);

    assert(expired == true);

    assert(session.id == originalId);
    assert(session.lastSeen == originalLastSeen);
    assert(session.buffer.size() == originalBufferSize);
  }

} // namespace

int main()
{
  test_expiration_contract();

  test_default_session_uses_default_time_point();
  test_identified_session_starts_fresh();

  test_session_younger_than_ttl_is_not_expired();
  test_exact_ttl_boundary_is_not_expired();
  test_one_tick_past_ttl_is_expired();
  test_session_older_than_ttl_is_expired();

  test_zero_ttl_at_same_time_is_not_expired();
  test_zero_ttl_one_tick_later_is_expired();
  test_negative_ttl_expires_current_session();

  test_future_last_seen_is_not_expired();
  test_large_ttl_keeps_session_active();

  test_touch_refreshes_expired_session();
  test_enqueue_refreshes_last_seen();
  test_non_empty_drain_refreshes_last_seen();

  test_empty_drain_does_not_refresh_last_seen();
  test_zero_count_drain_does_not_refresh_last_seen();

  test_expiration_check_does_not_modify_session();

  return 0;
}
