#ifndef VIX_WEBSOCKET_PROTOCOL_HPP
#define VIX_WEBSOCKET_PROTOCOL_HPP

/**
 * @file protocol.hpp
 * @brief JSON protocol helpers for WebSocket messages.
 *
 * Protocole standardisé pour préparer la persistance (SQLite + WAL).
 *
 * Format JSON sur le fil :
 *
 * {
 *   "id":     "msg-123",                    // optional, string
 *   "kind":   "event" | "system" | "error", // optional, default "event"
 *   "ts":     "2025-12-07T10:15:30Z",       // optional, ISO-8601 UTC
 *   "room":   "africa",                     // optional
 *   "type":   "chat.message",               // required (logique métier)
 *   "payload": { ... }                      // arbitrary key/values (vix::json::kvs)
 * }
 *
 * Public API :
 *   - JsonMessage::parse(text)
 *   - JsonMessage::serialize(...)
 *   - Helpers get_string(), get<T>() sur payload
 *
 * NOTE:
 *   - Les champs id / kind / ts / room peuvent être ignorés par les applis simples.
 *   - Pour WAL / SQLite:
 *       columns: id, room, kind, type, ts, payload_json
 */

#include <string>
#include <string_view>
#include <optional>

#include <nlohmann/json.hpp>
#include <vix/json/Simple.hpp> // vix::json::token / kvs

namespace vix::websocket
{
    namespace detail
    {
        inline nlohmann::json ws_token_to_nlohmann(const vix::json::token &t)
        {
            nlohmann::json j = nullptr;
            std::visit(
                [&](auto &&val)
                {
                    using T = std::decay_t<decltype(val)>;
                    if constexpr (std::is_same_v<T, std::monostate>)
                    {
                        j = nullptr;
                    }
                    else if constexpr (std::is_same_v<T, bool> ||
                                       std::is_same_v<T, long long> ||
                                       std::is_same_v<T, double> ||
                                       std::is_same_v<T, std::string>)
                    {
                        j = val;
                    }
                    else if constexpr (std::is_same_v<T, std::shared_ptr<vix::json::array_t>>)
                    {
                        if (!val)
                        {
                            j = nullptr;
                            return;
                        }
                        j = nlohmann::json::array();
                        for (const auto &el : val->elems)
                        {
                            j.push_back(ws_token_to_nlohmann(el));
                        }
                    }
                    else if constexpr (std::is_same_v<T, std::shared_ptr<vix::json::kvs>>)
                    {
                        if (!val)
                        {
                            j = nullptr;
                            return;
                        }
                        nlohmann::json obj = nlohmann::json::object();
                        const auto &a = val->flat;
                        const size_t n = a.size() - (a.size() % 2);
                        for (size_t i = 0; i < n; i += 2)
                        {
                            const auto &k = a[i].v;
                            const auto &vv = a[i + 1];
                            if (!std::holds_alternative<std::string>(k))
                                continue;
                            const auto &key = std::get<std::string>(k);
                            obj[key] = ws_token_to_nlohmann(vv);
                        }
                        j = std::move(obj);
                    }
                    else
                    {
                        j = nullptr;
                    }
                },
                t.v);
            return j;
        }

        inline nlohmann::json ws_kvs_to_nlohmann(const vix::json::kvs &list)
        {
            nlohmann::json obj = nlohmann::json::object();
            const auto &a = list.flat;
            const size_t n = a.size() - (a.size() % 2);

            for (size_t i = 0; i < n; i += 2)
            {
                const auto &k = a[i].v;
                const auto &v = a[i + 1];

                if (!std::holds_alternative<std::string>(k))
                    continue;
                const std::string &key = std::get<std::string>(k);

                obj[key] = ws_token_to_nlohmann(v);
            }
            return obj;
        }

        inline vix::json::kvs nlohmann_payload_to_kvs(const nlohmann::json &payload)
        {
            vix::json::kvs kv;

            if (!payload.is_object())
                return kv;

            for (auto it = payload.begin(); it != payload.end(); ++it)
            {
                const std::string key = it.key();
                const nlohmann::json &val = *it;

                // key
                kv.flat.emplace_back(vix::json::token{key});

                // value
                if (val.is_string())
                {
                    kv.flat.emplace_back(vix::json::token{val.get<std::string>()});
                }
                else if (val.is_boolean())
                {
                    kv.flat.emplace_back(vix::json::token{val.get<bool>()});
                }
                else if (val.is_number_integer())
                {
                    kv.flat.emplace_back(vix::json::token{val.get<long long>()});
                }
                else if (val.is_number_float())
                {
                    kv.flat.emplace_back(vix::json::token{val.get<double>()});
                }
                else if (val.is_null())
                {
                    kv.flat.emplace_back(vix::json::token{}); // monostate
                }
                else
                {
                    // Complex types (arrays / objects) → you can adapt later
                    kv.flat.emplace_back(vix::json::token{}); // placeholder
                }
            }

            return kv;
        }

    } // namespace detail

