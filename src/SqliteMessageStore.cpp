#include <vix/websocket/SqliteMessageStore.hpp>

#include <stdexcept>
#include <chrono>
#include <sstream>
#include <iomanip>

#include <nlohmann/json.hpp>

namespace vix::websocket
{
    using namespace vix::websocket::detail; // ws_kvs_to_nlohmann / nlohmann_payload_to_kvs

    // ───────────────────────── Helpers internes ─────────────────────────

    static void sqlite_check(int rc, sqlite3 *db, const char *stage)
    {
        if (rc != SQLITE_OK && rc != SQLITE_ROW && rc != SQLITE_DONE)
        {
            std::string msg = "[SqliteMessageStore] ";
            msg += stage;
            msg += " error: ";
            msg += sqlite3_errmsg(db);
            throw std::runtime_error(msg);
        }
    }

    // ───────────────────────── Ctor / Dtor ─────────────────────────

    SqliteMessageStore::SqliteMessageStore(const std::string &db_path)
    {
        int rc = sqlite3_open(db_path.c_str(), &db_);
        if (rc != SQLITE_OK)
        {
            std::string msg = "[SqliteMessageStore] Failed to open DB: ";
            msg += sqlite3_errstr(rc);
            throw std::runtime_error(msg);
        }

        // Activer WAL
        {
            char *errmsg = nullptr;
            rc = sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &errmsg);
            if (rc != SQLITE_OK)
            {
                std::string msg = "[SqliteMessageStore] Failed to set WAL: ";
                if (errmsg)
                {
                    msg += errmsg;
                    sqlite3_free(errmsg);
                }
                throw std::runtime_error(msg);
            }
        }

