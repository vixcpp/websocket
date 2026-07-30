/**
 *
 * @file long_polling_bridge_forwarding_test.cpp
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
#include <string>
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
    message.ts = "2026-07-30T10:00:00Z";
    message.room = std::move(room);
    message.type = std::move(type);

    return message;
  }

  static void test_send_from_http_without_forwarder_is_safe()
  {
    LongPollingManager manager;

    LongPollingBridge bridge{
        manager};

    bridge.send_from_http(
        "session-1",
        make_message(
            "message-1",
            "chat.message"));

    assert(true);
  }

  static void test_send_from_http_invokes_forwarder()
  {
    LongPollingManager manager;

    std::size_t calls = 0u;

    LongPollingBridge bridge{
        manager,
        LongPollingBridge::Resolver{},
        [&calls](const JsonMessage &)
        {
          ++calls;
        }};

    bridge.send_from_http(
        "session-1",
        make_message(
            "message-1",
            "chat.message"));

    assert(calls == 1u);
  }

  static void test_forwarder_receives_exact_message()
  {
    LongPollingManager manager;

    JsonMessage received;
    bool called = false;

    LongPollingBridge bridge{
        manager,
        LongPollingBridge::Resolver{},
        [&received, &called](
            const JsonMessage &message)
        {
          called = true;
          received = message;
        }};

    const JsonMessage original =
        make_message(
            "message-42",
            "chat.message",
            "general");

    bridge.send_from_http(
        "session-42",
        original);

    assert(called == true);

    assert(received.id == original.id);
    assert(received.kind == original.kind);
    assert(received.ts == original.ts);
    assert(received.room == original.room);
    assert(received.type == original.type);
  }

  static void test_forwarding_does_not_modify_original_message()
  {
    LongPollingManager manager;

    LongPollingBridge bridge{
        manager,
        LongPollingBridge::Resolver{},
        [](const JsonMessage &) {}};

    JsonMessage message =
        make_message(
            "message-1",
            "original.type",
            "general");

    const JsonMessage original =
        message;

    bridge.send_from_http(
        "session-1",
        message);

    assert(message.id == original.id);
    assert(message.kind == original.kind);
    assert(message.ts == original.ts);
    assert(message.room == original.room);
    assert(message.type == original.type);
  }

  static void test_multiple_http_messages_are_forwarded_in_order()
  {
    LongPollingManager manager;

    std::vector<std::string> receivedIds;

    LongPollingBridge bridge{
        manager,
        LongPollingBridge::Resolver{},
        [&receivedIds](
            const JsonMessage &message)
        {
          receivedIds.push_back(
              message.id);
        }};

    bridge.send_from_http(
        "session-1",
        make_message(
            "message-1",
            "first"));

    bridge.send_from_http(
        "session-1",
        make_message(
            "message-2",
            "second"));

    bridge.send_from_http(
        "session-1",
        make_message(
            "message-3",
            "third"));

    assert(receivedIds.size() == 3u);

    assert(receivedIds[0] == "message-1");
    assert(receivedIds[1] == "message-2");
    assert(receivedIds[2] == "message-3");
  }

  static void test_forwarding_is_synchronous()
  {
    LongPollingManager manager;

    bool completed = false;

    LongPollingBridge bridge{
        manager,
        LongPollingBridge::Resolver{},
        [&completed](
            const JsonMessage &)
        {
          completed = true;
        }};

    bridge.send_from_http(
        "session-1",
        make_message(
            "message-1",
            "chat.message"));

    assert(completed == true);
  }

  static void test_forwarder_can_preserve_state()
  {
    LongPollingManager manager;

    std::size_t totalMessages = 0u;
    std::size_t totalTypeLength = 0u;

    LongPollingBridge bridge{
        manager,
        LongPollingBridge::Resolver{},
        [&totalMessages, &totalTypeLength](
            const JsonMessage &message)
        {
          ++totalMessages;
          totalTypeLength += message.type.size();
        }};

    bridge.send_from_http(
        "session-1",
        make_message(
            "message-1",
            "abc"));

    bridge.send_from_http(
        "session-1",
        make_message(
            "message-2",
            "12345"));

    assert(totalMessages == 2u);
    assert(totalTypeLength == 8u);
  }

  static void test_empty_session_id_can_be_forwarded()
  {
    LongPollingManager manager;

    bool called = false;

    LongPollingBridge bridge{
        manager,
        LongPollingBridge::Resolver{},
        [&called](
            const JsonMessage &message)
        {
          called = true;

          assert(
              message.id ==
              "message-1");
        }};

    bridge.send_from_http(
        "",
        make_message(
            "message-1",
            "chat.message"));

    assert(called == true);
  }

  static void test_ws_message_does_not_use_http_forwarder()
  {
    LongPollingManager manager;

    std::size_t forwardCalls = 0u;

    LongPollingBridge bridge{
        manager,
        LongPollingBridge::Resolver{},
        [&forwardCalls](
            const JsonMessage &)
        {
          ++forwardCalls;
        }};

    bridge.on_ws_message(
        make_message(
            "message-1",
            "chat.message",
            "general"));

    assert(forwardCalls == 0u);

    assert(
        bridge.buffer_size(
            "room:general") ==
        1u);
  }

  static void test_http_forwarder_can_push_response_to_manager()
  {
    LongPollingManager manager;

    LongPollingBridge bridge{
        manager,
        LongPollingBridge::Resolver{},
        [&manager](
            const JsonMessage &message)
        {
          JsonMessage response =
              message;

          response.kind = "event";
          response.type = "forwarded." + message.type;

          manager.push_to(
              "responses",
              response);
        }};

    bridge.send_from_http(
        "session-1",
        make_message(
            "message-1",
            "chat.message",
            "general"));

    assert(bridge.session_count() == 2u);
    assert(bridge.buffer_size("responses") == 1u);

    const auto responses =
        bridge.poll(
            "responses",
            10u,
            false);

    assert(responses.size() == 1u);

    assert(responses[0].id == "message-1");

    assert(
        responses[0].type ==
        "forwarded.chat.message");

    assert(responses[0].room == "general");
  }

  static void test_owning_bridge_supports_http_forwarding()
  {
    WebSocketMetrics metrics;

    std::size_t calls = 0u;
    JsonMessage received;

    LongPollingBridge bridge{
        &metrics,
        std::chrono::seconds{60},
        16u,
        LongPollingBridge::Resolver{},
        [&calls, &received](
            const JsonMessage &message)
        {
          ++calls;
          received = message;
        }};

    bridge.send_from_http(
        "session-1",
        make_message(
            "message-1",
            "chat.message",
            "general"));

    assert(calls == 1u);

    assert(received.id == "message-1");
    assert(received.type == "chat.message");
    assert(received.room == "general");
  }

  static void test_custom_resolver_controls_ws_forwarding_only()
  {
    LongPollingManager manager;

    std::size_t resolverCalls = 0u;
    std::size_t forwardCalls = 0u;

    LongPollingBridge bridge{
        manager,
        [&resolverCalls](
            const JsonMessage &message)
        {
          ++resolverCalls;

          return std::string{"custom:"} +
                 message.id;
        },
        [&forwardCalls](
            const JsonMessage &)
        {
          ++forwardCalls;
        }};

    bridge.on_ws_message(
        make_message(
            "message-1",
            "from.websocket",
            "general"));

    assert(resolverCalls == 1u);
    assert(forwardCalls == 0u);

    assert(
        bridge.buffer_size(
            "custom:message-1") ==
        1u);

    bridge.send_from_http(
        "session-1",
        make_message(
            "message-2",
            "from.http"));

    assert(forwardCalls == 1u);
  }

  static void test_separate_bridges_forward_independently()
  {
    LongPollingManager firstManager;
    LongPollingManager secondManager;

    std::size_t firstCalls = 0u;
    std::size_t secondCalls = 0u;

    LongPollingBridge first{
        firstManager,
        LongPollingBridge::Resolver{},
        [&firstCalls](
            const JsonMessage &)
        {
          ++firstCalls;
        }};

    LongPollingBridge second{
        secondManager,
        LongPollingBridge::Resolver{},
        [&secondCalls](
            const JsonMessage &)
        {
          ++secondCalls;
        }};

    first.send_from_http(
        "first-session",
        make_message(
            "message-1",
            "first"));

    first.send_from_http(
        "first-session",
        make_message(
            "message-2",
            "first"));

    second.send_from_http(
        "second-session",
        make_message(
            "message-3",
            "second"));

    assert(firstCalls == 2u);
    assert(secondCalls == 1u);
  }

} // namespace

int main()
{
  test_send_from_http_without_forwarder_is_safe();
  test_send_from_http_invokes_forwarder();

  test_forwarder_receives_exact_message();
  test_forwarding_does_not_modify_original_message();

  test_multiple_http_messages_are_forwarded_in_order();
  test_forwarding_is_synchronous();
  test_forwarder_can_preserve_state();

  test_empty_session_id_can_be_forwarded();

  test_ws_message_does_not_use_http_forwarder();

  test_http_forwarder_can_push_response_to_manager();
  test_owning_bridge_supports_http_forwarding();

  test_custom_resolver_controls_ws_forwarding_only();
  test_separate_bridges_forward_independently();

  return 0;
}
