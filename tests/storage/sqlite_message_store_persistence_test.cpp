/**
 *
 * @file sqlite_message_store_persistence_test.cpp
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
#include <utility>
#include <vector>

#include <sqlite3.h>

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

    [[nodiscard]]
    const std::filesystem::path &
    filesystem_path() const noexcept
    {
      return databasePath_;
    }

    [[nodiscard]]
    const std::filesystem::path &
    directory() const noexcept
    {
      return directory_;
    }

  private:
    std::filesystem::path directory_{};
    std::filesystem::path databasePath_{};
  };

  class SqliteConnection
  {
  public:
    explicit SqliteConnection(
        const std::filesystem::path &path)
    {
      const int result =
          sqlite3_open_v2(
              path.string().c_str(),
              &database_,
              SQLITE_OPEN_READONLY,
              nullptr);

      assert(result == SQLITE_OK);
      assert(database_ != nullptr);
    }

    SqliteConnection(
        const SqliteConnection &) = delete;

    SqliteConnection &operator=(
        const SqliteConnection &) = delete;

    ~SqliteConnection()
    {
      if (database_ != nullptr)
      {
        sqlite3_close(database_);
      }
    }

    [[nodiscard]]
    sqlite3 *get() const noexcept
    {
      return database_;
    }

  private:
    sqlite3 *database_{nullptr};
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
        "2026-07-30T13:" +
        std::to_string(
            10u + index) +
        ":00Z";

    message.room =
        std::move(room);

    message.type =
        "persistent.message." +
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

  static std::string query_text(
      sqlite3 *database,
      const char *sql)
  {
    sqlite3_stmt *statement = nullptr;

    const int prepareResult =
        sqlite3_prepare_v2(
            database,
            sql,
            -1,
            &statement,
            nullptr);

    assert(prepareResult == SQLITE_OK);
    assert(statement != nullptr);

    const int stepResult =
        sqlite3_step(statement);

    assert(stepResult == SQLITE_ROW);

    const unsigned char *value =
        sqlite3_column_text(
            statement,
            0);

    const std::string result =
        value == nullptr
            ? std::string{}
            : reinterpret_cast<
                  const char *>(value);

    const int finalizeResult =
        sqlite3_finalize(statement);

    assert(finalizeResult == SQLITE_OK);

    return result;
  }

  static std::size_t query_count(
      sqlite3 *database,
      const char *sql)
  {
    sqlite3_stmt *statement = nullptr;

    const int prepareResult =
        sqlite3_prepare_v2(
            database,
            sql,
            -1,
            &statement,
            nullptr);

    assert(prepareResult == SQLITE_OK);
    assert(statement != nullptr);

    const int stepResult =
        sqlite3_step(statement);

    assert(stepResult == SQLITE_ROW);

    const auto result =
        static_cast<std::size_t>(
            sqlite3_column_int64(
                statement,
                0));

    const int finalizeResult =
        sqlite3_finalize(statement);

    assert(finalizeResult == SQLITE_OK);

    return result;
  }

  static void assert_database_integrity(
      const std::filesystem::path &path)
  {
    SqliteConnection connection{
        path};

    assert(
        query_text(
            connection.get(),
            "PRAGMA integrity_check;") ==
        "ok");
  }

  static void test_database_file_survives_store_destruction()
  {
    TemporaryDatabase database{
        "vix_sqlite_persistence_file"};

    {
      SqliteMessageStore store{
          database.path()};

      store.append(
          make_message(1u));
    }

    assert(
        std::filesystem::exists(
            database.filesystem_path()));

    assert(
        std::filesystem::is_regular_file(
            database.filesystem_path()));

    assert(
        std::filesystem::file_size(
            database.filesystem_path()) >
        0u);

    assert_database_integrity(
        database.filesystem_path());
  }

  static void test_message_survives_reopen()
  {
    TemporaryDatabase database{
        "vix_sqlite_persistence_message"};

    const std::string path =
        database.path();

    {
      SqliteMessageStore store{
          path};

      store.append(
          make_message(
              1u,
              "general"));
    }

    {
      SqliteMessageStore store{
          path};

      const auto messages =
          store.list_by_room(
              "general",
              10u);

      assert(messages.size() == 1u);

      assert(
          messages[0].id ==
          message_id(1u));

      assert(
          messages[0].kind ==
          "event");

      assert(
          messages[0].room ==
          "general");

      assert(
          messages[0].type ==
          "persistent.message.1");

      assert(
          messages[0].ts ==
          "2026-07-30T13:11:00Z");
    }
  }

  static void test_multiple_rooms_survive_reopen()
  {
    TemporaryDatabase database{
        "vix_sqlite_persistence_rooms"};

    const std::string path =
        database.path();

    {
      SqliteMessageStore store{
          path};

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

      const auto notifications =
          store.list_by_room(
              "notifications",
              10u);

      assert_ids(
          general,
          {4u, 1u});

      assert_ids(
          support,
          {2u});

      assert_ids(
          notifications,
          {3u});
    }
  }

  static void test_replay_survives_reopen()
  {
    TemporaryDatabase database{
        "vix_sqlite_persistence_replay"};

    const std::string path =
        database.path();

    {
      SqliteMessageStore store{
          path};

      append_range(
          store,
          "general",
          1u,
          5u);
    }

    {
      SqliteMessageStore store{
          path};

      const auto messages =
          store.replay_from(
              message_id(2u),
              10u);

      assert_ids(
          messages,
          {3u, 4u, 5u});
    }
  }

  static void test_new_messages_can_be_appended_after_reopen()
  {
    TemporaryDatabase database{
        "vix_sqlite_persistence_append_reopen"};

    const std::string path =
        database.path();

    {
      SqliteMessageStore store{
          path};

      append_range(
          store,
          "general",
          1u,
          3u);
    }

    {
      SqliteMessageStore store{
          path};

      append_range(
          store,
          "general",
          4u,
          6u);
    }

    {
      SqliteMessageStore store{
          path};

      const auto messages =
          store.list_by_room(
              "general",
              10u);

      assert_ids(
          messages,
          {6u, 5u, 4u, 3u, 2u, 1u});
    }
  }

  static void test_data_survives_multiple_reopen_cycles()
  {
    TemporaryDatabase database{
        "vix_sqlite_persistence_cycles"};

    const std::string path =
        database.path();

    constexpr std::size_t cycleCount = 10u;

    for (std::size_t index = 1u;
         index <= cycleCount;
         ++index)
    {
      SqliteMessageStore store{
          path};

      store.append(
          make_message(
              index,
              "general"));
    }

    {
      SqliteMessageStore store{
          path};

      const auto messages =
          store.list_by_room(
              "general",
              cycleCount);

      assert(messages.size() == cycleCount);

      for (std::size_t index = 0u;
           index < cycleCount;
           ++index)
      {
        assert(
            messages[index].id ==
            message_id(
                cycleCount -
                index));
      }
    }

    assert_database_integrity(
        database.filesystem_path());
  }

  static void test_insert_or_replace_persists_replacement()
  {
    TemporaryDatabase database{
        "vix_sqlite_persistence_replace"};

    const std::string path =
        database.path();

    {
      SqliteMessageStore store{
          path};

      JsonMessage original =
          make_message(
              1u,
              "general");

      original.kind = "event";
      original.type = "original.type";
      original.ts = "2026-07-30T13:00:00Z";

      store.append(original);

      JsonMessage replacement =
          original;

      replacement.kind = "command";
      replacement.room = "support";
      replacement.type = "replacement.type";
      replacement.ts = "2026-07-30T14:00:00Z";

      store.append(replacement);
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

      assert(general.empty());
      assert(support.size() == 1u);

      assert(
          support[0].id ==
          message_id(1u));

      assert(
          support[0].kind ==
          "command");

      assert(
          support[0].room ==
          "support");

      assert(
          support[0].type ==
          "replacement.type");

      assert(
          support[0].ts ==
          "2026-07-30T14:00:00Z");
    }
  }

  static void test_replacement_does_not_create_duplicate_rows()
  {
    TemporaryDatabase database{
        "vix_sqlite_persistence_replace_count"};

    const std::string path =
        database.path();

    {
      SqliteMessageStore store{
          path};

      JsonMessage message =
          make_message(
              1u,
              "general");

      for (std::size_t index = 0u;
           index < 20u;
           ++index)
      {
        message.type =
            "replacement." +
            std::to_string(index);

        store.append(message);
      }
    }

    SqliteConnection connection{
        database.filesystem_path()};

    assert(
        query_count(
            connection.get(),
            "SELECT COUNT(*) FROM messages;") ==
        1u);
  }

  static void test_automatic_fields_survive_reopen()
  {
    TemporaryDatabase database{
        "vix_sqlite_persistence_defaults"};

    const std::string path =
        database.path();

    std::string generatedId;

    {
      SqliteMessageStore store{
          path};

      JsonMessage message;

      message.id.clear();
      message.kind.clear();
      message.ts.clear();
      message.room = "generated";
      message.type = "generated.message";

      store.append(message);

      const auto messages =
          store.list_by_room(
              "generated",
              10u);

      assert(messages.size() == 1u);

      generatedId =
          messages[0].id;

      assert(!generatedId.empty());

      assert(
          messages[0].kind ==
          "event");

      assert(!messages[0].ts.empty());
    }

    {
      SqliteMessageStore store{
          path};

      const auto messages =
          store.list_by_room(
              "generated",
              10u);

      assert(messages.size() == 1u);

      assert(
          messages[0].id ==
          generatedId);

      assert(
          messages[0].kind ==
          "event");

      assert(!messages[0].ts.empty());

      assert(
          messages[0].room ==
          "generated");

      assert(
          messages[0].type ==
          "generated.message");
    }
  }

  static void test_room_cursor_survives_reopen()
  {
    TemporaryDatabase database{
        "vix_sqlite_persistence_room_cursor"};

    const std::string path =
        database.path();

    {
      SqliteMessageStore store{
          path};

      append_range(
          store,
          "general",
          1u,
          8u);
    }

    {
      SqliteMessageStore store{
          path};

      const auto messages =
          store.list_by_room(
              "general",
              3u,
              std::optional<std::string>{
                  message_id(7u)});

      assert_ids(
          messages,
          {6u, 5u, 4u});
    }
  }

  static void test_database_uses_wal_mode()
  {
    TemporaryDatabase database{
        "vix_sqlite_persistence_wal"};

    {
      SqliteMessageStore store{
          database.path()};

      store.append(
          make_message(1u));
    }

    SqliteConnection connection{
        database.filesystem_path()};

    assert(
        query_text(
            connection.get(),
            "PRAGMA journal_mode;") ==
        "wal");
  }

  static void test_database_schema_persists()
  {
    TemporaryDatabase database{
        "vix_sqlite_persistence_schema"};

    {
      SqliteMessageStore store{
          database.path()};

      store.append(
          make_message(1u));
    }

    SqliteConnection connection{
        database.filesystem_path()};

    assert(
        query_count(
            connection.get(),
            "SELECT COUNT(*) "
            "FROM sqlite_master "
            "WHERE type = 'table' "
            "AND name = 'messages';") ==
        1u);

    assert(
        query_count(
            connection.get(),
            "SELECT COUNT(*) "
            "FROM pragma_table_info('messages') "
            "WHERE name IN "
            "('id', 'kind', 'room', 'type', 'ts', 'payload_json');") ==
        6u);
  }

  static void test_separate_database_files_are_independent()
  {
    TemporaryDatabase firstDatabase{
        "vix_sqlite_persistence_first"};

    TemporaryDatabase secondDatabase{
        "vix_sqlite_persistence_second"};

    {
      SqliteMessageStore first{
          firstDatabase.path()};

      SqliteMessageStore second{
          secondDatabase.path()};

      first.append(
          make_message(
              1u,
              "first"));

      second.append(
          make_message(
              2u,
              "second"));
    }

    {
      SqliteMessageStore first{
          firstDatabase.path()};

      SqliteMessageStore second{
          secondDatabase.path()};

      const auto firstMessages =
          first.list_by_room(
              "first",
              10u);

      const auto secondMessages =
          second.list_by_room(
              "second",
              10u);

      assert_ids(
          firstMessages,
          {1u});

      assert_ids(
          secondMessages,
          {2u});

      assert(
          first.list_by_room(
                   "second",
                   10u)
              .empty());

      assert(
          second.list_by_room(
                    "first",
                    10u)
              .empty());
    }
  }

} // namespace

int main()
{
  test_database_file_survives_store_destruction();

  test_message_survives_reopen();
  test_multiple_rooms_survive_reopen();
  test_replay_survives_reopen();

  test_new_messages_can_be_appended_after_reopen();
  test_data_survives_multiple_reopen_cycles();

  test_insert_or_replace_persists_replacement();
  test_replacement_does_not_create_duplicate_rows();

  test_automatic_fields_survive_reopen();
  test_room_cursor_survives_reopen();

  test_database_uses_wal_mode();
  test_database_schema_persists();

  test_separate_database_files_are_independent();

  return 0;
}
