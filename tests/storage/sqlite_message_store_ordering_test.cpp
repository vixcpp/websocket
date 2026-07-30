/**
 *
 * @file sqlite_message_store_ordering_test.cpp
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
#include <optional>
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

      databasePath_ =
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
    std::string path() const
    {
      return databasePath_.string();
    }

  private:
    std::filesystem::path directory_{};
    std::filesystem::path databasePath_{};
  };

  static std::string message_id(
      std::size_t index)
  {
    std::string value =
        std::to_string(index);

    while (value.size() < 20u)
    {
      value.insert(
          value.begin(),
          '0');
    }

    return value;
  }

  static JsonMessage make_message(
      std::size_t index,
      std::string room = "general")
  {
    JsonMessage message;

    message.id =
        message_id(index);

    message.kind = "event";

    message.ts =
        "2026-07-30T12:" +
        std::to_string(
            10u + index) +
        ":00Z";

    message.room =
        std::move(room);

    message.type =
        "message." +
        std::to_string(index);

    return message;
  }

  static void append_range(
      SqliteMessageStore &store,
      const std::string &room,
      std::size_t first,
      std::size_t last)
  {
    for (std::size_t index = first;
         index <= last;
         ++index)
    {
      store.append(
          make_message(
              index,
              room));
    }
  }

  static void assert_ids(
      const std::vector<JsonMessage> &messages,
      const std::vector<std::size_t> &expected)
  {
    assert(
        messages.size() ==
        expected.size());

    for (std::size_t index = 0u;
         index < expected.size();
         ++index)
    {
      assert(
          messages[index].id ==
          message_id(
              expected[index]));
    }
  }

  static void test_ordering_api_contract()
  {
    static_assert(
        std::is_same_v<
            decltype(std::declval<
                         SqliteMessageStore &>()
                         .append(
                             std::declval<
                                 const JsonMessage &>())),
            void>);

    static_assert(
        std::is_same_v<
            decltype(std::declval<
                         SqliteMessageStore &>()
                         .list_by_room(
                             std::declval<
                                 const std::string &>(),
                             std::size_t{10},
                             std::declval<
                                 const std::optional<
                                     std::string> &>())),
            std::vector<JsonMessage>>);

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

  static void test_room_history_is_newest_first()
  {
    TemporaryDatabase database{
        "vix_sqlite_ordering_room"};

    SqliteMessageStore store{
        database.path()};

    append_range(
        store,
        "general",
        1u,
        5u);

    const auto messages =
        store.list_by_room(
            "general",
            10u);

    assert_ids(
        messages,
        {5u, 4u, 3u, 2u, 1u});
  }

  static void test_replay_is_oldest_first()
  {
    TemporaryDatabase database{
        "vix_sqlite_ordering_replay"};

    SqliteMessageStore store{
        database.path()};

    append_range(
        store,
        "general",
        1u,
        5u);

    const auto messages =
        store.replay_from(
            message_id(1u),
            10u);

    assert_ids(
        messages,
        {2u, 3u, 4u, 5u});
  }

  static void test_append_order_does_not_control_room_order()
  {
    TemporaryDatabase database{
        "vix_sqlite_ordering_append_order"};

    SqliteMessageStore store{
        database.path()};

    store.append(
        make_message(
            4u,
            "general"));

    store.append(
        make_message(
            1u,
            "general"));

    store.append(
        make_message(
            5u,
            "general"));

    store.append(
        make_message(
            2u,
            "general"));

    store.append(
        make_message(
            3u,
            "general"));

    const auto messages =
        store.list_by_room(
            "general",
            10u);

    assert_ids(
        messages,
        {5u, 4u, 3u, 2u, 1u});
  }

  static void test_append_order_does_not_control_replay_order()
  {
    TemporaryDatabase database{
        "vix_sqlite_ordering_replay_append_order"};

    SqliteMessageStore store{
        database.path()};

    store.append(
        make_message(
            5u,
            "general"));

    store.append(
        make_message(
            2u,
            "support"));

    store.append(
        make_message(
            4u,
            "general"));

    store.append(
        make_message(
            1u,
            "support"));

    store.append(
        make_message(
            3u,
            "general"));

    const auto messages =
        store.replay_from(
            message_id(1u),
            10u);

    assert_ids(
        messages,
        {2u, 3u, 4u, 5u});
  }

  static void test_timestamps_do_not_control_ordering()
  {
    TemporaryDatabase database{
        "vix_sqlite_ordering_timestamp"};

    SqliteMessageStore store{
        database.path()};

    JsonMessage first =
        make_message(
            1u,
            "general");

    JsonMessage second =
        make_message(
            2u,
            "general");

    JsonMessage third =
        make_message(
            3u,
            "general");

    first.ts =
        "2030-01-01T00:00:00Z";

    second.ts =
        "2020-01-01T00:00:00Z";

    third.ts =
        "2025-01-01T00:00:00Z";

    store.append(first);
    store.append(second);
    store.append(third);

    const auto roomMessages =
        store.list_by_room(
            "general",
            10u);

    assert_ids(
        roomMessages,
        {3u, 2u, 1u});

    const auto replayMessages =
        store.replay_from(
            message_id(1u),
            10u);

    assert_ids(
        replayMessages,
        {2u, 3u});
  }

  static void test_room_ordering_is_independent_per_room()
  {
    TemporaryDatabase database{
        "vix_sqlite_ordering_rooms"};

    SqliteMessageStore store{
        database.path()};

    store.append(
        make_message(
            1u,
            "general"));

    store.append(
        make_message(
            2u,
            "support"));

    store.append(
        make_message(
            3u,
            "general"));

    store.append(
        make_message(
            4u,
            "support"));

    store.append(
        make_message(
            5u,
            "general"));

    store.append(
        make_message(
            6u,
            "support"));

    const auto general =
        store.list_by_room(
            "general",
            10u);

    const auto support =
        store.list_by_room(
            "support",
            10u);

    assert_ids(
        general,
        {5u, 3u, 1u});

    assert_ids(
        support,
        {6u, 4u, 2u});
  }

  static void test_room_limit_selects_newest_messages()
  {
    TemporaryDatabase database{
        "vix_sqlite_ordering_room_limit"};

    SqliteMessageStore store{
        database.path()};

    append_range(
        store,
        "general",
        1u,
        10u);

    const auto messages =
        store.list_by_room(
            "general",
            4u);

    assert_ids(
        messages,
        {10u, 9u, 8u, 7u});
  }

  static void test_replay_limit_selects_oldest_newer_messages()
  {
    TemporaryDatabase database{
        "vix_sqlite_ordering_replay_limit"};

    SqliteMessageStore store{
        database.path()};

    append_range(
        store,
        "general",
        1u,
        10u);

    const auto messages =
        store.replay_from(
            message_id(3u),
            4u);

    assert_ids(
        messages,
        {4u, 5u, 6u, 7u});
  }

  static void test_before_cursor_returns_strictly_older_messages()
  {
    TemporaryDatabase database{
        "vix_sqlite_ordering_before"};

    SqliteMessageStore store{
        database.path()};

    append_range(
        store,
        "general",
        1u,
        10u);

    const auto messages =
        store.list_by_room(
            "general",
            10u,
            std::optional<std::string>{
                message_id(7u)});

    assert_ids(
        messages,
        {6u, 5u, 4u, 3u, 2u, 1u});
  }

  static void test_before_cursor_and_limit_work_together()
  {
    TemporaryDatabase database{
        "vix_sqlite_ordering_before_limit"};

    SqliteMessageStore store{
        database.path()};

    append_range(
        store,
        "general",
        1u,
        10u);

    const auto messages =
        store.list_by_room(
            "general",
            3u,
            std::optional<std::string>{
                message_id(8u)});

    assert_ids(
        messages,
        {7u, 6u, 5u});
  }

  static void test_replay_cursor_is_strictly_excluded()
  {
    TemporaryDatabase database{
        "vix_sqlite_ordering_replay_cursor"};

    SqliteMessageStore store{
        database.path()};

    append_range(
        store,
        "general",
        1u,
        6u);

    const auto messages =
        store.replay_from(
            message_id(3u),
            10u);

    assert_ids(
        messages,
        {4u, 5u, 6u});

    for (const JsonMessage &message :
         messages)
    {
      assert(
          message.id !=
          message_id(3u));
    }
  }

  static void test_replay_crosses_rooms_in_global_id_order()
  {
    TemporaryDatabase database{
        "vix_sqlite_ordering_global"};

    SqliteMessageStore store{
        database.path()};

    store.append(
        make_message(
            1u,
            "general"));

    store.append(
        make_message(
            2u,
            "support"));

    store.append(
        make_message(
            3u,
            "notifications"));

    store.append(
        make_message(
            4u,
            "general"));

    const auto messages =
        store.replay_from(
            message_id(1u),
            10u);

    assert_ids(
        messages,
        {2u, 3u, 4u});

    assert(
        messages[0].room ==
        "support");

    assert(
        messages[1].room ==
        "notifications");

    assert(
        messages[2].room ==
        "general");
  }

  static void test_missing_replay_cursor_uses_lexical_position()
  {
    TemporaryDatabase database{
        "vix_sqlite_ordering_missing_cursor"};

    SqliteMessageStore store{
        database.path()};

    append_range(
        store,
        "general",
        1u,
        5u);

    const auto messages =
        store.replay_from(
            "000000000000000000025",
            10u);

    assert_ids(
        messages,
        {3u, 4u, 5u});
  }

  static void test_identical_queries_are_deterministic()
  {
    TemporaryDatabase database{
        "vix_sqlite_ordering_deterministic"};

    SqliteMessageStore store{
        database.path()};

    append_range(
        store,
        "general",
        1u,
        20u);

    const auto first =
        store.list_by_room(
            "general",
            10u);

    const auto second =
        store.list_by_room(
            "general",
            10u);

    assert(
        first.size() ==
        second.size());

    for (std::size_t index = 0u;
         index < first.size();
         ++index)
    {
      assert(
          first[index].id ==
          second[index].id);
    }

    const auto firstReplay =
        store.replay_from(
            message_id(5u),
            10u);

    const auto secondReplay =
        store.replay_from(
            message_id(5u),
            10u);

    assert(
        firstReplay.size() ==
        secondReplay.size());

    for (std::size_t index = 0u;
         index < firstReplay.size();
         ++index)
    {
      assert(
          firstReplay[index].id ==
          secondReplay[index].id);
    }
  }

} // namespace

int main()
{
  test_ordering_api_contract();

  test_room_history_is_newest_first();
  test_replay_is_oldest_first();

  test_append_order_does_not_control_room_order();
  test_append_order_does_not_control_replay_order();

  test_timestamps_do_not_control_ordering();
  test_room_ordering_is_independent_per_room();

  test_room_limit_selects_newest_messages();
  test_replay_limit_selects_oldest_newer_messages();

  test_before_cursor_returns_strictly_older_messages();
  test_before_cursor_and_limit_work_together();

  test_replay_cursor_is_strictly_excluded();
  test_replay_crosses_rooms_in_global_id_order();

  test_missing_replay_cursor_uses_lexical_position();
  test_identical_queries_are_deterministic();

  return 0;
}
