/**
 *
 * @file sqlite_message_store_replay_test.cpp
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
#include <filesystem>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <vix/websocket/SqliteMessageStore.hpp>
#include <vix/websocket/protocol.hpp>

namespace
{
  using JsonMessage =
      vix::websocket::JsonMessage;

  using SqliteMessageStore =
      vix::websocket::SqliteMessageStore;

  class TemporaryDatabase
  {
  public:
    explicit TemporaryDatabase(
        std::string testName)
    {
      static std::atomic<std::size_t>
          counter{0u};

      const auto timestamp =
          std::chrono::steady_clock::now()
              .time_since_epoch()
              .count();

      const std::size_t sequence =
          counter.fetch_add(
              1u,
              std::memory_order_relaxed);

      directory_ =
          std::filesystem::temp_directory_path() /
          (std::move(testName) +
           "_" +
           std::to_string(timestamp) +
           "_" +
           std::to_string(sequence));

      std::error_code error;

      std::filesystem::remove_all(
          directory_,
          error);

      error.clear();

      const bool created =
          std::filesystem::create_directories(
              directory_,
              error);

      assert(created);
      assert(!error);

      path_ =
          directory_ /
          "messages.sqlite3";
    }

    TemporaryDatabase(
        const TemporaryDatabase &) = delete;

    TemporaryDatabase &operator=(
        const TemporaryDatabase &) = delete;

    ~TemporaryDatabase()
    {
      std::error_code error;

      std::filesystem::remove_all(
          directory_,
          error);
    }

    [[nodiscard]]
    std::string path_string() const
    {
      return path_.string();
    }

  private:
    std::filesystem::path directory_{};
    std::filesystem::path path_{};
  };

  static JsonMessage make_message(
      std::size_t index,
      std::string room)
  {
    JsonMessage message;

    message.id =
        "message-" +
        std::to_string(1000u + index);

    message.kind = "event";

    message.ts =
        "2026-07-30T11:" +
        std::to_string(10u + index) +
        ":00Z";

    message.room =
        std::move(room);

    message.type =
        "event." +
        std::to_string(index);

    return message;
  }

  static void append_message(
      SqliteMessageStore &store,
      std::size_t index,
      const std::string &room)
  {
    store.append(
        make_message(
            index,
            room));
  }

  static void append_range(
      SqliteMessageStore &store,
      std::size_t first,
      std::size_t last)
  {
    for (std::size_t index = first;
         index <= last;
         ++index)
    {
      append_message(
          store,
          index,
          index % 2u == 0u
              ? "support"
              : "general");
    }
  }

  static void test_replay_api_contract()
  {
    static_assert(
        std::is_same_v<
            decltype(std::declval<
                         SqliteMessageStore &>()
                         .replay_from(
                             std::declval<
                                 const std::string &>(),
                             std::size_t{10})),
            std::vector<JsonMessage>>);
  }

  static void test_empty_store_replay_returns_empty()
  {
    TemporaryDatabase database{
        "vix_sqlite_replay_empty"};

    SqliteMessageStore store{
        database.path_string()};

    const auto messages =
        store.replay_from(
            "message-1001",
            10u);

    assert(messages.empty());
  }

  static void test_replay_starts_after_cursor()
  {
    TemporaryDatabase database{
        "vix_sqlite_replay_after_cursor"};

    SqliteMessageStore store{
        database.path_string()};

    append_range(
        store,
        1u,
        5u);

    const auto messages =
        store.replay_from(
            "message-1002",
            10u);

    assert(messages.size() == 3u);

    assert(
        messages[0].id ==
        "message-1003");

    assert(
        messages[1].id ==
        "message-1004");

    assert(
        messages[2].id ==
        "message-1005");
  }

  static void test_replay_excludes_cursor_message()
  {
    TemporaryDatabase database{
        "vix_sqlite_replay_excludes_cursor"};

    SqliteMessageStore store{
        database.path_string()};

    append_range(
        store,
        1u,
        5u);

    const auto messages =
        store.replay_from(
            "message-1003",
            10u);

    assert(messages.size() == 2u);

    for (const JsonMessage &message :
         messages)
    {
      assert(
          message.id !=
          "message-1003");
    }
  }

  static void test_replay_is_oldest_first()
  {
    TemporaryDatabase database{
        "vix_sqlite_replay_order"};

    SqliteMessageStore store{
        database.path_string()};

    append_range(
        store,
        1u,
        8u);

    const auto messages =
        store.replay_from(
            "message-1001",
            10u);

    assert(messages.size() == 7u);

    for (std::size_t index = 0u;
         index < messages.size();
         ++index)
    {
      assert(
          messages[index].id ==
          "message-" +
              std::to_string(
                  1002u + index));
    }
  }

  static void test_replay_limit_is_applied()
  {
    TemporaryDatabase database{
        "vix_sqlite_replay_limit"};

    SqliteMessageStore store{
        database.path_string()};

    append_range(
        store,
        1u,
        10u);

    const auto messages =
        store.replay_from(
            "message-1002",
            3u);

    assert(messages.size() == 3u);

    assert(
        messages[0].id ==
        "message-1003");

    assert(
        messages[1].id ==
        "message-1004");

    assert(
        messages[2].id ==
        "message-1005");
  }

  static void test_zero_limit_returns_empty()
  {
    TemporaryDatabase database{
        "vix_sqlite_replay_zero_limit"};

    SqliteMessageStore store{
        database.path_string()};

    append_range(
        store,
        1u,
        5u);

    const auto messages =
        store.replay_from(
            "message-1001",
            0u);

    assert(messages.empty());

    const auto remaining =
        store.replay_from(
            "message-1001",
            10u);

    assert(remaining.size() == 4u);
  }

  static void test_large_limit_returns_all_newer_messages()
  {
    TemporaryDatabase database{
        "vix_sqlite_replay_large_limit"};

    SqliteMessageStore store{
        database.path_string()};

    append_range(
        store,
        1u,
        5u);

    const auto messages =
        store.replay_from(
            "message-1001",
            100u);

    assert(messages.size() == 4u);

    assert(
        messages.front().id ==
        "message-1002");

    assert(
        messages.back().id ==
        "message-1005");
  }

  static void test_replay_after_latest_message_is_empty()
  {
    TemporaryDatabase database{
        "vix_sqlite_replay_latest"};

    SqliteMessageStore store{
        database.path_string()};

    append_range(
        store,
        1u,
        5u);

    const auto messages =
        store.replay_from(
            "message-1005",
            10u);

    assert(messages.empty());
  }

  static void test_replay_crosses_room_boundaries()
  {
    TemporaryDatabase database{
        "vix_sqlite_replay_rooms"};

    SqliteMessageStore store{
        database.path_string()};

    append_message(
        store,
        1u,
        "general");

    append_message(
        store,
        2u,
        "support");

    append_message(
        store,
        3u,
        "general");

    append_message(
        store,
        4u,
        "notifications");

    const auto messages =
        store.replay_from(
            "message-1001",
            10u);

    assert(messages.size() == 3u);

    assert(
        messages[0].room ==
        "support");

    assert(
        messages[1].room ==
        "general");

    assert(
        messages[2].room ==
        "notifications");
  }

  static void test_replay_preserves_message_fields()
  {
    TemporaryDatabase database{
        "vix_sqlite_replay_fields"};

    SqliteMessageStore store{
        database.path_string()};

    JsonMessage cursor;

    cursor.id = "cursor-message";
    cursor.kind = "event";
    cursor.ts = "2026-07-30T14:00:00Z";
    cursor.room = "general";
    cursor.type = "cursor";

    JsonMessage expected;

    expected.id = "replayed-message";
    expected.kind = "command";
    expected.ts = "2026-07-30T14:01:00Z";
    expected.room = "support";
    expected.type = "chat.send";

    store.append(cursor);
    store.append(expected);

    const auto messages =
        store.replay_from(
            "cursor-message",
            10u);

    assert(messages.size() == 1u);

    assert(
        messages[0].id ==
        "replayed-message");

    assert(
        messages[0].kind ==
        "command");

    assert(
        messages[0].ts ==
        "2026-07-30T14:01:00Z");

    assert(
        messages[0].room ==
        "support");

    assert(
        messages[0].type ==
        "chat.send");

    const auto json =
        messages[0].to_nlohmann();

    assert(json.contains("payload"));
    assert(json["payload"].is_object());
  }

  static void test_replay_queries_are_non_destructive()
  {
    TemporaryDatabase database{
        "vix_sqlite_replay_non_destructive"};

    SqliteMessageStore store{
        database.path_string()};

    append_range(
        store,
        1u,
        5u);

    const auto first =
        store.replay_from(
            "message-1001",
            10u);

    const auto second =
        store.replay_from(
            "message-1001",
            10u);

    assert(first.size() == 4u);
    assert(second.size() == 4u);

    for (std::size_t index = 0u;
         index < first.size();
         ++index)
    {
      assert(
          first[index].id ==
          second[index].id);
    }
  }

  static void test_replay_can_continue_from_previous_result()
  {
    TemporaryDatabase database{
        "vix_sqlite_replay_continue"};

    SqliteMessageStore store{
        database.path_string()};

    append_range(
        store,
        1u,
        8u);

    const auto first =
        store.replay_from(
            "message-1001",
            3u);

    assert(first.size() == 3u);

    assert(
        first[0].id ==
        "message-1002");

    assert(
        first[1].id ==
        "message-1003");

    assert(
        first[2].id ==
        "message-1004");

    const auto second =
        store.replay_from(
            first.back().id,
            3u);

    assert(second.size() == 3u);

    assert(
        second[0].id ==
        "message-1005");

    assert(
        second[1].id ==
        "message-1006");

    assert(
        second[2].id ==
        "message-1007");

    const auto third =
        store.replay_from(
            second.back().id,
            3u);

    assert(third.size() == 1u);

    assert(
        third[0].id ==
        "message-1008");
  }

  static void test_replay_survives_store_reopen()
  {
    TemporaryDatabase database{
        "vix_sqlite_replay_reopen"};

    const std::string path =
        database.path_string();

    {
      SqliteMessageStore store{
          path};

      append_range(
          store,
          1u,
          5u);
    }

    {
      SqliteMessageStore store{
          path};

      const auto messages =
          store.replay_from(
              "message-1002",
              10u);

      assert(messages.size() == 3u);

      assert(
          messages[0].id ==
          "message-1003");

      assert(
          messages[1].id ==
          "message-1004");

      assert(
          messages[2].id ==
          "message-1005");
    }
  }

  static void test_new_messages_are_visible_to_later_replay()
  {
    TemporaryDatabase database{
        "vix_sqlite_replay_new_messages"};

    SqliteMessageStore store{
        database.path_string()};

    append_range(
        store,
        1u,
        3u);

    const auto first =
        store.replay_from(
            "message-1001",
            10u);

    assert(first.size() == 2u);

    append_message(
        store,
        4u,
        "notifications");

    append_message(
        store,
        5u,
        "general");

    const auto second =
        store.replay_from(
            "message-1003",
            10u);

    assert(second.size() == 2u);

    assert(
        second[0].id ==
        "message-1004");

    assert(
        second[1].id ==
        "message-1005");
  }

} // namespace

int main()
{
  test_replay_api_contract();

  test_empty_store_replay_returns_empty();

  test_replay_starts_after_cursor();
  test_replay_excludes_cursor_message();
  test_replay_is_oldest_first();

  test_replay_limit_is_applied();
  test_zero_limit_returns_empty();
  test_large_limit_returns_all_newer_messages();

  test_replay_after_latest_message_is_empty();
  test_replay_crosses_room_boundaries();

  test_replay_preserves_message_fields();
  test_replay_queries_are_non_destructive();

  test_replay_can_continue_from_previous_result();
  test_replay_survives_store_reopen();

  test_new_messages_are_visible_to_later_replay();

  return 0;
}
