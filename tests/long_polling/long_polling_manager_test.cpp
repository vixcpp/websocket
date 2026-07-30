/**
 *
 * @file long_polling_manager_test.cpp
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
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include <vix/websocket/LongPolling.hpp>

namespace
{
  using JsonMessage =
      vix::websocket::JsonMessage;

  using LongPollingManager =
      vix::websocket::LongPollingManager;

  static JsonMessage make_message(
      std::string id,
      std::string type,
      std::string room = "general")
  {
    JsonMessage message;

    message.id = std::move(id);
    message.kind = "event";
    message.room = std::move(room);
    message.type = std::move(type);

    return message;
  }

  static JsonMessage make_indexed_message(
      std::size_t index)
  {
    return make_message(
        "message-" + std::to_string(index),
        "event-" + std::to_string(index));
  }

  static void test_manager_type_contracts()
  {
    static_assert(
        std::is_same_v<
            LongPollingManager::SessionId,
            std::string>);

    static_assert(
        std::is_default_constructible_v<
            LongPollingManager>);

    static_assert(
        !std::is_copy_constructible_v<
            LongPollingManager>);

    static_assert(
        !std::is_copy_assignable_v<
            LongPollingManager>);

    static_assert(
        std::is_nothrow_move_constructible_v<
            LongPollingManager>);

    static_assert(
        std::is_nothrow_move_assignable_v<
            LongPollingManager>);

    static_assert(
        std::is_same_v<
            decltype(std::declval<LongPollingManager &>()
                         .push_to(
                             std::declval<const std::string &>(),
                             std::declval<const JsonMessage &>())),
            void>);

    static_assert(
        std::is_same_v<
            decltype(std::declval<LongPollingManager &>()
                         .poll(
                             std::declval<const std::string &>(),
                             std::size_t{50},
                             true)),
            std::vector<JsonMessage>>);

    static_assert(
        std::is_same_v<
            decltype(std::declval<LongPollingManager &>()
                         .sweep_expired()),
            void>);

    static_assert(
        std::is_same_v<
            decltype(std::declval<const LongPollingManager &>()
                         .session_count()),
            std::size_t>);

    static_assert(
        std::is_same_v<
            decltype(std::declval<const LongPollingManager &>()
                         .buffer_size(
                             std::declval<const std::string &>())),
            std::size_t>);
  }

  static void test_new_manager_is_empty()
  {
    LongPollingManager manager;

    assert(manager.session_count() == 0u);
    assert(manager.buffer_size("missing") == 0u);
  }

  static void test_push_creates_session()
  {
    LongPollingManager manager;

    manager.push_to(
        "session-1",
        make_message(
            "message-1",
            "chat.message"));

    assert(manager.session_count() == 1u);
    assert(manager.buffer_size("session-1") == 1u);
  }

  static void test_repeated_push_uses_same_session()
  {
    LongPollingManager manager;

    manager.push_to(
        "session-1",
        make_indexed_message(1u));

    manager.push_to(
        "session-1",
        make_indexed_message(2u));

    manager.push_to(
        "session-1",
        make_indexed_message(3u));

    assert(manager.session_count() == 1u);
    assert(manager.buffer_size("session-1") == 3u);
  }

  static void test_push_creates_independent_sessions()
  {
    LongPollingManager manager;

    manager.push_to(
        "session-a",
        make_message(
            "message-a",
            "event.a"));

    manager.push_to(
        "session-b",
        make_message(
            "message-b",
            "event.b"));

    assert(manager.session_count() == 2u);

    assert(manager.buffer_size("session-a") == 1u);
    assert(manager.buffer_size("session-b") == 1u);
  }

  static void test_poll_returns_message()
  {
    LongPollingManager manager;

    manager.push_to(
        "session-1",
        make_message(
            "message-1",
            "chat.message"));

    const auto messages =
        manager.poll(
            "session-1",
            10u,
            false);

    assert(messages.size() == 1u);

    assert(messages[0].id == "message-1");
    assert(messages[0].type == "chat.message");

    assert(manager.buffer_size("session-1") == 0u);
    assert(manager.session_count() == 1u);
  }

  static void test_poll_preserves_fifo_order()
  {
    LongPollingManager manager;

    manager.push_to(
        "session-1",
        make_indexed_message(1u));

    manager.push_to(
        "session-1",
        make_indexed_message(2u));

    manager.push_to(
        "session-1",
        make_indexed_message(3u));

    const auto messages =
        manager.poll(
            "session-1",
            3u,
            false);

    assert(messages.size() == 3u);

    assert(messages[0].id == "message-1");
    assert(messages[1].id == "message-2");
    assert(messages[2].id == "message-3");

    assert(manager.buffer_size("session-1") == 0u);
  }

  static void test_partial_poll_leaves_remaining_messages()
  {
    LongPollingManager manager;

    for (std::size_t index = 1u;
         index <= 5u;
         ++index)
    {
      manager.push_to(
          "session-1",
          make_indexed_message(index));
    }

    const auto first =
        manager.poll(
            "session-1",
            2u,
            false);

    assert(first.size() == 2u);
    assert(first[0].id == "message-1");
    assert(first[1].id == "message-2");

    assert(manager.buffer_size("session-1") == 3u);

    const auto second =
        manager.poll(
            "session-1",
            10u,
            false);

    assert(second.size() == 3u);

    assert(second[0].id == "message-3");
    assert(second[1].id == "message-4");
    assert(second[2].id == "message-5");

    assert(manager.buffer_size("session-1") == 0u);
  }

  static void test_poll_missing_without_creation()
  {
    LongPollingManager manager;

    const auto messages =
        manager.poll(
            "missing-session",
            50u,
            false);

    assert(messages.empty());
    assert(manager.session_count() == 0u);
  }

  static void test_poll_missing_with_creation()
  {
    LongPollingManager manager;

    const auto messages =
        manager.poll(
            "new-session",
            50u,
            true);

    assert(messages.empty());

    assert(manager.session_count() == 1u);
    assert(manager.buffer_size("new-session") == 0u);
  }

  static void test_poll_defaults_create_missing_session()
  {
    LongPollingManager manager;

    const auto messages =
        manager.poll("default-session");

    assert(messages.empty());
    assert(manager.session_count() == 1u);
  }

  static void test_default_poll_limit_is_fifty()
  {
    LongPollingManager manager;

    for (std::size_t index = 1u;
         index <= 60u;
         ++index)
    {
      manager.push_to(
          "session-1",
          make_indexed_message(index));
    }

    const auto first =
        manager.poll("session-1");

    assert(first.size() == 50u);

    assert(first.front().id == "message-1");
    assert(first.back().id == "message-50");

    assert(manager.buffer_size("session-1") == 10u);

    const auto second =
        manager.poll("session-1");

    assert(second.size() == 10u);

    assert(second.front().id == "message-51");
    assert(second.back().id == "message-60");
  }

  static void test_zero_message_poll_does_not_drain()
  {
    LongPollingManager manager;

    manager.push_to(
        "session-1",
        make_indexed_message(1u));

    manager.push_to(
        "session-1",
        make_indexed_message(2u));

    const auto messages =
        manager.poll(
            "session-1",
            0u,
            false);

    assert(messages.empty());
    assert(manager.buffer_size("session-1") == 2u);
  }

  static void test_custom_buffer_limit_drops_oldest()
  {
    LongPollingManager manager{
        std::chrono::seconds{60},
        3u};

    for (std::size_t index = 1u;
         index <= 5u;
         ++index)
    {
      manager.push_to(
          "session-1",
          make_indexed_message(index));
    }

    assert(manager.buffer_size("session-1") == 3u);

    const auto messages =
        manager.poll(
            "session-1",
            10u,
            false);

    assert(messages.size() == 3u);

    assert(messages[0].id == "message-3");
    assert(messages[1].id == "message-4");
    assert(messages[2].id == "message-5");
  }

  static void test_default_buffer_limit_is_256()
  {
    LongPollingManager manager;

    for (std::size_t index = 1u;
         index <= 300u;
         ++index)
    {
      manager.push_to(
          "session-1",
          make_indexed_message(index));
    }

    assert(manager.buffer_size("session-1") == 256u);

    const auto messages =
        manager.poll(
            "session-1",
            300u,
            false);

    assert(messages.size() == 256u);

    assert(messages.front().id == "message-45");
    assert(messages.back().id == "message-300");
  }

  static void test_empty_session_id_is_supported()
  {
    LongPollingManager manager;

    manager.push_to(
        "",
        make_message(
            "message-1",
            "empty.session"));

    assert(manager.session_count() == 1u);
    assert(manager.buffer_size("") == 1u);

    const auto messages =
        manager.poll(
            "",
            10u,
            false);

    assert(messages.size() == 1u);
    assert(messages[0].type == "empty.session");
  }

  static void test_sweep_keeps_active_sessions()
  {
    LongPollingManager manager{
        std::chrono::seconds{
            std::chrono::hours{24}},
        16u};

    manager.push_to(
        "session-1",
        make_indexed_message(1u));

    manager.push_to(
        "session-2",
        make_indexed_message(2u));

    manager.sweep_expired();

    assert(manager.session_count() == 2u);
    assert(manager.buffer_size("session-1") == 1u);
    assert(manager.buffer_size("session-2") == 1u);
  }

  static void test_sweep_removes_expired_sessions()
  {
    LongPollingManager manager{
        std::chrono::seconds{-1},
        16u};

    manager.push_to(
        "session-1",
        make_indexed_message(1u));

    manager.push_to(
        "session-2",
        make_indexed_message(2u));

    assert(manager.session_count() == 2u);

    manager.sweep_expired();

    assert(manager.session_count() == 0u);

    assert(manager.buffer_size("session-1") == 0u);
    assert(manager.buffer_size("session-2") == 0u);
  }

  static void test_push_after_sweep_recreates_session()
  {
    LongPollingManager manager{
        std::chrono::seconds{-1},
        16u};

    manager.push_to(
        "session-1",
        make_indexed_message(1u));

    manager.sweep_expired();

    assert(manager.session_count() == 0u);

    manager.push_to(
        "session-1",
        make_indexed_message(2u));

    assert(manager.session_count() == 1u);
    assert(manager.buffer_size("session-1") == 1u);

    const auto messages =
        manager.poll(
            "session-1",
            10u,
            false);

    assert(messages.size() == 1u);
    assert(messages[0].id == "message-2");
  }

  static void test_move_constructor_preserves_sessions()
  {
    LongPollingManager source{
        std::chrono::seconds{60},
        3u};

    source.push_to(
        "session-1",
        make_indexed_message(1u));

    source.push_to(
        "session-1",
        make_indexed_message(2u));

    LongPollingManager destination{
        std::move(source)};

    assert(destination.session_count() == 1u);
    assert(destination.buffer_size("session-1") == 2u);

    const auto messages =
        destination.poll(
            "session-1",
            10u,
            false);

    assert(messages.size() == 2u);
    assert(messages[0].id == "message-1");
    assert(messages[1].id == "message-2");
  }

  static void test_move_assignment_preserves_sessions()
  {
    LongPollingManager source{
        std::chrono::seconds{60},
        4u};

    source.push_to(
        "source-session",
        make_indexed_message(1u));

    source.push_to(
        "source-session",
        make_indexed_message(2u));

    LongPollingManager destination;

    destination.push_to(
        "old-session",
        make_indexed_message(99u));

    destination =
        std::move(source);

    assert(destination.session_count() == 1u);

    assert(
        destination.buffer_size(
            "source-session") == 2u);

    assert(
        destination.buffer_size(
            "old-session") == 0u);
  }

  static void test_concurrent_pushes_are_thread_safe()
  {
    constexpr std::size_t workerCount = 4u;
    constexpr std::size_t messagesPerWorker = 100u;
    constexpr std::size_t totalMessages =
        workerCount * messagesPerWorker;

    LongPollingManager manager{
        std::chrono::seconds{60},
        totalMessages};

    std::vector<std::thread> workers;
    workers.reserve(workerCount);

    for (std::size_t worker = 0u;
         worker < workerCount;
         ++worker)
    {
      workers.emplace_back(
          [&manager, worker]()
          {
            for (std::size_t index = 0u;
                 index < messagesPerWorker;
                 ++index)
            {
              const std::string id =
                  "worker-" +
                  std::to_string(worker) +
                  "-message-" +
                  std::to_string(index);

              manager.push_to(
                  "shared-session",
                  make_message(
                      id,
                      "concurrent.message"));
            }
          });
    }

    for (std::thread &worker : workers)
    {
      worker.join();
    }

    assert(manager.session_count() == 1u);

    assert(
        manager.buffer_size(
            "shared-session") ==
        totalMessages);

    const auto messages =
        manager.poll(
            "shared-session",
            totalMessages,
            false);

    assert(messages.size() == totalMessages);

    std::unordered_set<std::string> identifiers;
    identifiers.reserve(totalMessages);

    for (const JsonMessage &message : messages)
    {
      assert(message.type == "concurrent.message");

      const auto [_, inserted] =
          identifiers.insert(message.id);

      assert(inserted == true);
    }

    assert(identifiers.size() == totalMessages);
    assert(manager.buffer_size("shared-session") == 0u);
  }

} // namespace

int main()
{
  test_manager_type_contracts();

  test_new_manager_is_empty();

  test_push_creates_session();
  test_repeated_push_uses_same_session();
  test_push_creates_independent_sessions();

  test_poll_returns_message();
  test_poll_preserves_fifo_order();
  test_partial_poll_leaves_remaining_messages();

  test_poll_missing_without_creation();
  test_poll_missing_with_creation();
  test_poll_defaults_create_missing_session();

  test_default_poll_limit_is_fifty();
  test_zero_message_poll_does_not_drain();

  test_custom_buffer_limit_drops_oldest();
  test_default_buffer_limit_is_256();

  test_empty_session_id_is_supported();

  test_sweep_keeps_active_sessions();
  test_sweep_removes_expired_sessions();
  test_push_after_sweep_recreates_session();

  test_move_constructor_preserves_sessions();
  test_move_assignment_preserves_sessions();

  test_concurrent_pushes_are_thread_safe();

  return 0;
}
