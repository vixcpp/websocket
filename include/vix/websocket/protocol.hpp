#ifndef VIX_WEBSOCKET_PROTOCOL_HPP
#define VIX_WEBSOCKET_PROTOCOL_HPP

/**
 * @file protocol.hpp
 * @brief JSON protocol helpers for WebSocket { type, payload } messages.
 *
 * Public API uses vix::json::kvs for payload. Internally, a JSON library
 * is used to parse and format text frames.
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

    /// High-level protocol { type, payload } on top of WebSocket text frames.
    struct JsonMessage
    {
        std::string type;
        vix::json::kvs payload;

        // ---- NEW: client-friendly helpers ------------------------------------

        /// Get a string from payload["key"], or empty string if missing
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

        /// Generic typed getter (optional)
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

        // ---- Parse JSON -------------------------------------------------------

        static std::optional<JsonMessage> parse(std::string_view s)
        {
            try
            {
                auto j = nlohmann::json::parse(s);
                if (!j.is_object())
                    return std::nullopt;

                JsonMessage msg;
                msg.type = j.value("type", "");

                if (j.contains("payload"))
                    msg.payload = detail::nlohmann_payload_to_kvs(j["payload"]);

                return msg;
            }
            catch (...)
            {
                return std::nullopt;
            }
        }

        // ---- Serialize { type, payload } -------------------------------------

        static std::string serialize(const std::string &type,
                                     const vix::json::kvs &payloadKvs)
        {
            nlohmann::json payload = detail::ws_kvs_to_nlohmann(payloadKvs);

            nlohmann::json j{
                {"type", type},
                {"payload", payload},
            };
            return j.dump();
        }
    };

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_PROTOCOL_HPP
