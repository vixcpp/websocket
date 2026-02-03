/**
 *
 *  @file protocol.hpp
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
#ifndef VIX_WEBSOCKET_PROTOCOL_HPP
#define VIX_WEBSOCKET_PROTOCOL_HPP

/**
 * @file protocol.hpp
 * @brief JSON protocol helpers for WebSocket messages.
 *
 * Standardized envelope to support typed messaging and future persistence (SQLite + WAL).
 *
 * Wire format:
 * {
 *   "id":     "msg-123",                    // optional
 *   "kind":   "event" | "system" | "error", // optional, default "event"
 *   "ts":     "2025-12-07T10:15:30Z",       // optional, ISO-8601 UTC
 *   "room":   "africa",                     // optional
 *   "type":   "chat.message",               // required (business type)
 *   "payload": { ... }                      // arbitrary key/values (vix::json::kvs)
 * }
 */

#include <string>
#include <string_view>
#include <optional>

#include <nlohmann/json.hpp>
#include <vix/json/Simple.hpp>

namespace vix::websocket
{
  namespace detail
  {
    /** @brief Convert a vix::json::token to a nlohmann::json value. */
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

    /** @brief Convert a flat kvs list (k,v,k,v,...) into a nlohmann::json object. */
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

    /** @brief Convert a JSON payload object into vix::json::kvs (best-effort). */
    inline vix::json::kvs nlohmann_payload_to_kvs(const nlohmann::json &payload)
    {
      vix::json::kvs kv;

      if (!payload.is_object())
        return kv;

      for (auto it = payload.begin(); it != payload.end(); ++it)
      {
        const std::string key = it.key();
        const nlohmann::json &val = *it;
        kv.flat.emplace_back(vix::json::token{key});

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
          kv.flat.emplace_back(vix::json::token{});
        }
        else
        {
          kv.flat.emplace_back(vix::json::token{});
        }
      }

      return kv;
    }

  } // namespace detail

  /**
   * @brief Typed WebSocket message envelope (designed for future DB/WAL persistence).
   *
   * Maps cleanly to a DB row: id, room, kind, type, ts, payload_json.
   * Only "type" is required. Other fields are optional for simple applications.
   */
  struct JsonMessage
  {
    std::string id{};
    std::string kind{"event"};
    std::string ts{};
    std::string room{};
    std::string type{};
    vix::json::kvs payload{};

    JsonMessage() = default;

    /** @brief Read payload["key"] as string, or empty string if missing/mismatched. */
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

    /**
     * @brief Read payload["key"] as type T.
     *
     * Returns std::nullopt if missing or if the stored token type does not match T.
     */
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

    /**
     * @brief Parse a JSON text frame into JsonMessage.
     *
     * Returns nullopt on invalid JSON, wrong shape, or missing "type".
     */
    static std::optional<JsonMessage> parse(std::string_view s)
    {
      try
      {
        auto j = nlohmann::json::parse(s);
        if (!j.is_object())
          return std::nullopt;

        JsonMessage msg;

        msg.id = j.value("id", std::string{});
        msg.kind = j.value("kind", std::string{});
        msg.ts = j.value("ts", std::string{});
        msg.room = j.value("room", std::string{});
        msg.type = j.value("type", std::string{});

        if (j.contains("payload"))
          msg.payload = detail::nlohmann_payload_to_kvs(j["payload"]);

        if (msg.type.empty())
          return std::nullopt;

        if (msg.kind.empty())
          msg.kind = "event";

        return msg;
      }
      catch (...)
      {
        return std::nullopt;
      }
    }

    /** @brief Serialize a JsonMessage to compact JSON text. */
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

    /**
     * @brief Convenience serializer for type + payload with optional metadata.
     */
    static std::string serialize(
        const std::string &type,
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

    /** @brief Convert this message to a nlohmann::json object (same shape as serialize()). */
    nlohmann::json to_nlohmann() const
    {
      nlohmann::json payloadJson = detail::ws_kvs_to_nlohmann(payload);

      nlohmann::json j = nlohmann::json::object();

      if (!id.empty())
        j["id"] = id;
      if (!kind.empty())
        j["kind"] = kind;
      if (!ts.empty())
        j["ts"] = ts;
      if (!room.empty())
        j["room"] = room;

      j["type"] = type;
      j["payload"] = payloadJson;

      return j;
    }
  };

  /** @brief Convert a single JsonMessage to nlohmann::json (same shape as serialize()). */
  inline nlohmann::json json_message_to_nlohmann(const JsonMessage &m)
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
    j["payload"] = std::move(payloadJson);

    return j;
  }

  /** @brief Convert multiple messages to a JSON array (useful for /ws/poll). */
  inline nlohmann::json json_messages_to_nlohmann_array(
      const std::vector<JsonMessage> &messages)
  {
    nlohmann::json arr = nlohmann::json::array();

    if (!messages.empty())
    {
      auto &vec = arr.get_ref<nlohmann::json::array_t &>();
      vec.reserve(messages.size());
    }

    for (const auto &msg : messages)
    {
      arr.push_back(msg.to_nlohmann());
    }

    return arr;
  }

  /** @brief Serialize multiple messages as a compact JSON array string. */
  inline std::string serialize_messages_array(
      const std::vector<JsonMessage> &messages)
  {
    return json_messages_to_nlohmann_array(messages).dump();
  }

} // namespace vix::websocket

#endif // VIX_WEBSOCKET_PROTOCOL_HPP
