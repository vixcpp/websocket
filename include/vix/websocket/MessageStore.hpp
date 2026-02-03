/**
 *
 *  @file MessageStore.hpp
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
#ifndef VIX_WEBSOCKET_MESSAGE_STORE_HPP
#define VIX_WEBSOCKET_MESSAGE_STORE_HPP

#include <string>
#include <vector>
#include <optional>

#include <vix/websocket/protocol.hpp>

namespace vix::websocket
{
  /**
   * @brief Abstract persistence interface for JsonMessage history.
   *
   * Provides an optional backend for chat history, replay, and room queries.
   * Implementations may use SQLite/WAL, files, or any durable store.
   */
  class IMessageStore
  {
  public:
    virtual ~IMessageStore() = default;

    /** @brief Append a message to the store. */
    virtual void append(const JsonMessage &msg) = 0;

    /**
     * @brief List messages for a room (newest-first or store-defined ordering).
     *
     * @param room Room identifier.
     * @param limit Maximum number of messages to return.
     * @param before_id Optional cursor for pagination.
     */
    virtual std::vector<JsonMessage> list_by_room(
        const std::string &room,
        std::size_t limit,
        const std::optional<std::string> &before_id = std::nullopt) = 0;

    /**
     * @brief Replay messages starting from a given message id.
     *
     * @param start_id Cursor id to start from (store-defined semantics).
     * @param limit Maximum number of messages to return.
     */
    virtual std::vector<JsonMessage> replay_from(
        const std::string &start_id,
        std::size_t limit) = 0;
  };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_MESSAGE_STORE_HPP