        init_schema();
    }

    SqliteMessageStore::~SqliteMessageStore()
    {
        if (db_)
        {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }

    void SqliteMessageStore::init_schema()
    {
        const char *sql =
            "CREATE TABLE IF NOT EXISTS messages ("
            "  id           TEXT PRIMARY KEY,"
            "  kind         TEXT NOT NULL,"
            "  room         TEXT,"
            "  type         TEXT NOT NULL,"
            "  ts           TEXT NOT NULL,"
            "  payload_json TEXT NOT NULL"
            ");";

        char *errmsg = nullptr;
        int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &errmsg);
        if (rc != SQLITE_OK)
        {
            std::string msg = "[SqliteMessageStore] Failed to create table: ";
            if (errmsg)
            {
                msg += errmsg;
                sqlite3_free(errmsg);
            }
            throw std::runtime_error(msg);
        }
    }

    // ───────────────────────── ID helper ─────────────────────────

    std::string SqliteMessageStore::generate_id()
    {
        // ID basé sur le temps en microsecondes, zéro-paddé pour ordre lexicographique.
        using clock = std::chrono::system_clock;
        auto now = clock::now().time_since_epoch();
        auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now).count();

        std::ostringstream oss;
        oss << std::setw(20) << std::setfill('0') << micros;
        return oss.str();
    }

    // ───────────────────────── append() ─────────────────────────

    void SqliteMessageStore::append(const JsonMessage &msg)
    {
        // On construit une copie avec id/ts/kind/room normalisés
        JsonMessage m = msg;

        if (m.id.empty())
        {
            m.id = generate_id();
        }

        if (m.ts.empty())
        {
            // ISO-8601 très simple en UTC : YYYY-MM-DDTHH:MM:SSZ
            using clock = std::chrono::system_clock;
            auto now = clock::now();
            auto tt = clock::to_time_t(now);
            std::tm tm{};
#if defined(_WIN32)
            gmtime_s(&tm, &tt);
#else
            gmtime_r(&tt, &tm);
#endif
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                          tm.tm_year + 1900,
                          tm.tm_mon + 1,
                          tm.tm_mday,
                          tm.tm_hour,
                          tm.tm_min,
                          tm.tm_sec);
            m.ts = buf;
        }

        if (m.kind.empty())
        {
            m.kind = "event";
        }

        // On sérialise uniquement payload en JSON texte pour la colonne payload_json
        nlohmann::json payloadJson = ws_kvs_to_nlohmann(m.payload);
        std::string payloadText = payloadJson.dump();

        const char *sql =
            "INSERT OR REPLACE INTO messages "
            "(id, kind, room, type, ts, payload_json) "
            "VALUES (?, ?, ?, ?, ?, ?);";

        sqlite3_stmt *stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        sqlite_check(rc, db_, "prepare append");

        rc = sqlite3_bind_text(stmt, 1, m.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite_check(rc, db_, "bind id");

        rc = sqlite3_bind_text(stmt, 2, m.kind.c_str(), -1, SQLITE_TRANSIENT);
        sqlite_check(rc, db_, "bind kind");

        if (m.room.empty())
        {
            rc = sqlite3_bind_null(stmt, 3);
        }
        else
        {
            rc = sqlite3_bind_text(stmt, 3, m.room.c_str(), -1, SQLITE_TRANSIENT);
        }
        sqlite_check(rc, db_, "bind room");

        rc = sqlite3_bind_text(stmt, 4, m.type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite_check(rc, db_, "bind type");

        rc = sqlite3_bind_text(stmt, 5, m.ts.c_str(), -1, SQLITE_TRANSIENT);
        sqlite_check(rc, db_, "bind ts");

        rc = sqlite3_bind_text(stmt, 6, payloadText.c_str(), -1, SQLITE_TRANSIENT);
        sqlite_check(rc, db_, "bind payload");

        rc = sqlite3_step(stmt);
        sqlite_check(rc, db_, "step append");

        sqlite3_finalize(stmt);
    }

    // ───────────────────────── list_by_room() ─────────────────────────

    std::vector<JsonMessage> SqliteMessageStore::list_by_room(
        const std::string &room,
        std::size_t limit,
        const std::optional<std::string> &before_id)
    {
        std::vector<JsonMessage> out;
        if (limit == 0)
            return out;

        const char *sql_base =
            "SELECT id, kind, room, type, ts, payload_json "
            "FROM messages "
            "WHERE room = ?1 ";

        // newest-first, avec pagination optionnelle sur id
        std::string sql;
        if (before_id.has_value())
        {
            sql = std::string(sql_base) +
                  "AND id < ?2 "
                  "ORDER BY id DESC "
                  "LIMIT ?3;";
        }
        else
        {
            sql = std::string(sql_base) +
                  "ORDER BY id DESC "
                  "LIMIT ?2;";
        }

        sqlite3_stmt *stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        sqlite_check(rc, db_, "prepare list_by_room");

        rc = sqlite3_bind_text(stmt, 1, room.c_str(), -1, SQLITE_TRANSIENT);
        sqlite_check(rc, db_, "bind room");

        if (before_id.has_value())
        {
            rc = sqlite3_bind_text(stmt, 2, before_id->c_str(), -1, SQLITE_TRANSIENT);
            sqlite_check(rc, db_, "bind before_id");

            rc = sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(limit));
            sqlite_check(rc, db_, "bind limit");
        }
        else
        {
            rc = sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(limit));
            sqlite_check(rc, db_, "bind limit");
        }

        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            JsonMessage m;

            const unsigned char *id = sqlite3_column_text(stmt, 0);
            const unsigned char *kind = sqlite3_column_text(stmt, 1);
            const unsigned char *room_col = sqlite3_column_text(stmt, 2);
            const unsigned char *type = sqlite3_column_text(stmt, 3);
            const unsigned char *ts = sqlite3_column_text(stmt, 4);
            const unsigned char *payload_json = sqlite3_column_text(stmt, 5);

            if (id)
                m.id = reinterpret_cast<const char *>(id);
            if (kind)
                m.kind = reinterpret_cast<const char *>(kind);
            if (room_col)
                m.room = reinterpret_cast<const char *>(room_col);
            if (type)
                m.type = reinterpret_cast<const char *>(type);
            if (ts)
                m.ts = reinterpret_cast<const char *>(ts);

            if (payload_json)
            {
                try
                {
                    auto pj = nlohmann::json::parse(
                        reinterpret_cast<const char *>(payload_json));
                    m.payload = nlohmann_payload_to_kvs(pj);
                }
                catch (...)
                {
                    // payload invalide → payload vide
                    m.payload = vix::json::kvs{};
                }
            }

            out.push_back(std::move(m));
        }

        if (rc != SQLITE_DONE)
        {
            sqlite_check(rc, db_, "step list_by_room");
        }

        sqlite3_finalize(stmt);
        return out; // newest-first
    }

    // ───────────────────────── replay_from() ─────────────────────────

    std::vector<JsonMessage> SqliteMessageStore::replay_from(
        const std::string &start_id,
        std::size_t limit)
    {
        std::vector<JsonMessage> out;
        if (limit == 0)
            return out;

        const char *sql =
            "SELECT id, kind, room, type, ts, payload_json "
            "FROM messages "
            "WHERE id > ?1 "
            "ORDER BY id ASC "
            "LIMIT ?2;";

        sqlite3_stmt *stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        sqlite_check(rc, db_, "prepare replay_from");

        rc = sqlite3_bind_text(stmt, 1, start_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite_check(rc, db_, "bind start_id");

        rc = sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(limit));
        sqlite_check(rc, db_, "bind limit");

        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            JsonMessage m;

            const unsigned char *id = sqlite3_column_text(stmt, 0);
            const unsigned char *kind = sqlite3_column_text(stmt, 1);
            const unsigned char *room_col = sqlite3_column_text(stmt, 2);
            const unsigned char *type = sqlite3_column_text(stmt, 3);
            const unsigned char *ts = sqlite3_column_text(stmt, 4);
            const unsigned char *payload_json = sqlite3_column_text(stmt, 5);

            if (id)
                m.id = reinterpret_cast<const char *>(id);
            if (kind)
                m.kind = reinterpret_cast<const char *>(kind);
            if (room_col)
                m.room = reinterpret_cast<const char *>(room_col);
            if (type)
                m.type = reinterpret_cast<const char *>(type);
            if (ts)
                m.ts = reinterpret_cast<const char *>(ts);

            if (payload_json)
            {
                try
                {
                    auto pj = nlohmann::json::parse(
                        reinterpret_cast<const char *>(payload_json));
                    m.payload = nlohmann_payload_to_kvs(pj);
                }
                catch (...)
                {
                    m.payload = vix::json::kvs{};
                }
            }

            out.push_back(std::move(m));
        }

        if (rc != SQLITE_DONE)
        {
            sqlite_check(rc, db_, "step replay_from");
        }

        sqlite3_finalize(stmt);
        return out; // oldest-first
    }

} // namespace vix::websocket
