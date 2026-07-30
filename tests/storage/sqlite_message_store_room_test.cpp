/**
 *
 * @file sqlite_message_store_room_test.cpp
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
        "2026-07-30T10:" +
        std::to_string(10u + index) +
        ":00Z";

    message.room =
        std::move(room);

    message.type =
        "chat.message." +
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

  static void test_room_api_contract()
  {
    static_assert(
        std::is_constructible_v<
            SqliteMessageStore,
            const std::string &>);

    static_assert(
        !std::is_copy_constructible_v<
            SqliteMessageStore>);

    static_assert(
        !std::is_copy_assignable_v<
            SqliteMessageStore>);

    static_assert(
        !std::is_move_constructible_v<
            SqliteMessageStore>);

    static_assert(
        !std::is_move_assignable_v<
            SqliteMessageStore>);

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
  }

  static void test_empty_store_returns_empty_room_history()
  {
    TemporaryDatabase database{
        "vix_sqlite_room_empty"};

    SqliteMessageStore store{
        database.path_string()};

    const auto messages =
        store.list_by_room(
            "general",
            10u);

    assert(messages.empty());
  }

  static void test_room_filter_is_exact()
  {
    TemporaryDatabase database{
        "vix_sqlite_room_filter"};

    SqliteMessageStore store{
        database.path_string()};

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
            "general-extra"));

    const auto general =
        store.list_by_room(
            "general",
            10u);

    const auto support =
        store.list_by_room(
            "support",
            10u);

    const auto extra =
        store.list_by_room(
            "general-extra",
            10u);

    assert(general.size() == 2u);
    assert(support.size() == 1u);
    assert(extra.size() == 1u);

    for (const JsonMessage &message :
         general)
    {
      assert(message.room == "general");
    }

    assert(support[0].room == "support");
    assert(extra[0].room == "general-extra");
  }

  static void test_room_history_is_newest_first()
  {
    TemporaryDatabase database{
        "vix_sqlite_room_order"};

    SqliteMessageStore store{
        database.path_string()};

    append_range(
        store,
        "general",
        1u,
        5u);

    const auto messages =
        store.list_by_room(
            "general",
            10u);

    assert(messages.size() == 5u);

    assert(
        messages[0].id ==
        "message-1005");

    assert(
        messages[1].id ==
        "message-1004");

    assert(
        messages[2].id ==
        "message-1003");

    assert(
        messages[3].id ==
        "message-1002");

    assert(
        messages[4].id ==
        "message-1001");
  }

  static void test_room_limit_is_applied()
  {
    TemporaryDatabase database{
        "vix_sqlite_room_limit"};

    SqliteMessageStore store{
        database.path_string()};

    append_range(
        store,
        "general",
        1u,
        10u);

    const auto messages =
        store.list_by_room(
            "general",
            3u);

    assert(messages.size() == 3u);

    assert(
        messages[0].id ==
        "message-1010");

    assert(
        messages[1].id ==
        "message-1009");

    assert(
        messages[2].id ==
        "message-1008");
  }

  static void test_zero_limit_returns_no_messages()
  {
    TemporaryDatabase database{
        "vix_sqlite_room_zero_limit"};

    SqliteMessageStore store{
        database.path_string()};

    append_range(
        store,
        "general",
        1u,
        5u);

    const auto messages =
        store.list_by_room(
            "general",
            0u);

    assert(messages.empty());

    const auto remaining =
        store.list_by_room(
            "general",
            10u);

    assert(remaining.size() == 5u);
  }

  static void test_limit_larger_than_history_returns_everything()
  {
    TemporaryDatabase database{
        "vix_sqlite_room_large_limit"};

    SqliteMessageStore store{
        database.path_string()};

    append_range(
        store,
        "general",
        1u,
        4u);

    const auto messages =
        store.list_by_room(
            "general",
            100u);

    assert(messages.size() == 4u);
  }

  static void test_before_id_returns_older_messages()
  {
    TemporaryDatabase database{
        "vix_sqlite_room_before"};

    SqliteMessageStore store{
        database.path_string()};

    append_range(
        store,
        "general",
        1u,
        6u);

    const auto messages =
        store.list_by_room(
            "general",
            10u,
            std::optional<std::string>{
                "message-1004"});

    assert(messages.size() == 3u);

    assert(
        messages[0].id ==
        "message-1003");

    assert(
        messages[1].id ==
        "message-1002");

    assert(
        messages[2].id ==
        "message-1001");
  }

  static void test_before_id_excludes_cursor()
  {
    TemporaryDatabase database{
        "vix_sqlite_room_cursor_excluded"};

    SqliteMessageStore store{
        database.path_string()};

    append_range(
        store,
        "general",
        1u,
        5u);

    const auto messages =
        store.list_by_room(
            "general",
            10u,
            std::optional<std::string>{
                "message-1003"});

    assert(messages.size() == 2u);

    for (const JsonMessage &message :
         messages)
    {
      assert(
          message.id !=
          "message-1003");
    }
  }

  static void test_before_oldest_message_returns_empty()
  {
    TemporaryDatabase database{
        "vix_sqlite_room_before_oldest"};

    SqliteMessageStore store{
        database.path_string()};

    append_range(
        store,
        "general",
        1u,
        5u);

    const auto messages =
        store.list_by_room(
            "general",
            10u,
            std::optional<std::string>{
                "message-1001"});

    assert(messages.empty());
  }

  static void test_before_id_respects_limit()
  {
    TemporaryDatabase database{
        "vix_sqlite_room_before_limit"};

    SqliteMessageStore store{
        database.path_string()};

    append_range(
        store,
        "general",
        1u,
        10u);

    const auto messages =
        store.list_by_room(
            "general",
            2u,
            std::optional<std::string>{
                "message-1008"});

    assert(messages.size() == 2u);

    assert(
        messages[0].id ==
        "message-1007");

    assert(
        messages[1].id ==
        "message-1006");
  }

  static void test_empty_room_name_is_supported()
  {
    TemporaryDatabase database{
        "vix_sqlite_room_empty_name"};

    SqliteMessageStore store{
        database.path_string()};

    store.append(
        make_message(
            1u,
            ""));

    store.append(
        make_message(
            2u,
            "general"));

    store.append(
        make_message(
            3u,
            ""));

    const auto messages =
        store.list_by_room(
            "",
            10u);

    assert(messages.size() == 2u);

    assert(
        messages[0].id ==
        "message-1003");

    assert(
        messages[1].id ==
        "message-1001");

    assert(messages[0].room.empty());
    assert(messages[1].room.empty());
  }

  static void test_room_history_preserves_message_fields()
  {
    TemporaryDatabase database{
        "vix_sqlite_room_fields"};

    SqliteMessageStore store{
        database.path_string()};

    JsonMessage message;

    message.id = "message-fields";
    message.kind = "command";
    message.ts = "2026-07-30T14:00:00Z";
    message.room = "general";
    message.type = "chat.created";

    store.append(message);

    const auto messages =
        store.list_by_room(
            "general",
            1u);

    assert(messages.size() == 1u);

    assert(
        messages[0].id ==
        "message-fields");

    assert(
        messages[0].kind ==
        "command");

    assert(
        messages[0].ts ==
        "2026-07-30T14:00:00Z");

    assert(
        messages[0].room ==
        "general");

    assert(
        messages[0].type ==
        "chat.created");

    const auto json =
        messages[0].to_nlohmann();

    assert(json.contains("payload"));
    assert(json["payload"].is_object());
  }

  static void test_room_queries_do_not_remove_messages()
  {
    TemporaryDatabase database{
        "vix_sqlite_room_non_destructive"};

    SqliteMessageStore store{
        database.path_string()};

    append_range(
        store,
        "general",
        1u,
        5u);

    const auto first =
        store.list_by_room(
            "general",
            10u);

    const auto second =
        store.list_by_room(
            "general",
            10u);

    assert(first.size() == 5u);
    assert(second.size() == 5u);

    for (std::size_t index = 0u;
         index < first.size();
         ++index)
    {
      assert(
          first[index].id ==
          second[index].id);
    }
  }

  static void test_room_history_survives_reopen()
  {
    TemporaryDatabase database{
        "vix_sqlite_room_reopen"};

    const std::string path =
        database.path_string();

    {
      SqliteMessageStore store{
          path};

      append_range(
          store,
          "general",
          1u,
          3u);

      store.append(
          make_message(
              4u,
              "support"));
    }

    {
      SqliteMessageStore store{
          path};

      const auto general =
          store.list_by_room(
              "general",
              10u);

      const auto support =
          store.list_by_room(
              "support",
              10u);

      assert(general.size() == 3u);
      assert(support.size() == 1u);

      assert(
          general[0].id ==
          "message-1003");

      assert(
          general[1].id ==
          "message-1002");

      assert(
          general[2].id ==
          "message-1001");

      assert(
          support[0].id ==
          "message-1004");
    }
  }

} // namespace

int main()
{
  test_room_api_contract();

  test_empty_store_returns_empty_room_history();
  test_room_filter_is_exact();

  test_room_history_is_newest_first();

  test_room_limit_is_applied();
  test_zero_limit_returns_no_messages();
  test_limit_larger_than_history_returns_everything();

  test_before_id_returns_older_messages();
  test_before_id_excludes_cursor();
  test_before_oldest_message_returns_empty();
  test_before_id_respects_limit();

  test_empty_room_name_is_supported();

  test_room_history_preserves_message_fields();
  test_room_queries_do_not_remove_messages();

  test_room_history_survives_reopen();

  return 0;
}
