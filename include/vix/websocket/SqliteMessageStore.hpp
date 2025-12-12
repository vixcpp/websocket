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
    class SqliteMessageStore : public IMessageStore
    {
    public:
        explicit SqliteMessageStore(const std::string &db_path);
        ~SqliteMessageStore() override;

        SqliteMessageStore(const SqliteMessageStore &) = delete;
        SqliteMessageStore &operator=(const SqliteMessageStore &) = delete;
        SqliteMessageStore(SqliteMessageStore &&) = delete;
        SqliteMessageStore &operator=(SqliteMessageStore &&) = delete;

        void append(const JsonMessage &msg) override;

        [[nodiscard]] std::vector<JsonMessage> list_by_room(
            const std::string &room,
            std::size_t limit,
            const std::optional<std::string> &before_id = std::nullopt) override;

        [[nodiscard]] std::vector<JsonMessage> replay_from(
            const std::string &start_id,
            std::size_t limit) override;

    private:
        sqlite3 *db_{nullptr};

        void init_schema();
        static std::string generate_id();
    };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_SQLITE_MESSAGE_STORE_HPP
