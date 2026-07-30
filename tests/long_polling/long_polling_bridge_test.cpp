/**
 *
 * @file long_polling_bridge_test.cpp
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
#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <vix/websocket/LongPolling.hpp>
#include <vix/websocket/LongPollingBridge.hpp>
#include <vix/websocket/Metrics.hpp>

namespace
{
  using JsonMessage =
      vix::websocket::JsonMessage;

  using LongPollingBridge =
      vix::websocket::LongPollingBridge;

  using LongPollingManager =
      vix::websocket::LongPollingManager;

  using WebSocketMetrics =
      vix::websocket::WebSocketMetrics;

  static JsonMessage make_message(
      std::string id,
      std::string type,
      std::string room = {})
  {
    JsonMessage message;

    message.id = std::move(id);
    message.kind = "event";
    message.room = std::move(room);
    message.type = std::move(type);

    return message;
  }

  static void test_bridge_type_contracts()
  {
    static_assert(
        std::is_same_v<
            LongPollingBridge::SessionId,
            std::string>);

    static_assert(
        std::is_same_v<
            LongPollingBridge::Resolver,
            std::function<
                std::string(
                    const JsonMessage &)>>);

    static_assert(
        std::is_same_v<
            LongPollingBridge::HttpToWsForward,
            std::function<
                void(
                    const JsonMessage &)>>);

    static_assert(
        std::is_constructible_v<
            LongPollingBridge,
            LongPollingManager &>);

    static_assert(
        std::is_constructible_v<
            LongPollingBridge,
            WebSocketMetrics *>);
  }

  static void test_bridge_api_return_types()
  {
    static_assert(
        std::is_same_v<
            decltype(std::declval<LongPollingBridge &>()
                         .on_ws_message(
                             std::declval<
                                 const JsonMessage &>())),
            void>);

    static_assert(
        std::is_same_v<
            decltype(std::declval<LongPollingBridge &>()
                         .poll(
                             std::declval<
                                 const std::string &>(),
                             std::size_t{50},
                             true)),
            std::vector<JsonMessage>>);

    static_assert(
        std::is_same_v<
            decltype(std::declval<LongPollingBridge &>()
                         .manager()),
            LongPollingManager &>);

    static_assert(
        std::is_same_v<
            decltype(std::declval<
                         const LongPollingBridge &>()
                         .manager()),
            const LongPollingManager &>);

    static_assert(
        std::is_same_v<
            decltype(std::declval<
                         const LongPollingBridge &>()
                         .session_count()),
            std::size_t>);

    static_assert(
        std::is_same_v<
            decltype(std::declval<
                         const LongPollingBridge &>()
                         .buffer_size(
                             std::declval<
                                 const std::string &>())),
            std::size_t>);
  }

  static void test_external_manager_is_reused()
  {
    LongPollingManager manager;

    LongPollingBridge bridge{
        manager};

    assert(&bridge.manager() == &manager);

    const LongPollingBridge &constBridge =
        bridge;

    assert(&constBridge.manager() == &manager);
  }

  static void test_external_manager_changes_are_visible()
  {
    LongPollingManager manager;

    LongPollingBridge bridge{
        manager};

    manager.push_to(
        "session-1",
        make_message(
            "message-1",
            "chat.message"));

    assert(bridge.session_count() == 1u);
    assert(bridge.buffer_size("session-1") == 1u);

    const auto messages =
        bridge.poll(
            "session-1",
            10u,
            false);

    assert(messages.size() == 1u);
    assert(messages[0].id == "message-1");

    assert(manager.buffer_size("session-1") == 0u);
  }

  static void test_bridge_changes_are_visible_to_external_manager()
  {
    LongPollingManager manager;

    LongPollingBridge bridge{
        manager};

    bridge.on_ws_message(
        make_message(
            "message-1",
            "chat.message",
            "general"));

    assert(manager.session_count() == 1u);
    assert(manager.buffer_size("room:general") == 1u);
  }

  static void test_owning_bridge_constructs_manager()
  {
    LongPollingBridge bridge{
        static_cast<WebSocketMetrics *>(nullptr)};

    assert(bridge.session_count() == 0u);
    assert(bridge.buffer_size("missing") == 0u);

    assert(&bridge.manager() != nullptr);
  }

  static void test_default_room_resolution()
  {
    LongPollingManager manager;

    LongPollingBridge bridge{
        manager};

    bridge.on_ws_message(
        make_message(
            "message-1",
            "chat.message",
            "general"));

    assert(bridge.session_count() == 1u);
    assert(bridge.buffer_size("room:general") == 1u);

    assert(bridge.buffer_size("general") == 0u);
    assert(bridge.buffer_size("broadcast") == 0u);

    const auto messages =
        bridge.poll(
            "room:general",
            10u,
            false);

    assert(messages.size() == 1u);

    assert(messages[0].id == "message-1");
    assert(messages[0].room == "general");
    assert(messages[0].type == "chat.message");
  }

  static void test_default_empty_room_resolution()
  {
    LongPollingManager manager;

    LongPollingBridge bridge{
        manager};

    bridge.on_ws_message(
        make_message(
            "message-1",
            "system.ready"));

    assert(bridge.session_count() == 1u);
    assert(bridge.buffer_size("broadcast") == 1u);

    const auto messages =
        bridge.poll(
            "broadcast",
            10u,
            false);

    assert(messages.size() == 1u);
    assert(messages[0].id == "message-1");
    assert(messages[0].room.empty());
  }

  static void test_same_room_reuses_same_session()
  {
    LongPollingManager manager;

    LongPollingBridge bridge{
        manager};

    bridge.on_ws_message(
        make_message(
            "message-1",
            "first",
            "general"));

    bridge.on_ws_message(
        make_message(
            "message-2",
            "second",
            "general"));

    bridge.on_ws_message(
        make_message(
            "message-3",
            "third",
            "general"));

    assert(bridge.session_count() == 1u);
    assert(bridge.buffer_size("room:general") == 3u);

    const auto messages =
        bridge.poll(
            "room:general",
            10u,
            false);

    assert(messages.size() == 3u);

    assert(messages[0].id == "message-1");
    assert(messages[1].id == "message-2");
    assert(messages[2].id == "message-3");
  }

  static void test_different_rooms_create_different_sessions()
  {
    LongPollingManager manager;

    LongPollingBridge bridge{
        manager};

    bridge.on_ws_message(
        make_message(
            "message-1",
            "chat.message",
            "general"));

    bridge.on_ws_message(
        make_message(
            "message-2",
            "chat.message",
            "support"));

    bridge.on_ws_message(
        make_message(
            "message-3",
            "system.ready"));

    assert(bridge.session_count() == 3u);

    assert(bridge.buffer_size("room:general") == 1u);
    assert(bridge.buffer_size("room:support") == 1u);
    assert(bridge.buffer_size("broadcast") == 1u);
  }

  static void test_custom_resolver_is_used()
  {
    LongPollingManager manager;

    std::size_t resolverCalls = 0u;

    LongPollingBridge bridge{
        manager,
        [&resolverCalls](
            const JsonMessage &message)
        {
          ++resolverCalls;

          return std::string{"message:"} +
                 message.id;
        }};

    bridge.on_ws_message(
        make_message(
            "message-42",
            "chat.message",
            "ignored-room"));

    assert(resolverCalls == 1u);

    assert(bridge.session_count() == 1u);

    assert(
        bridge.buffer_size(
            "message:message-42") ==
        1u);

    assert(
        bridge.buffer_size(
            "room:ignored-room") ==
        0u);
  }

  static void test_custom_resolver_can_return_empty_session_id()
  {
    LongPollingManager manager;

    LongPollingBridge bridge{
        manager,
        [](const JsonMessage &)
        {
          return std::string{};
        }};

    bridge.on_ws_message(
        make_message(
            "message-1",
            "chat.message",
            "general"));

    assert(bridge.session_count() == 1u);
    assert(bridge.buffer_size("") == 1u);

    const auto messages =
        bridge.poll(
            "",
            10u,
            false);

    assert(messages.size() == 1u);
    assert(messages[0].id == "message-1");
  }

  static void test_poll_delegates_to_manager()
  {
    LongPollingManager manager;

    LongPollingBridge bridge{
        manager};

    manager.push_to(
        "session-1",
        make_message(
            "message-1",
            "first"));

    manager.push_to(
        "session-1",
        make_message(
            "message-2",
            "second"));

    manager.push_to(
        "session-1",
        make_message(
            "message-3",
            "third"));

    const auto first =
        bridge.poll(
            "session-1",
            2u,
            false);

    assert(first.size() == 2u);

    assert(first[0].id == "message-1");
    assert(first[1].id == "message-2");

    assert(bridge.buffer_size("session-1") == 1u);

    const auto second =
        bridge.poll(
            "session-1",
            10u,
            false);

    assert(second.size() == 1u);
    assert(second[0].id == "message-3");

    assert(bridge.buffer_size("session-1") == 0u);
  }

  static void test_poll_missing_without_creation()
  {
    LongPollingManager manager;

    LongPollingBridge bridge{
        manager};

    const auto messages =
        bridge.poll(
            "missing-session",
            10u,
            false);

    assert(messages.empty());
    assert(bridge.session_count() == 0u);
  }

  static void test_poll_missing_with_creation()
  {
    LongPollingManager manager;

    LongPollingBridge bridge{
        manager};

    const auto messages =
        bridge.poll(
            "created-session",
            10u,
            true);

    assert(messages.empty());

    assert(bridge.session_count() == 1u);
    assert(bridge.buffer_size("created-session") == 0u);
  }

  static void test_owning_bridge_respects_buffer_limit()
  {
    LongPollingBridge bridge{
        static_cast<WebSocketMetrics *>(nullptr),
        std::chrono::seconds{60},
        2u};

    bridge.on_ws_message(
        make_message(
            "message-1",
            "first",
            "general"));

    bridge.on_ws_message(
        make_message(
            "message-2",
            "second",
            "general"));

    bridge.on_ws_message(
        make_message(
            "message-3",
            "third",
            "general"));

    assert(bridge.buffer_size("room:general") == 2u);

    const auto messages =
        bridge.poll(
            "room:general",
            10u,
            false);

    assert(messages.size() == 2u);

    assert(messages[0].id == "message-2");
    assert(messages[1].id == "message-3");
  }

  static void test_owning_bridge_updates_metrics()
  {
    WebSocketMetrics metrics;

    LongPollingBridge bridge{
        &metrics,
        std::chrono::seconds{60},
        16u};

    bridge.on_ws_message(
        make_message(
            "message-1",
            "chat.message",
            "general"));

    assert(
        metrics.lp_sessions_total.load(
            std::memory_order_relaxed) ==
        1u);

    assert(
        metrics.lp_sessions_active.load(
            std::memory_order_relaxed) ==
        1u);

    assert(
        metrics.lp_messages_buffered.load(
            std::memory_order_relaxed) ==
        1u);

    assert(
        metrics.lp_messages_enqueued_total.load(
            std::memory_order_relaxed) ==
        1u);
  }

} // namespace

int main()
{
  test_bridge_type_contracts();
  test_bridge_api_return_types();

  test_external_manager_is_reused();
  test_external_manager_changes_are_visible();
  test_bridge_changes_are_visible_to_external_manager();

  test_owning_bridge_constructs_manager();

  test_default_room_resolution();
  test_default_empty_room_resolution();

  test_same_room_reuses_same_session();
  test_different_rooms_create_different_sessions();

  test_custom_resolver_is_used();
  test_custom_resolver_can_return_empty_session_id();

  test_poll_delegates_to_manager();
  test_poll_missing_without_creation();
  test_poll_missing_with_creation();

  test_owning_bridge_respects_buffer_limit();
  test_owning_bridge_updates_metrics();

  return 0;
}
