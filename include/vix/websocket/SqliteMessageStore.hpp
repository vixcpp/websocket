/**
 *
 *  @file SqliteMessageStore.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#ifndef VIX_WEBSOCKET_SQLITE_MESSAGE_STORE_HPP
#define VIX_WEBSOCKET_SQLITE_MESSAGE_STORE_HPP

#include <string>
#include <vector>
#include <optional>

#include <vix/websocket/MessageStore.hpp>
#include <vix/websocket/protocol.hpp>

struct sqlite3;

namespace vix::websocket
{
  /**
   * @brief SQLite-backed persistent message store.
   *
   * Stores WebSocket JsonMessage objects in a SQLite database,
   * designed for history replay, room-based queries, and WAL-friendly persistence.
   */
  class SqliteMessageStore : public IMessageStore
  {
  public:
    /**
     * @brief Open or create a SQLite message store.
     *
     * @param db_path Path to the SQLite database file.
     */
    explicit SqliteMessageStore(const std::string &db_path);

    ~SqliteMessageStore() override;

    SqliteMessageStore(const SqliteMessageStore &) = delete;
    SqliteMessageStore &operator=(const SqliteMessageStore &) = delete;
    SqliteMessageStore(SqliteMessageStore &&) = delete;
    SqliteMessageStore &operator=(SqliteMessageStore &&) = delete;

    /** @brief Persist a message into the database. */
    void append(const JsonMessage &msg) override;

    /**
     * @brief List messages for a given room.
     *
     * @param room Room identifier.
     * @param limit Maximum number of messages.
     * @param before_id Optional cursor for pagination.
     */
    [[nodiscard]] std::vector<JsonMessage> list_by_room(
        const std::string &room,
        std::size_t limit,
        const std::optional<std::string> &before_id = std::nullopt) override;

    /**
     * @brief Replay messages starting from a given message id.
     *
     * @param start_id Message identifier to start from.
     * @param limit Maximum number of messages.
     */
    [[nodiscard]] std::vector<JsonMessage> replay_from(
        const std::string &start_id,
        std::size_t limit) override;

  private:
    sqlite3 *db_{nullptr};

    /** @brief Initialize database schema if missing. */
    void init_schema();

    /** @brief Generate a unique message identifier. */
    static std::string generate_id();
  };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_SQLITE_MESSAGE_STORE_HPP
