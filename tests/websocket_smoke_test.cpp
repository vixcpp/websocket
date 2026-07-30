/**
 *
 * @file websocket_smoke_test.cpp
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
#include <memory>
#include <string>
#include <type_traits>

#include <vix/websocket.hpp>

namespace
{
  using Client = vix::websocket::Client;
  using Config = vix::websocket::Config;
  using JsonMessage = vix::websocket::JsonMessage;
  using Router = vix::websocket::Router;

  static void test_public_type_traits()
  {
    static_assert(std::is_default_constructible_v<Config>);
    static_assert(std::is_copy_constructible_v<Config>);
    static_assert(std::is_copy_assignable_v<Config>);
    static_assert(std::is_move_constructible_v<Config>);
    static_assert(std::is_move_assignable_v<Config>);
    static_assert(std::is_destructible_v<Config>);

    static_assert(std::is_default_constructible_v<Router>);
    static_assert(std::is_destructible_v<Router>);

    static_assert(std::is_default_constructible_v<JsonMessage>);
    static_assert(std::is_copy_constructible_v<JsonMessage>);
    static_assert(std::is_move_constructible_v<JsonMessage>);
    static_assert(std::is_destructible_v<JsonMessage>);

    static_assert(!std::is_default_constructible_v<Client>);
    static_assert(std::is_destructible_v<Client>);
  }

  static void test_public_objects_can_be_constructed()
  {
    Config config;
    Router router;
    JsonMessage message;

    (void)config;
    (void)router;

    assert(message.id.empty());
    assert(message.kind == "event");
    assert(message.ts.empty());
    assert(message.room.empty());
    assert(message.type.empty());
    assert(message.payload.flat.empty());
  }

  static void test_minimal_json_message_round_trip()
  {
    JsonMessage message;

    message.id = "smoke-message";
    message.kind = "event";
    message.ts = "2025-01-01T00:00:00Z";
    message.room = "smoke-room";
    message.type = "smoke";

    const std::string serialized =
        JsonMessage::serialize(message);

    assert(serialized.empty() == false);

    const auto parsed =
        JsonMessage::parse(serialized);

    assert(parsed.has_value() == true);

    assert(parsed->id == "smoke-message");
    assert(parsed->kind == "event");
    assert(parsed->ts == "2025-01-01T00:00:00Z");
    assert(parsed->room == "smoke-room");
    assert(parsed->type == "smoke");
    assert(parsed->payload.flat.empty());
  }

  static void test_client_factory_creates_disconnected_client()
  {
    const std::shared_ptr<Client> client =
        Client::create(
            "127.0.0.1",
            "9090",
            "/smoke");

    assert(client != nullptr);
    assert(client->is_connected() == false);

    client->close();

    assert(client->is_connected() == false);
  }

  static void test_client_factory_normalizes_empty_target()
  {
    const std::shared_ptr<Client> client =
        Client::create(
            "localhost",
            "9090",
            "");

    assert(client != nullptr);
    assert(client->is_connected() == false);

    client->close();
  }

} // namespace

int main()
{
  test_public_type_traits();

  test_public_objects_can_be_constructed();
  test_minimal_json_message_round_trip();

  test_client_factory_creates_disconnected_client();
  test_client_factory_normalizes_empty_target();

  return 0;
}
