#ifndef VIX_WEBSOCKET_MESSAGE_STORE_HPP
#define VIX_WEBSOCKET_MESSAGE_STORE_HPP

#include <string>
#include <vector>
#include <optional>

#include <vix/websocket/protocol.hpp> // JsonMessage

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

        /// Append / upsert d'un message.
        virtual void append(const JsonMessage &msg) = 0;

        /// Liste les messages d'une room, "newest-first".
        virtual std::vector<JsonMessage> list_by_room(
            const std::string &room,
            std::size_t limit,
            const std::optional<std::string> &before_id = std::nullopt) = 0;

        /// Replay global à partir d'un id (strictement après).
        virtual std::vector<JsonMessage> replay_from(
            const std::string &start_id,
            std::size_t limit) = 0;
    };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_MESSAGE_STORE_HPP
