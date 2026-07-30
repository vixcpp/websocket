/**
 *
 * @file long_polling_session_test.cpp
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
#include <vector>

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
      std::string type,
      std::string room = "general")
  {
    JsonMessage message;

    message.id = std::move(id);
    message.kind = "event";
    message.ts = "2026-07-30T10:00:00Z";
    message.room = std::move(room);
    message.type = std::move(type);

    return message;
  }

  static void test_type_contracts()
  {
    static_assert(
        std::is_default_constructible_v<
            LongPollingSession>);

    static_assert(
        std::is_constructible_v<
            LongPollingSession,
            std::string>);

    static_assert(
        std::is_copy_constructible_v<
            LongPollingSession>);

    static_assert(
        std::is_move_constructible_v<
            LongPollingSession>);

    static_assert(
        std::is_same_v<
            decltype(std::declval<LongPollingSession &>()
                         .touch()),
            void>);

    static_assert(
        std::is_same_v<
            decltype(std::declval<const LongPollingSession &>()
                         .is_expired(
                             std::chrono::seconds{60},
                             Clock::time_point{})),
            bool>);

    static_assert(
        std::is_same_v<
            decltype(std::declval<LongPollingSession &>()
                         .enqueue(
                             std::declval<const JsonMessage &>(),
                             std::size_t{1})),
            void>);

    static_assert(
        std::is_same_v<
            decltype(std::declval<LongPollingSession &>()
                         .drain(std::size_t{1})),
            std::vector<JsonMessage>>);
  }

  static void test_default_constructor()
  {
    LongPollingSession session;

    assert(session.id.empty());
    assert(session.buffer.empty());
    assert(session.lastSeen == Clock::time_point{});
  }

  static void test_session_id_constructor()
  {
    const auto before =
        Clock::now();

    LongPollingSession session{
        "session-42"};

    const auto after =
        Clock::now();

    assert(session.id == "session-42");
    assert(session.buffer.empty());

    assert(session.lastSeen >= before);
    assert(session.lastSeen <= after);
  }

  static void test_session_id_constructor_owns_value()
  {
    std::string id =
        "owned-session";

    LongPollingSession session{
        id};

    id = "modified";

    assert(session.id == "owned-session");
  }

  static void test_touch_updates_last_seen()
  {
    LongPollingSession session{
        "session-touch"};

    session.lastSeen =
        Clock::time_point{};

    const auto before =
        Clock::now();

    session.touch();

    const auto after =
        Clock::now();

    assert(
        session.lastSeen >
        Clock::time_point{});

    assert(session.lastSeen >= before);
    assert(session.lastSeen <= after);
  }

  static void test_recent_session_is_not_expired()
  {
    LongPollingSession session{
        "recent-session"};

    const auto now =
        Clock::now();

    session.lastSeen =
        now - std::chrono::seconds{30};

    assert(
        session.is_expired(
            std::chrono::seconds{60},
            now) == false);
  }

  static void test_old_session_is_expired()
  {
    LongPollingSession session{
        "expired-session"};

    const auto now =
        Clock::now();

    session.lastSeen =
        now - std::chrono::seconds{120};

    assert(
        session.is_expired(
            std::chrono::seconds{60},
            now) == true);
  }

  static void test_future_last_seen_is_not_expired()
  {
    LongPollingSession session{
        "future-session"};

    const auto now =
        Clock::now();

    session.lastSeen =
        now + std::chrono::seconds{10};

    assert(
        session.is_expired(
            std::chrono::seconds{60},
            now) == false);
  }

  static void test_enqueue_single_message()
  {
    LongPollingSession session{
        "session-1"};

    const JsonMessage message =
        make_message(
            "message-1",
            "chat.message");

    session.enqueue(
        message,
        16u);

    assert(session.buffer.size() == 1u);

    const JsonMessage &buffered =
        session.buffer.front();

    assert(buffered.id == "message-1");
    assert(buffered.kind == "event");
    assert(buffered.room == "general");
    assert(buffered.type == "chat.message");
  }

  static void test_enqueue_preserves_message_value()
  {
    LongPollingSession session{
        "session-copy"};

    JsonMessage message =
        make_message(
            "original-id",
            "original.type");

    session.enqueue(
        message,
        16u);

    message.id = "modified-id";
    message.type = "modified.type";

    assert(session.buffer.size() == 1u);
    assert(session.buffer.front().id == "original-id");
    assert(session.buffer.front().type == "original.type");
  }

  static void test_enqueue_updates_last_seen()
  {
    LongPollingSession session{
        "session-enqueue-touch"};

    session.lastSeen =
        Clock::time_point{};

    const auto before =
        Clock::now();

    session.enqueue(
        make_message(
            "message-1",
            "system.ready"),
        16u);

    const auto after =
        Clock::now();

    assert(session.lastSeen >= before);
    assert(session.lastSeen <= after);
  }

  static void test_drain_single_message()
  {
    LongPollingSession session{
        "session-drain"};

    session.enqueue(
        make_message(
            "message-1",
            "chat.message"),
        16u);

    const std::vector<JsonMessage> drained =
        session.drain(1u);

    assert(drained.size() == 1u);
    assert(drained[0].id == "message-1");
    assert(drained[0].type == "chat.message");

    assert(session.buffer.empty());
  }

  static void test_drain_preserves_fifo_order()
  {
    LongPollingSession session{
        "session-fifo"};

    session.enqueue(
        make_message("message-1", "first"),
        16u);

    session.enqueue(
        make_message("message-2", "second"),
        16u);

    session.enqueue(
        make_message("message-3", "third"),
        16u);

    const auto drained =
        session.drain(3u);

    assert(drained.size() == 3u);

    assert(drained[0].id == "message-1");
    assert(drained[0].type == "first");

    assert(drained[1].id == "message-2");
    assert(drained[1].type == "second");

    assert(drained[2].id == "message-3");
    assert(drained[2].type == "third");

    assert(session.buffer.empty());
  }

  static void test_partial_drain()
  {
    LongPollingSession session{
        "session-partial"};

    session.enqueue(
        make_message("message-1", "first"),
        16u);

    session.enqueue(
        make_message("message-2", "second"),
        16u);

    session.enqueue(
        make_message("message-3", "third"),
        16u);

    const auto drained =
        session.drain(2u);

    assert(drained.size() == 2u);
    assert(drained[0].id == "message-1");
    assert(drained[1].id == "message-2");

    assert(session.buffer.size() == 1u);
    assert(session.buffer.front().id == "message-3");
  }

  static void test_drain_more_than_available()
  {
    LongPollingSession session{
        "session-drain-all"};

    session.enqueue(
        make_message("message-1", "first"),
        16u);

    session.enqueue(
        make_message("message-2", "second"),
        16u);

    const auto drained =
        session.drain(100u);

    assert(drained.size() == 2u);
    assert(drained[0].id == "message-1");
    assert(drained[1].id == "message-2");

    assert(session.buffer.empty());
  }

  static void test_drain_empty_session()
  {
    LongPollingSession session{
        "session-empty"};

    const auto drained =
        session.drain(10u);

    assert(drained.empty());
    assert(session.buffer.empty());
  }

  static void test_drain_updates_last_seen()
  {
    LongPollingSession session{
        "session-drain-touch"};

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

  static void test_session_can_be_reused_after_drain()
  {
    LongPollingSession session{
        "reusable-session"};

    session.enqueue(
        make_message("message-1", "first"),
        16u);

    const auto first =
        session.drain(10u);

    assert(first.size() == 1u);
    assert(session.buffer.empty());

    session.enqueue(
        make_message("message-2", "second"),
        16u);

    const auto second =
        session.drain(10u);

    assert(second.size() == 1u);
    assert(second[0].id == "message-2");
    assert(second[0].type == "second");

    assert(session.buffer.empty());
  }

} // namespace

int main()
{
  test_type_contracts();

  test_default_constructor();
  test_session_id_constructor();
  test_session_id_constructor_owns_value();

  test_touch_updates_last_seen();

  test_recent_session_is_not_expired();
  test_old_session_is_expired();
  test_future_last_seen_is_not_expired();

  test_enqueue_single_message();
  test_enqueue_preserves_message_value();
  test_enqueue_updates_last_seen();

  test_drain_single_message();
  test_drain_preserves_fifo_order();
  test_partial_drain();
  test_drain_more_than_available();
  test_drain_empty_session();
  test_drain_updates_last_seen();

  test_session_can_be_reused_after_drain();

  return 0;
}
