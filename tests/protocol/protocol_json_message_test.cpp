/**
 *
 * @file protocol_json_message_test.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira. All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license that can be found in the License file.
 *
 * Vix.cpp
 *
 */

#include <cassert>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include <nlohmann/json.hpp>

#include <vix/websocket/protocol.hpp>

namespace
{
  using JsonMessage = vix::websocket::JsonMessage;

  static vix::json::kvs make_payload()
  {
    vix::json::kvs payload;

    payload.flat.emplace_back(
        vix::json::token{std::string{"name"}});

    payload.flat.emplace_back(
        vix::json::token{std::string{"Gaspard"}});

    payload.flat.emplace_back(
        vix::json::token{std::string{"count"}});

    payload.flat.emplace_back(
        vix::json::token{static_cast<long long>(42)});

    payload.flat.emplace_back(
        vix::json::token{std::string{"active"}});

    payload.flat.emplace_back(
        vix::json::token{true});

    payload.flat.emplace_back(
        vix::json::token{std::string{"ratio"}});

    payload.flat.emplace_back(
        vix::json::token{1.5});

    return payload;
  }

  static void test_json_message_type_traits()
  {
    static_assert(std::is_default_constructible_v<JsonMessage>);
    static_assert(std::is_copy_constructible_v<JsonMessage>);
    static_assert(std::is_copy_assignable_v<JsonMessage>);
    static_assert(std::is_move_constructible_v<JsonMessage>);
    static_assert(std::is_move_assignable_v<JsonMessage>);
    static_assert(std::is_destructible_v<JsonMessage>);

    static_assert(
        std::is_same_v<
            decltype(JsonMessage::parse(
                std::declval<std::string_view>())),
            std::optional<JsonMessage>>);

    static_assert(
        std::is_same_v<
            decltype(JsonMessage::serialize(
                std::declval<const JsonMessage &>())),
            std::string>);
  }

  static void test_default_values()
  {
    JsonMessage message;

    assert(message.id.empty());
    assert(message.kind == "event");
    assert(message.ts.empty());
    assert(message.room.empty());
    assert(message.type.empty());
    assert(message.payload.flat.empty());
  }

  static void test_get_string_existing_value()
  {
    JsonMessage message;
    message.payload = make_payload();

    assert(
        message.get_string("name") ==
        "Gaspard");
  }

  static void test_get_string_missing_value()
  {
    JsonMessage message;
    message.payload = make_payload();

    assert(
        message.get_string("missing").empty());
  }

  static void test_get_string_wrong_type()
  {
    JsonMessage message;
    message.payload = make_payload();

    assert(
        message.get_string("count").empty());
  }

  static void test_get_typed_string()
  {
    JsonMessage message;
    message.payload = make_payload();

    const auto value =
        message.get<std::string>("name");

    assert(value.has_value());
    assert(*value == "Gaspard");
  }

  static void test_get_typed_integer()
  {
    JsonMessage message;
    message.payload = make_payload();

    const auto value =
        message.get<long long>("count");

    assert(value.has_value());
    assert(*value == 42);
  }

  static void test_get_typed_boolean()
  {
    JsonMessage message;
    message.payload = make_payload();

    const auto value =
        message.get<bool>("active");

    assert(value.has_value());
    assert(*value == true);
  }

  static void test_get_typed_double()
  {
    JsonMessage message;
    message.payload = make_payload();

    const auto value =
        message.get<double>("ratio");

    assert(value.has_value());
    assert(*value == 1.5);
  }

  static void test_get_wrong_type_returns_nullopt()
  {
    JsonMessage message;
    message.payload = make_payload();

    assert(
        !message.get<std::string>("count").has_value());

    assert(
        !message.get<long long>("name").has_value());

    assert(
        !message.get<bool>("ratio").has_value());
  }

  static void test_get_missing_key_returns_nullopt()
  {
    JsonMessage message;
    message.payload = make_payload();

    assert(
        !message.get<std::string>("missing").has_value());

    assert(
        !message.get<long long>("missing").has_value());
  }

