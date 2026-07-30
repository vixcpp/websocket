/**
 *
 * @file sqlite_message_store_concurrency_test.cpp
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
#include <exception>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_set>
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

  static void require(
      bool condition,
      const char *message)
  {
    if (!condition)
    {
      throw std::runtime_error{
          message};
    }
  }

  static std::string message_id(
      std::size_t value)
  {
    std::ostringstream output;

    output
        << std::setw(20)
        << std::setfill('0')
        << value;

    return output.str();
  }

  static JsonMessage make_message(
      std::size_t id,
      std::string room,
      std::string type)
  {
    JsonMessage message;

    message.id =
        message_id(id);

    message.kind = "event";

    message.ts =
        "2026-07-30T14:00:00Z";

    message.room =
        std::move(room);

    message.type =
        std::move(type);

    return message;
  }

  template <typename Function>
  static void run_concurrently(
      std::size_t threadCount,
      Function function)
  {
    assert(threadCount > 0u);

    std::atomic<std::size_t> ready{
        0u};

    std::atomic<bool> start{
        false};

    std::vector<std::exception_ptr>
        errors(threadCount);

    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    for (std::size_t threadIndex = 0u;
         threadIndex < threadCount;
         ++threadIndex)
    {
      threads.emplace_back(
          [threadIndex,
           &ready,
           &start,
           &errors,
           &function]()
          {
            ready.fetch_add(
                1u,
                std::memory_order_release);

            while (!start.load(
                std::memory_order_acquire))
            {
              std::this_thread::yield();
            }

            try
            {
              function(threadIndex);
            }
            catch (...)
            {
              errors[threadIndex] =
                  std::current_exception();
            }
          });
    }

    while (ready.load(
               std::memory_order_acquire) <
           threadCount)
    {
      std::this_thread::yield();
    }

    start.store(
        true,
        std::memory_order_release);

    for (std::thread &thread :
         threads)
    {
      thread.join();
    }

    for (const std::exception_ptr &error :
         errors)
    {
      if (error)
      {
        std::rethrow_exception(error);
      }
    }
  }

  static void assert_unique_messages(
      const std::vector<JsonMessage> &messages,
      std::size_t expectedCount,
      const std::string &expectedRoom)
  {
    assert(
        messages.size() ==
        expectedCount);

    std::unordered_set<std::string> ids;
    ids.reserve(messages.size());

    for (const JsonMessage &message :
         messages)
    {
      assert(
          message.room ==
          expectedRoom);

      assert(!message.id.empty());

      const bool inserted =
          ids.insert(
                 message.id)
              .second;

      assert(inserted);
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

  static void test_api_contract()
  {
    static_assert(
        std::is_constructible_v<
            SqliteMessageStore,
            const std::string &>);

    static_assert(
        std::is_same_v<
            decltype(std::declval<
                         SqliteMessageStore &>()
                         .append(
                             std::declval<
                                 const JsonMessage &>())),
            void>);

    static_assert(
        !std::is_copy_constructible_v<
            SqliteMessageStore>);

    static_assert(
        !std::is_copy_assignable_v<
            SqliteMessageStore>);
  }

  static void test_sqlite_build_is_threadsafe()
  {
    assert(
        sqlite3_threadsafe() !=
        0);
  }

  static void test_concurrent_appends_to_distinct_rooms()
  {
    TemporaryDatabase database{
        "vix_sqlite_concurrency_rooms"};

    SqliteMessageStore store{
        database.path()};

    constexpr std::size_t threadCount = 6u;
    constexpr std::size_t messagesPerThread = 40u;

    run_concurrently(
        threadCount,
        [&store](
            std::size_t threadIndex)
        {
          const std::string room =
              "room-" +
              std::to_string(threadIndex);

          for (std::size_t index = 0u;
               index < messagesPerThread;
               ++index)
          {
            const std::size_t id =
                100000u +
                threadIndex *
                    messagesPerThread +
                index;

            store.append(
                make_message(
                    id,
                    room,
                    "distinct-room-message"));
          }
        });

    for (std::size_t threadIndex = 0u;
         threadIndex < threadCount;
         ++threadIndex)
    {
      const std::string room =
          "room-" +
          std::to_string(threadIndex);

      const auto messages =
          store.list_by_room(
              room,
              messagesPerThread + 10u);

      assert_unique_messages(
          messages,
          messagesPerThread,
          room);
    }

    const auto replay =
        store.replay_from(
            message_id(0u),
            threadCount *
                    messagesPerThread +
                10u);

    assert(
        replay.size() ==
        threadCount *
            messagesPerThread);
  }

  static void test_concurrent_appends_to_same_room()
  {
    TemporaryDatabase database{
        "vix_sqlite_concurrency_same_room"};

    SqliteMessageStore store{
        database.path()};

    constexpr std::size_t threadCount = 8u;
    constexpr std::size_t messagesPerThread = 50u;

    run_concurrently(
        threadCount,
        [&store](
            std::size_t threadIndex)
        {
          for (std::size_t index = 0u;
               index < messagesPerThread;
               ++index)
          {
            const std::size_t id =
                200000u +
                threadIndex *
                    messagesPerThread +
                index;

            store.append(
                make_message(
                    id,
                    "general",
                    "same-room-message"));
          }
        });

    const auto messages =
        store.list_by_room(
            "general",
            threadCount *
                    messagesPerThread +
                10u);

    assert_unique_messages(
        messages,
        threadCount *
            messagesPerThread,
        "general");
  }

  static void test_concurrent_readers_on_single_store()
  {
    TemporaryDatabase database{
        "vix_sqlite_concurrency_readers"};

    SqliteMessageStore store{
        database.path()};

    constexpr std::size_t messageCount = 200u;

    for (std::size_t index = 1u;
         index <= messageCount;
         ++index)
    {
      store.append(
          make_message(
              300000u + index,
              "general",
              "reader-message"));
    }

    constexpr std::size_t readerCount = 8u;
    constexpr std::size_t iterations = 100u;

    run_concurrently(
        readerCount,
        [&store](
            std::size_t)
        {
          for (std::size_t iteration = 0u;
               iteration < iterations;
               ++iteration)
          {
            const auto history =
                store.list_by_room(
                    "general",
                    messageCount);

            require(
                history.size() ==
                    messageCount,
                "concurrent room read returned an invalid size");

            for (const JsonMessage &message :
                 history)
            {
              require(
                  message.room ==
                      "general",
                  "concurrent room read returned another room");
            }

            const auto replay =
                store.replay_from(
                    message_id(300000u),
                    messageCount);

            require(
                replay.size() ==
                    messageCount,
                "concurrent replay returned an invalid size");
          }
        });
  }

  static void test_concurrent_reads_and_writes_on_single_store()
  {
    TemporaryDatabase database{
        "vix_sqlite_concurrency_single_store_rw"};

    SqliteMessageStore store{
        database.path()};

    constexpr std::size_t writerMessages = 250u;
    constexpr std::size_t readerCount = 4u;
    constexpr std::size_t readerIterations = 150u;

    run_concurrently(
        readerCount + 1u,
        [&store](
            std::size_t threadIndex)
        {
          if (threadIndex == 0u)
          {
            for (std::size_t index = 1u;
                 index <= writerMessages;
                 ++index)
            {
              store.append(
                  make_message(
                      400000u + index,
                      "general",
                      "concurrent-write"));

              if (index % 10u == 0u)
              {
                std::this_thread::yield();
              }
            }

            return;
          }

          for (std::size_t iteration = 0u;
               iteration < readerIterations;
               ++iteration)
          {
            const auto history =
                store.list_by_room(
                    "general",
                    writerMessages);

            require(
                history.size() <=
                    writerMessages,
                "room history exceeded the written message count");

            for (const JsonMessage &message :
                 history)
            {
              require(
                  message.room ==
                      "general",
                  "room history returned an invalid room");
            }

            const auto replay =
                store.replay_from(
                    message_id(400000u),
                    writerMessages);

            require(
                replay.size() <=
                    writerMessages,
                "replay exceeded the written message count");

            std::this_thread::yield();
          }
        });

    const auto finalHistory =
        store.list_by_room(
            "general",
            writerMessages + 10u);

    assert_unique_messages(
        finalHistory,
        writerMessages,
        "general");
  }

  static void test_wal_one_writer_and_multiple_connections()
  {
    TemporaryDatabase database{
        "vix_sqlite_concurrency_wal"};

    const std::string path =
        database.path();

    auto writer =
        std::make_unique<
            SqliteMessageStore>(
            path);

    constexpr std::size_t readerCount = 4u;

    std::vector<
        std::unique_ptr<
            SqliteMessageStore>>
        readers;

    readers.reserve(readerCount);

    for (std::size_t index = 0u;
         index < readerCount;
         ++index)
    {
      readers.push_back(
          std::make_unique<
              SqliteMessageStore>(
              path));
    }

    constexpr std::size_t writerMessages = 200u;
    constexpr std::size_t readerIterations = 120u;

    run_concurrently(
        readerCount + 1u,
        [&writer, &readers](
            std::size_t threadIndex)
        {
          if (threadIndex == 0u)
          {
            for (std::size_t index = 1u;
                 index <= writerMessages;
                 ++index)
            {
              writer->append(
                  make_message(
                      500000u + index,
                      "general",
                      "wal-writer-message"));

              if (index % 5u == 0u)
              {
                std::this_thread::yield();
              }
            }

            return;
          }

          SqliteMessageStore &reader =
              *readers[threadIndex - 1u];

          for (std::size_t iteration = 0u;
               iteration < readerIterations;
               ++iteration)
          {
            const auto messages =
                reader.list_by_room(
                    "general",
                    writerMessages);

            require(
                messages.size() <=
                    writerMessages,
                "WAL reader returned too many messages");

            for (const JsonMessage &message :
                 messages)
            {
              require(
                  message.room ==
                      "general",
                  "WAL reader returned another room");
            }

            std::this_thread::yield();
          }
        });

    const auto messages =
        writer->list_by_room(
            "general",
            writerMessages + 10u);

    assert_unique_messages(
        messages,
        writerMessages,
        "general");
  }

  static void test_concurrent_replace_keeps_single_row()
  {
    TemporaryDatabase database{
        "vix_sqlite_concurrency_replace"};

    SqliteMessageStore store{
        database.path()};

    constexpr std::size_t threadCount = 8u;
    constexpr std::size_t replacementsPerThread = 50u;

    const std::string sharedId =
        message_id(600000u);

    run_concurrently(
        threadCount,
        [&store, &sharedId](
            std::size_t threadIndex)
        {
          for (std::size_t index = 0u;
               index < replacementsPerThread;
               ++index)
          {
            JsonMessage message;

            message.id = sharedId;
            message.kind = "event";
            message.ts = "2026-07-30T14:00:00Z";
            message.room = "replace-room";

            message.type =
                "thread-" +
                std::to_string(threadIndex) +
                "-replacement-" +
                std::to_string(index);

            store.append(message);
          }
        });

    const auto messages =
        store.list_by_room(
            "replace-room",
            10u);

    assert(messages.size() == 1u);

    assert(
        messages[0].id ==
        sharedId);

    assert(
        messages[0].room ==
        "replace-room");

    assert(
        messages[0].type.find(
            "thread-") ==
        0u);
  }

  static void test_concurrent_data_persists_after_reopen()
  {
    TemporaryDatabase database{
        "vix_sqlite_concurrency_persistence"};

    const std::string path =
        database.path();

    constexpr std::size_t threadCount = 6u;
    constexpr std::size_t messagesPerThread = 50u;

    {
      SqliteMessageStore store{
          path};

      run_concurrently(
          threadCount,
          [&store](
              std::size_t threadIndex)
          {
            for (std::size_t index = 0u;
                 index < messagesPerThread;
                 ++index)
            {
              const std::size_t id =
                  700000u +
                  threadIndex *
                      messagesPerThread +
                  index;

              store.append(
                  make_message(
                      id,
                      "persistent-room",
                      "persistent-concurrent-message"));
            }
          });
    }

    assert(
        std::filesystem::exists(
            database.filesystem_path()));

    {
      SqliteConnection connection{
          database.filesystem_path()};

      assert(
          query_text(
              connection.get(),
              "PRAGMA integrity_check;") ==
          "ok");

      assert(
          query_count(
              connection.get(),
              "SELECT COUNT(*) FROM messages;") ==
          threadCount *
              messagesPerThread);
    }

    {
      SqliteMessageStore reopened{
          path};

      const auto messages =
          reopened.list_by_room(
              "persistent-room",
              threadCount *
                      messagesPerThread +
                  10u);

      assert_unique_messages(
          messages,
          threadCount *
              messagesPerThread,
          "persistent-room");

      const auto replay =
          reopened.replay_from(
              message_id(0u),
              threadCount *
                      messagesPerThread +
                  10u);

      assert(
          replay.size() ==
          threadCount *
              messagesPerThread);
    }
  }

} // namespace

int main()
{
  test_api_contract();
  test_sqlite_build_is_threadsafe();

  test_concurrent_appends_to_distinct_rooms();
  test_concurrent_appends_to_same_room();

  test_concurrent_readers_on_single_store();
  test_concurrent_reads_and_writes_on_single_store();

  test_wal_one_writer_and_multiple_connections();

  test_concurrent_replace_keeps_single_row();
  test_concurrent_data_persists_after_reopen();

  return 0;
}
