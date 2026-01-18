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
   * @brief Abstraction de stockage pour les messages WebSocket.
   *
   * Pensé pour être implémenté avec SQLite, Postgres, Redis, etc.
   *
   * Sémantique attendue :
   *  - append(msg) : persiste un message (id, kind, room, type, ts, payload)
   *  - list_by_room(room, limit, before_id) :
   *      → retourne les derniers messages d'une room
   *      → ordonnés du plus récent au plus ancien (newest-first)
   *      → si before_id est défini, ne retourne que les messages STRICTEMENT plus anciens.
   *  - replay_from(id, limit) :
   *      → retourne les messages avec id STRICTEMENT > id
   *      → ordonnés du plus ancien au plus récent (oldest-first)
   */
  class IMessageStore
  {
  public:
    virtual ~IMessageStore() = default;
    virtual void append(const JsonMessage &msg) = 0;
    virtual std::vector<JsonMessage> list_by_room(
        const std::string &room,
        std::size_t limit,
        const std::optional<std::string> &before_id = std::nullopt) = 0;
    virtual std::vector<JsonMessage> replay_from(
        const std::string &start_id,
        std::size_t limit) = 0;
  };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_MESSAGE_STORE_HPP