  static void test_parse_complete_message()
  {
    const std::string input =
        R"({
          "id":"msg-42",
          "kind":"command",
          "ts":"2026-07-30T10:00:00Z",
          "room":"general",
          "type":"chat.message",
          "payload":{
            "name":"Gaspard",
            "count":42,
            "active":true,
            "ratio":1.5
          }
        })";

    const auto parsed =
        JsonMessage::parse(input);

    assert(parsed.has_value());

    assert(parsed->id == "msg-42");
    assert(parsed->kind == "command");
    assert(parsed->ts == "2026-07-30T10:00:00Z");
    assert(parsed->room == "general");
    assert(parsed->type == "chat.message");

    assert(
        parsed->get_string("name") ==
        "Gaspard");

    assert(
        parsed->get<long long>("count") ==
        std::optional<long long>{42});

    assert(
        parsed->get<bool>("active") ==
        std::optional<bool>{true});

    assert(
        parsed->get<double>("ratio") ==
        std::optional<double>{1.5});
  }

  static void test_parse_minimal_message()
  {
    const auto parsed =
        JsonMessage::parse(
            R"({"type":"system.ready"})");

    assert(parsed.has_value());

    assert(parsed->id.empty());
    assert(parsed->kind == "event");
    assert(parsed->ts.empty());
    assert(parsed->room.empty());
    assert(parsed->type == "system.ready");
    assert(parsed->payload.flat.empty());
  }

  static void test_parse_missing_kind_uses_event()
  {
    const auto parsed =
        JsonMessage::parse(
            R"({
              "type":"chat.message",
              "payload":{}
            })");

    assert(parsed.has_value());
    assert(parsed->kind == "event");
  }

  static void test_parse_empty_kind_uses_event()
  {
    const auto parsed =
        JsonMessage::parse(
            R"({
              "kind":"",
              "type":"chat.message",
              "payload":{}
            })");

    assert(parsed.has_value());
    assert(parsed->kind == "event");
  }

  static void test_parse_missing_type_fails()
  {
    const auto parsed =
        JsonMessage::parse(
            R"({
              "kind":"event",
              "payload":{}
            })");

    assert(!parsed.has_value());
  }

  static void test_parse_empty_type_fails()
  {
    const auto parsed =
        JsonMessage::parse(
            R"({
              "type":"",
              "payload":{}
            })");

    assert(!parsed.has_value());
  }

  static void test_parse_invalid_json_fails()
  {
    assert(
        !JsonMessage::parse("{invalid").has_value());

    assert(
        !JsonMessage::parse("").has_value());

    assert(
        !JsonMessage::parse("not json").has_value());
  }

  static void test_parse_non_object_fails()
  {
    assert(
        !JsonMessage::parse("[]").has_value());

    assert(
        !JsonMessage::parse("\"message\"").has_value());

    assert(
        !JsonMessage::parse("42").has_value());

    assert(
        !JsonMessage::parse("true").has_value());

    assert(
        !JsonMessage::parse("null").has_value());
  }

  static void test_parse_missing_payload()
  {
    const auto parsed =
        JsonMessage::parse(
            R"({"type":"system.ready"})");

    assert(parsed.has_value());
    assert(parsed->payload.flat.empty());
  }

  static void test_parse_null_payload()
  {
    const auto parsed =
        JsonMessage::parse(
            R"({
              "type":"system.ready",
              "payload":null
            })");

    assert(parsed.has_value());
    assert(parsed->payload.flat.empty());
  }

  static void test_parse_payload_array()
  {
    const auto parsed =
        JsonMessage::parse(
            R"({
              "type":"items",
              "payload":[1,2,3]
            })");

    assert(parsed.has_value());
    assert(parsed->payload.flat.empty());
  }

  static void test_parse_nested_payload_values_become_null_tokens()
  {
    const auto parsed =
        JsonMessage::parse(
            R"({
              "type":"nested",
              "payload":{
                "object":{"value":1},
                "array":[1,2,3]
              }
            })");

    assert(parsed.has_value());

    assert(
        !parsed->get<std::string>("object").has_value());

    assert(
        !parsed->get<std::string>("array").has_value());
  }

  static void test_serialize_complete_message()
  {
    JsonMessage message;

    message.id = "msg-42";
    message.kind = "command";
    message.ts = "2026-07-30T10:00:00Z";
    message.room = "general";
    message.type = "chat.message";
    message.payload = make_payload();

    const std::string serialized =
        JsonMessage::serialize(message);

    const nlohmann::json json =
        nlohmann::json::parse(serialized);

    assert(json.is_object());

    assert(json["id"] == "msg-42");
    assert(json["kind"] == "command");
    assert(json["ts"] == "2026-07-30T10:00:00Z");
    assert(json["room"] == "general");
    assert(json["type"] == "chat.message");

    assert(json["payload"]["name"] == "Gaspard");
    assert(json["payload"]["count"] == 42);
    assert(json["payload"]["active"] == true);
    assert(json["payload"]["ratio"] == 1.5);
  }

  static void test_serialize_omits_empty_optional_fields()
  {
    JsonMessage message;

    message.kind.clear();
    message.type = "system.ready";

    const nlohmann::json json =
        nlohmann::json::parse(
            JsonMessage::serialize(message));

    assert(!json.contains("id"));
    assert(!json.contains("kind"));
    assert(!json.contains("ts"));
    assert(!json.contains("room"));

    assert(json["type"] == "system.ready");
    assert(json.contains("payload"));
    assert(json["payload"].is_object());
  }

  static void test_default_message_serialization_includes_kind()
  {
    JsonMessage message;
    message.type = "system.ready";

    const nlohmann::json json =
        nlohmann::json::parse(
            JsonMessage::serialize(message));

    assert(json["kind"] == "event");
    assert(json["type"] == "system.ready");
  }

  static void test_serialize_overload()
  {
    const vix::json::kvs payload =
        make_payload();

    const std::string serialized =
        JsonMessage::serialize(
            "chat.message",
            payload,
            "general",
            "msg-42",
            "command",
            "2026-07-30T10:00:00Z");

    const nlohmann::json json =
        nlohmann::json::parse(serialized);

    assert(json["type"] == "chat.message");
    assert(json["room"] == "general");
    assert(json["id"] == "msg-42");
    assert(json["kind"] == "command");
    assert(json["ts"] == "2026-07-30T10:00:00Z");

    assert(json["payload"]["name"] == "Gaspard");
  }

  static void test_serialize_overload_defaults()
  {
    const vix::json::kvs payload =
        make_payload();

    const nlohmann::json json =
        nlohmann::json::parse(
            JsonMessage::serialize(
                "chat.message",
                payload));

    assert(json["type"] == "chat.message");
    assert(json.contains("payload"));

    assert(!json.contains("room"));
    assert(!json.contains("id"));
    assert(!json.contains("kind"));
    assert(!json.contains("ts"));
  }

  static void test_to_nlohmann_complete_message()
  {
    JsonMessage message;

    message.id = "msg-1";
    message.kind = "event";
    message.ts = "2026-07-30T10:00:00Z";
    message.room = "general";
    message.type = "presence.joined";
    message.payload = make_payload();

    const nlohmann::json json =
        message.to_nlohmann();

    assert(json["id"] == "msg-1");
    assert(json["kind"] == "event");
    assert(json["ts"] == "2026-07-30T10:00:00Z");
    assert(json["room"] == "general");
    assert(json["type"] == "presence.joined");

    assert(json["payload"]["name"] == "Gaspard");
  }

  static void test_serialize_matches_to_nlohmann()
  {
    JsonMessage message;

    message.id = "msg-99";
    message.kind = "event";
    message.room = "general";
    message.type = "chat.message";
    message.payload = make_payload();

    const nlohmann::json serialized =
        nlohmann::json::parse(
            JsonMessage::serialize(message));

    const nlohmann::json direct =
        message.to_nlohmann();

    assert(serialized == direct);
  }

  static void test_parse_serialize_roundtrip()
  {
    JsonMessage original;

    original.id = "msg-42";
    original.kind = "command";
    original.ts = "2026-07-30T10:00:00Z";
    original.room = "general";
    original.type = "chat.message";
    original.payload = make_payload();

    const std::string serialized =
        JsonMessage::serialize(original);

    const auto parsed =
        JsonMessage::parse(serialized);

    assert(parsed.has_value());

    assert(parsed->id == original.id);
    assert(parsed->kind == original.kind);
    assert(parsed->ts == original.ts);
    assert(parsed->room == original.room);
    assert(parsed->type == original.type);

    assert(
        parsed->get_string("name") ==
        "Gaspard");

    assert(
        parsed->get<long long>("count") ==
        std::optional<long long>{42});

    assert(
        parsed->get<bool>("active") ==
        std::optional<bool>{true});
  }

  static void test_repeated_serialization_is_deterministic()
  {
    JsonMessage message;

    message.id = "msg-42";
    message.kind = "event";
    message.room = "general";
    message.type = "chat.message";
    message.payload = make_payload();

    const std::string first =
        JsonMessage::serialize(message);

    const std::string second =
        JsonMessage::serialize(message);

    const std::string third =
        JsonMessage::serialize(message);

    assert(first == second);
    assert(second == third);
  }

  static void test_serialization_does_not_modify_message()
  {
    JsonMessage message;

    message.id = "msg-42";
    message.kind = "event";
    message.room = "general";
    message.type = "chat.message";
    message.payload = make_payload();

    const std::string original_id = message.id;
    const std::string original_kind = message.kind;
    const std::string original_room = message.room;
    const std::string original_type = message.type;

    const std::string serialized =
        JsonMessage::serialize(message);

    assert(!serialized.empty());

    assert(message.id == original_id);
    assert(message.kind == original_kind);
    assert(message.room == original_room);
    assert(message.type == original_type);

    assert(
        message.get_string("name") ==
        "Gaspard");
  }

} // namespace

int main()
{
  test_json_message_type_traits();
  test_default_values();

  test_get_string_existing_value();
  test_get_string_missing_value();
  test_get_string_wrong_type();

  test_get_typed_string();
  test_get_typed_integer();
  test_get_typed_boolean();
  test_get_typed_double();

  test_get_wrong_type_returns_nullopt();
  test_get_missing_key_returns_nullopt();

  test_parse_complete_message();
  test_parse_minimal_message();

  test_parse_missing_kind_uses_event();
  test_parse_empty_kind_uses_event();

  test_parse_missing_type_fails();
  test_parse_empty_type_fails();

  test_parse_invalid_json_fails();
  test_parse_non_object_fails();

  test_parse_missing_payload();
  test_parse_null_payload();
  test_parse_payload_array();
  test_parse_nested_payload_values_become_null_tokens();

  test_serialize_complete_message();
  test_serialize_omits_empty_optional_fields();
  test_default_message_serialization_includes_kind();

  test_serialize_overload();
  test_serialize_overload_defaults();

  test_to_nlohmann_complete_message();
  test_serialize_matches_to_nlohmann();

  test_parse_serialize_roundtrip();
  test_repeated_serialization_is_deterministic();
  test_serialization_does_not_modify_message();

  return 0;
}
