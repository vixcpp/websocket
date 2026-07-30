/**
 *
 * @file sqlite_message_store_constructor_test.cpp
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
#include <type_traits>
#include <utility>

#include <sqlite3.h>

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

  static std::string query_single_text(
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

  static std::size_t user_table_count(
      sqlite3 *database)
  {
    sqlite3_stmt *statement = nullptr;

    const char *sql =
        "SELECT COUNT(*) "
        "FROM sqlite_master "
        "WHERE type = 'table' "
        "AND name NOT LIKE 'sqlite_%';";

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

  static void assert_valid_database(
      const std::filesystem::path &path)
  {
    assert(
        std::filesystem::exists(path));

    assert(
        std::filesystem::is_regular_file(path));

    assert(
        std::filesystem::file_size(path) >
        0u);

    SqliteConnection connection{
        path};

    assert(
        query_single_text(
            connection.get(),
            "PRAGMA integrity_check;") ==
        "ok");

    assert(
        user_table_count(
            connection.get()) >=
        1u);
  }

  static void test_constructor_contract()
  {
    constexpr bool constructible =
        std::is_constructible_v<
            SqliteMessageStore,
            const std::filesystem::path &> ||
        std::is_constructible_v<
            SqliteMessageStore,
            std::filesystem::path> ||
        std::is_constructible_v<
            SqliteMessageStore,
            const std::string &> ||
        std::is_constructible_v<
            SqliteMessageStore,
            std::string> ||
        std::is_constructible_v<
            SqliteMessageStore,
            const char *>;

    static_assert(
        constructible,
        "SqliteMessageStore must accept a database path");

    static_assert(
        std::is_destructible_v<
            SqliteMessageStore>);
  }

  static void test_constructor_creates_database_file()
  {
    TemporaryDatabase database{
        "vix_sqlite_store_constructor"};

    assert(
        !std::filesystem::exists(
            database.path()));

    {
      auto store =
          make_store<SqliteMessageStore>(
              database.path());

      (void)store;

      assert(
          std::filesystem::exists(
              database.path()));
    }

    assert_valid_database(
        database.path());
  }

  static void test_constructor_initializes_schema()
  {
    TemporaryDatabase database{
        "vix_sqlite_store_schema"};

    {
      auto store =
          make_store<SqliteMessageStore>(
              database.path());

      (void)store;
    }

    SqliteConnection connection{
        database.path()};

    assert(
        user_table_count(
            connection.get()) >=
        1u);
  }

  static void test_constructor_can_reopen_existing_database()
  {
    TemporaryDatabase database{
        "vix_sqlite_store_reopen"};

    {
      auto first =
          make_store<SqliteMessageStore>(
              database.path());

      (void)first;
    }

    assert_valid_database(
        database.path());

    const auto originalSize =
        std::filesystem::file_size(
            database.path());

    {
      auto second =
          make_store<SqliteMessageStore>(
              database.path());

      (void)second;
    }

    assert_valid_database(
        database.path());

    assert(
        std::filesystem::file_size(
            database.path()) >=
        originalSize);
  }

  static void test_repeated_construction_keeps_schema_valid()
  {
    TemporaryDatabase database{
        "vix_sqlite_store_repeated"};

    for (std::size_t index = 0u;
         index < 10u;
         ++index)
    {
      {
        auto store =
            make_store<SqliteMessageStore>(
                database.path());

        (void)store;
      }

      assert_valid_database(
          database.path());
    }
  }

  static void test_separate_paths_create_separate_databases()
  {
    TemporaryDatabase firstDatabase{
        "vix_sqlite_store_first"};

    TemporaryDatabase secondDatabase{
        "vix_sqlite_store_second"};

    {
      auto first =
          make_store<SqliteMessageStore>(
              firstDatabase.path());

      auto second =
          make_store<SqliteMessageStore>(
              secondDatabase.path());

      (void)first;
      (void)second;
    }

    assert(
        firstDatabase.path() !=
        secondDatabase.path());

    assert_valid_database(
        firstDatabase.path());

    assert_valid_database(
        secondDatabase.path());
  }

  static void test_database_remains_valid_after_destruction()
  {
    TemporaryDatabase database{
        "vix_sqlite_store_destruction"};

    {
      auto store =
          make_store<SqliteMessageStore>(
              database.path());

      (void)store;
    }

    SqliteConnection connection{
        database.path()};

    assert(
        query_single_text(
            connection.get(),
            "PRAGMA quick_check;") ==
        "ok");
  }

} // namespace

int main()
{
  test_constructor_contract();

  test_constructor_creates_database_file();
  test_constructor_initializes_schema();

  test_constructor_can_reopen_existing_database();
  test_repeated_construction_keeps_schema_valid();

  test_separate_paths_create_separate_databases();
  test_database_remains_valid_after_destruction();

  return 0;
}
