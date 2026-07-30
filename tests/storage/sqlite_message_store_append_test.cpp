/**
 *
 * @file sqlite_message_store_append_test.cpp
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
#include <filesystem>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <sqlite3.h>

#include <vix/websocket/protocol.hpp>

#if __has_include(<vix/websocket/SqliteMessageStore.hpp>)
#include <vix/websocket/SqliteMessageStore.hpp>
#elif __has_include(<vix/websocket/storage/SqliteMessageStore.hpp>)
#include <vix/websocket/storage/SqliteMessageStore.hpp>
#elif __has_include(<vix/websocket/sqlite_message_store.hpp>)
#include <vix/websocket/sqlite_message_store.hpp>
#else
#error "Vix WebSocket SqliteMessageStore header was not found"
#endif

namespace
{
  using JsonMessage =
      vix::websocket::JsonMessage;

  using SqliteMessageStore =
      vix::websocket::SqliteMessageStore;

  template <typename>
  inline constexpr bool alwaysFalse = false;

  class TemporaryDatabase
  {
  public:
    explicit TemporaryDatabase(
        std::string name)
    {
      const auto stamp =
          std::chrono::steady_clock::now()
              .time_since_epoch()
              .count();

      directory_ =
          std::filesystem::temp_directory_path() /
          (std::move(name) +
           "_" +
           std::to_string(stamp));

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
    const std::filesystem::path &
    path() const noexcept
    {
      return path_;
    }

  private:
    std::filesystem::path directory_{};
    std::filesystem::path path_{};
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

  template <typename Store>
  [[nodiscard]]
  Store make_store(
      const std::filesystem::path &path)
  {
    if constexpr (
        std::is_constructible_v<
            Store,
            const std::filesystem::path &>)
    {
      return Store{path};
    }
    else if constexpr (
        std::is_constructible_v<
            Store,
            std::filesystem::path>)
    {
      return Store{
          std::filesystem::path{path}};
    }
    else if constexpr (
        std::is_constructible_v<
            Store,
            const std::string &>)
    {
      const std::string value =
          path.string();

      return Store{value};
    }
    else if constexpr (
        std::is_constructible_v<
            Store,
            std::string>)
    {
      return Store{
          path.string()};
    }
    else if constexpr (
        std::is_constructible_v<
            Store,
            const char *>)
    {
      const std::string value =
          path.string();

      return Store{
          value.c_str()};
    }
    else
    {
      static_assert(
          alwaysFalse<Store>,
          "Unsupported SqliteMessageStore constructor");
    }
  }

  template <typename Store>
  void append_message(
      Store &store,
      const JsonMessage &message)
  {
    if constexpr (
        requires {
          store.append(message);
        })
    {
      (void)store.append(message);
    }
    else if constexpr (
        requires {
          store.append(
              message.room,
              message);
        })
    {
      (void)store.append(
          message.room,
          message);
    }
    else if constexpr (
        requires {
          store.append(
              message.room,
              JsonMessage::serialize(
                  message));
        })
    {
      (void)store.append(
          message.room,
          JsonMessage::serialize(
              message));
    }
    else if constexpr (
        requires {
          store.append(
              JsonMessage::serialize(
                  message));
        })
    {
      (void)store.append(
          JsonMessage::serialize(
              message));
    }
    else
    {
      static_assert(
          alwaysFalse<Store>,
          "Unsupported SqliteMessageStore::append signature");
    }
  }

  static JsonMessage make_message(
      std::size_t index,
      std::string room = "general")
  {
    JsonMessage message;

    message.id =
        "message-" +
        std::to_string(index);

    message.kind = "event";

    message.ts =
        "2026-07-30T11:" +
        std::to_string(
            10u + index) +
        ":00Z";

    message.room =
        std::move(room);

    message.type =
        "chat.message." +
        std::to_string(index);

    return message;
  }

  static std::string quote_identifier(
      std::string_view identifier)
  {
    std::string result;
    result.reserve(
        identifier.size() + 2u);

    result.push_back('"');

    for (const char character :
         identifier)
    {
      if (character == '"')
      {
        result.push_back('"');
      }

      result.push_back(
          character);
    }

    result.push_back('"');

    return result;
  }

  static std::vector<std::string>
  user_tables(
      sqlite3 *database)
  {
    sqlite3_stmt *statement = nullptr;

    const char *sql =
        "SELECT name "
        "FROM sqlite_master "
        "WHERE type = 'table' "
        "AND name NOT LIKE 'sqlite_%' "
        "ORDER BY name;";

    const int prepareResult =
        sqlite3_prepare_v2(
            database,
            sql,
            -1,
            &statement,
            nullptr);

    assert(prepareResult == SQLITE_OK);
    assert(statement != nullptr);

    std::vector<std::string> tables;

    while (true)
    {
      const int stepResult =
          sqlite3_step(statement);

      if (stepResult == SQLITE_DONE)
      {
        break;
      }

      assert(stepResult == SQLITE_ROW);

      const unsigned char *name =
          sqlite3_column_text(
              statement,
              0);

      assert(name != nullptr);

      tables.emplace_back(
          reinterpret_cast<
              const char *>(name));
    }

    const int finalizeResult =
        sqlite3_finalize(statement);

    assert(finalizeResult == SQLITE_OK);

    return tables;
  }

  static std::size_t table_row_count(
      sqlite3 *database,
      const std::string &table)
  {
    const std::string sql =
        "SELECT COUNT(*) FROM " +
        quote_identifier(table) +
        ";";

    sqlite3_stmt *statement = nullptr;

    const int prepareResult =
        sqlite3_prepare_v2(
            database,
            sql.c_str(),
            -1,
            &statement,
            nullptr);

    assert(prepareResult == SQLITE_OK);
    assert(statement != nullptr);

    const int stepResult =
        sqlite3_step(statement);

    assert(stepResult == SQLITE_ROW);

    const auto count =
        static_cast<std::size_t>(
            sqlite3_column_int64(
                statement,
                0));

    const int finalizeResult =
        sqlite3_finalize(statement);

    assert(finalizeResult == SQLITE_OK);

    return count;
  }

  static std::size_t total_row_count(
      const std::filesystem::path &path)
  {
    SqliteConnection connection{
        path};

    std::size_t total = 0u;

    for (const std::string &table :
         user_tables(connection.get()))
    {
      total +=
          table_row_count(
              connection.get(),
              table);
    }

    return total;
  }

  static bool database_contains_text(
      const std::filesystem::path &path,
      std::string_view expected)
  {
    SqliteConnection connection{
        path};

    for (const std::string &table :
         user_tables(connection.get()))
    {
      const std::string sql =
          "SELECT * FROM " +
          quote_identifier(table) +
          ";";

      sqlite3_stmt *statement = nullptr;

      const int prepareResult =
          sqlite3_prepare_v2(
              connection.get(),
              sql.c_str(),
              -1,
              &statement,
              nullptr);

      assert(prepareResult == SQLITE_OK);
      assert(statement != nullptr);

      bool found = false;

      while (!found)
      {
        const int stepResult =
            sqlite3_step(statement);

        if (stepResult == SQLITE_DONE)
        {
          break;
        }

        assert(stepResult == SQLITE_ROW);

        const int columnCount =
            sqlite3_column_count(
                statement);

        for (int column = 0;
             column < columnCount;
             ++column)
        {
          const unsigned char *value =
              sqlite3_column_text(
                  statement,
                  column);

          if (value == nullptr)
          {
            continue;
          }

          const std::string_view text{
              reinterpret_cast<
                  const char *>(value),
              static_cast<std::size_t>(
                  sqlite3_column_bytes(
                      statement,
                      column))};

          if (text.find(expected) !=
              std::string_view::npos)
          {
            found = true;
            break;
          }
        }
      }

      const int finalizeResult =
          sqlite3_finalize(statement);

      assert(finalizeResult == SQLITE_OK);

      if (found)
      {
        return true;
      }
    }

    return false;
  }

  static void test_append_is_available()
  {
    static_assert(
        requires(
            SqliteMessageStore &store,
            const JsonMessage &message) {
          store.append(message);
        });
  }

  static void test_append_adds_persistent_data()
  {
    TemporaryDatabase database{
        "vix_sqlite_store_append"};

    auto store =
        make_store<SqliteMessageStore>(
            database.path());

    const std::size_t before =
        total_row_count(
            database.path());

    const JsonMessage message =
        make_message(1u);

    append_message(
        store,
        message);

    const std::size_t after =
        total_row_count(
            database.path());

    assert(after > before);
  }

  static void test_append_persists_message_fields()
  {
    TemporaryDatabase database{
        "vix_sqlite_store_fields"};

    {
      auto store =
          make_store<SqliteMessageStore>(
              database.path());

      const JsonMessage message =
          make_message(
              42u,
              "room:general");

      append_message(
          store,
          message);
    }

    assert(
        database_contains_text(
            database.path(),
            "message-42"));

    assert(
        database_contains_text(
            database.path(),
            "event"));

    assert(
        database_contains_text(
            database.path(),
            "room:general"));

    assert(
        database_contains_text(
            database.path(),
            "chat.message.42"));

    assert(
        database_contains_text(
            database.path(),
            "2026-07-30"));
  }

  static void test_append_does_not_modify_message()
  {
    TemporaryDatabase database{
        "vix_sqlite_store_immutable"};

    auto store =
        make_store<SqliteMessageStore>(
            database.path());

    JsonMessage message =
        make_message(
            7u,
            "immutable-room");

    const std::string originalId =
        message.id;

    const std::string originalKind =
        message.kind;

    const std::string originalTimestamp =
        message.ts;

    const std::string originalRoom =
        message.room;

    const std::string originalType =
        message.type;

    append_message(
        store,
        message);

    assert(message.id == originalId);
    assert(message.kind == originalKind);
    assert(message.ts == originalTimestamp);
    assert(message.room == originalRoom);
    assert(message.type == originalType);
  }

  static void test_multiple_appends_add_multiple_rows()
  {
    TemporaryDatabase database{
        "vix_sqlite_store_multiple"};

    auto store =
        make_store<SqliteMessageStore>(
            database.path());

    const std::size_t initial =
        total_row_count(
            database.path());

    append_message(
        store,
        make_message(1u));

    const std::size_t afterFirst =
        total_row_count(
            database.path());

    append_message(
        store,
        make_message(2u));

    const std::size_t afterSecond =
        total_row_count(
            database.path());

    append_message(
        store,
        make_message(3u));

    const std::size_t afterThird =
        total_row_count(
            database.path());

    assert(afterFirst > initial);
    assert(afterSecond > afterFirst);
    assert(afterThird > afterSecond);
  }

  static void test_many_messages_can_be_appended()
  {
    TemporaryDatabase database{
        "vix_sqlite_store_many"};

    auto store =
        make_store<SqliteMessageStore>(
            database.path());

    const std::size_t before =
        total_row_count(
            database.path());

    constexpr std::size_t count = 100u;

    for (std::size_t index = 1u;
         index <= count;
         ++index)
    {
      append_message(
          store,
          make_message(index));
    }

    const std::size_t after =
        total_row_count(
            database.path());

    assert(after >= before + count);

    assert(
        database_contains_text(
            database.path(),
            "message-1"));

    assert(
        database_contains_text(
            database.path(),
            "message-100"));
  }

  static void test_messages_from_different_rooms_are_persisted()
  {
    TemporaryDatabase database{
        "vix_sqlite_store_rooms"};

    {
      auto store =
          make_store<SqliteMessageStore>(
              database.path());

      append_message(
          store,
          make_message(
              1u,
              "room-alpha"));

      append_message(
          store,
          make_message(
              2u,
              "room-beta"));

      append_message(
          store,
          make_message(
              3u,
              "room-gamma"));
    }

    assert(
        database_contains_text(
            database.path(),
            "room-alpha"));

    assert(
        database_contains_text(
            database.path(),
            "room-beta"));

    assert(
        database_contains_text(
            database.path(),
            "room-gamma"));
  }

  static void test_appended_data_survives_store_destruction()
  {
    TemporaryDatabase database{
        "vix_sqlite_store_persistence"};

    {
      auto store =
          make_store<SqliteMessageStore>(
              database.path());

      append_message(
          store,
          make_message(
              88u,
              "persistent-room"));
    }

    assert(
        database_contains_text(
            database.path(),
            "message-88"));

    assert(
        database_contains_text(
            database.path(),
            "persistent-room"));

    assert(
        database_contains_text(
            database.path(),
            "chat.message.88"));
  }

  static void test_append_after_reopening_database()
  {
    TemporaryDatabase database{
        "vix_sqlite_store_reopen_append"};

    {
      auto first =
          make_store<SqliteMessageStore>(
              database.path());

      append_message(
          first,
          make_message(1u));
    }

    const std::size_t afterFirst =
        total_row_count(
            database.path());

    {
      auto second =
          make_store<SqliteMessageStore>(
              database.path());

      append_message(
          second,
          make_message(2u));
    }

    const std::size_t afterSecond =
        total_row_count(
            database.path());

    assert(afterSecond > afterFirst);

    assert(
        database_contains_text(
            database.path(),
            "message-1"));

    assert(
        database_contains_text(
            database.path(),
            "message-2"));
  }

} // namespace

int main()
{
  test_append_is_available();

  test_append_adds_persistent_data();
  test_append_persists_message_fields();
  test_append_does_not_modify_message();

  test_multiple_appends_add_multiple_rows();
  test_many_messages_can_be_appended();

  test_messages_from_different_rooms_are_persisted();

  test_appended_data_survives_store_destruction();
  test_append_after_reopening_database();

  return 0;
}
