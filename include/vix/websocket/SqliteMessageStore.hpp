#ifndef VIX_WEBSOCKET_SQLITE_MESSAGE_STORE_HPP
#define VIX_WEBSOCKET_SQLITE_MESSAGE_STORE_HPP

#include <string>
#include <vector>
#include <optional>

#include <sqlite3.h>

#include <vix/websocket/MessageStore.hpp>
#include <vix/websocket/protocol.hpp>

namespace vix::websocket
{
    /**
     * @brief Implémentation SQLite de IMessageStore, avec WAL activé.
     *
     * Schéma:
     *
     *   CREATE TABLE IF NOT EXISTS messages (
     *       id           TEXT PRIMARY KEY,
     *       kind         TEXT NOT NULL,
     *       room         TEXT,
     *       type         TEXT NOT NULL,
     *       ts           TEXT NOT NULL,
     *       payload_json TEXT NOT NULL
     *   );
     *
     * - journal_mode = WAL
     * - id est une string lexicographiquement ordonnée (zéro-padding)
     */
    class SqliteMessageStore : public IMessageStore
    {
    public:
        explicit SqliteMessageStore(const std::string &db_path);
        ~SqliteMessageStore() override;

        void append(const JsonMessage &msg) override;

        std::vector<JsonMessage> list_by_room(
            const std::string &room,
            std::size_t limit,
            const std::optional<std::string> &before_id = std::nullopt) override;

        std::vector<JsonMessage> replay_from(
            const std::string &start_id,
            std::size_t limit) override;

    private:
        sqlite3 *db_{nullptr};

        void init_schema();
        static std::string generate_id();
    };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_SQLITE_MESSAGE_STORE_HPP