    /// High-level protocol envelope for WebSocket text frames.
    ///
    /// This struct is designed to map almost 1:1 to a DB row:
    ///
    ///   id      → TEXT / INTEGER PRIMARY KEY
    ///   kind    → TEXT (event / system / error / ...)
    ///   ts      → TEXT (ISO-8601, UTC)
    ///   room    → TEXT (nullable)
    ///   type    → TEXT (business type, e.g. chat.message)
    ///   payload → JSON text (from kvs)
    ///
    struct JsonMessage
    {
        std::string id;         ///< optional stable identifier for WAL / DB
        std::string kind;       ///< "event", "system", "error", ...
        std::string ts;         ///< ISO-8601 UTC timestamp (optional)
        std::string room;       ///< logical channel / room (optional)
        std::string type;       ///< business message type (required)
        vix::json::kvs payload; ///< business payload as flat kvs

        // ---- Client-friendly helpers on payload ------------------------------

        /// Get a string from payload["key"], or empty string if missing.
        std::string get_string(const std::string &key) const
        {
            const auto &a = payload.flat;
            const size_t n = a.size() - (a.size() % 2);

            for (size_t i = 0; i < n; i += 2)
            {
                const auto &k = a[i].v;
                const auto &v = a[i + 1].v;

                if (std::holds_alternative<std::string>(k) &&
                    std::get<std::string>(k) == key)
                {
                    if (std::holds_alternative<std::string>(v))
                        return std::get<std::string>(v);
                    else
                        return {};
                }
            }
            return {};
        }

        /// Generic typed getter (optional) from payload.
        template <typename T>
        std::optional<T> get(const std::string &key) const
        {
            const auto &a = payload.flat;
            const size_t n = a.size() - (a.size() % 2);

            for (size_t i = 0; i < n; i += 2)
            {
                const auto &k = a[i].v;
                const auto &v = a[i + 1].v;

                if (std::holds_alternative<std::string>(k) &&
                    std::get<std::string>(k) == key)
                {
                    if (std::holds_alternative<T>(v))
                        return std::get<T>(v);

                    return std::nullopt;
                }
            }
            return std::nullopt;
        }

        // ---- Parse JSON envelope ---------------------------------------------

        static std::optional<JsonMessage> parse(std::string_view s)
        {
            try
            {
                auto j = nlohmann::json::parse(s);
                if (!j.is_object())
                    return std::nullopt;

                JsonMessage msg;

                // Envelope fields (all optional sauf type)
                msg.id = j.value("id", std::string{});
                msg.kind = j.value("kind", std::string{}); // souvent "event" / "system"
                msg.ts = j.value("ts", std::string{});
                msg.room = j.value("room", std::string{});
                msg.type = j.value("type", std::string{});

                if (j.contains("payload"))
                    msg.payload = detail::nlohmann_payload_to_kvs(j["payload"]);

                // type vide = on ignore (message invalide)
                if (msg.type.empty())
                    return std::nullopt;

                return msg;
            }
            catch (...)
            {
                return std::nullopt;
            }
        }

        // ---- Serialize envelope ----------------------------------------------

        /// Serialize a full JsonMessage (envelope + payload) to a JSON string.
        static std::string serialize(const JsonMessage &m)
        {
            nlohmann::json payloadJson = detail::ws_kvs_to_nlohmann(m.payload);

            nlohmann::json j = nlohmann::json::object();
            if (!m.id.empty())
                j["id"] = m.id;
            if (!m.kind.empty())
                j["kind"] = m.kind;
            if (!m.ts.empty())
                j["ts"] = m.ts;
            if (!m.room.empty())
                j["room"] = m.room;

            j["type"] = m.type;
            j["payload"] = payloadJson;

            return j.dump();
        }

        /// Convenience: serialize type + payload seulement (métadonnées optionnelles).
        ///
        /// Exemple simple :
        ///   JsonMessage::serialize("chat.message", payloadKvs);
        ///
        /// Exemple avancé avec room:
        ///   JsonMessage::serialize("chat.message", payloadKvs, "africa");
        ///
        static std::string serialize(const std::string &type,
                                     const vix::json::kvs &payloadKvs,
                                     const std::string &room = {},
                                     const std::string &id = {},
                                     const std::string &kind = {},
                                     const std::string &ts = {})
        {
            JsonMessage m;
            m.type = type;
            m.payload = payloadKvs;
            m.room = room;
            m.id = id;
            m.kind = kind;
            m.ts = ts;

            return serialize(m);
        }
    };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_PROTOCOL_HPP
