/**
 *
 * @file websocket_disconnect_tests.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira. All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license that can be found in the License file.
 *
 * Vix.cpp
 *
 */

#include <cstdlib>
#include <iostream>
#include <string>

#include <vix/utils/NetworkError.hpp>

namespace
{
  static int failures = 0;

  static void expect_true(
      bool condition,
      const std::string &name)
  {
    if (!condition)
    {
      std::cerr
          << "[FAIL] "
          << name
          << ": expected true\n";

      ++failures;
    }
  }

  static void expect_false(
      bool condition,
      const std::string &name)
  {
    if (condition)
    {
      std::cerr
          << "[FAIL] "
          << name
          << ": expected false\n";

      ++failures;
    }
  }

  static void test_broken_pipe_is_normal_disconnect()
  {
    expect_true(
        vix::utils::is_normal_network_disconnect_message(
            "Broken pipe"),
        "Broken pipe");

    expect_true(
        vix::utils::is_normal_network_disconnect_message(
            "broken pipe"),
        "broken pipe");

    expect_true(
        vix::utils::is_normal_network_disconnect_message(
            "websocket write failed: Broken pipe"),
        "Broken pipe inside a larger message");
  }

  static void test_connection_reset_is_normal_disconnect()
  {
    expect_true(
        vix::utils::is_normal_network_disconnect_message(
            "connection reset"),
        "connection reset");

    expect_true(
        vix::utils::is_normal_network_disconnect_message(
            "Connection reset"),
        "Connection reset");

    expect_true(
        vix::utils::is_normal_network_disconnect_message(
            "Connection reset by peer"),
        "Connection reset by peer");

    expect_true(
        vix::utils::is_normal_network_disconnect_message(
            "read failed: connection reset by peer"),
        "connection reset inside a larger message");
  }

  static void test_cancelled_operations_are_normal_disconnects()
  {
    expect_true(
        vix::utils::is_normal_network_disconnect_message(
            "operation canceled"),
        "operation canceled");

    expect_true(
        vix::utils::is_normal_network_disconnect_message(
            "operation cancelled"),
        "operation cancelled");

    expect_true(
        vix::utils::is_normal_network_disconnect_message(
            "Operation canceled"),
        "Operation canceled");

    expect_true(
        vix::utils::is_normal_network_disconnect_message(
            "Operation cancelled"),
        "Operation cancelled");
  }

  static void test_end_of_file_is_normal_disconnect()
  {
    expect_true(
        vix::utils::is_normal_network_disconnect_message(
            "EOF"),
        "EOF");

    expect_true(
        vix::utils::is_normal_network_disconnect_message(
            "eof"),
        "eof");

    expect_true(
        vix::utils::is_normal_network_disconnect_message(
            "unexpected EOF"),
        "unexpected EOF");

    expect_true(
        vix::utils::is_normal_network_disconnect_message(
            "Unexpected eof"),
        "Unexpected eof");

    expect_true(
        vix::utils::is_normal_network_disconnect_message(
            "end of file"),
        "end of file");
  }

  static void test_protocol_errors_are_not_normal_disconnects()
  {
    expect_false(
        vix::utils::is_normal_network_disconnect_message(
            "websocket handshake must use GET"),
        "handshake must use GET");

    expect_false(
        vix::utils::is_normal_network_disconnect_message(
            "missing Upgrade: websocket"),
        "missing Upgrade header");

    expect_false(
        vix::utils::is_normal_network_disconnect_message(
            "missing Connection: Upgrade"),
        "missing Connection header");

    expect_false(
        vix::utils::is_normal_network_disconnect_message(
            "missing Sec-WebSocket-Key"),
        "missing WebSocket key");

    expect_false(
        vix::utils::is_normal_network_disconnect_message(
            "unsupported Sec-WebSocket-Version"),
        "unsupported WebSocket version");
  }

  static void test_write_and_internal_errors_are_not_normal_disconnects()
  {
    expect_false(
        vix::utils::is_normal_network_disconnect_message(
            "websocket frame write failed"),
        "frame write failed");

    expect_false(
        vix::utils::is_normal_network_disconnect_message(
            "websocket handshake write failed"),
        "handshake write failed");

    expect_false(
        vix::utils::is_normal_network_disconnect_message(
            "stream not open"),
        "stream not open");

    expect_false(
        vix::utils::is_normal_network_disconnect_message(
            "failed to create native Vix TCP listener"),
        "listener creation failed");
  }

  static void test_empty_and_unknown_messages_are_not_normal_disconnects()
  {
    expect_false(
        vix::utils::is_normal_network_disconnect_message(""),
        "empty message");

    expect_false(
        vix::utils::is_normal_network_disconnect_message(
            "unknown websocket failure"),
        "unknown WebSocket failure");

    expect_false(
        vix::utils::is_normal_network_disconnect_message(
            "invalid websocket frame"),
        "invalid WebSocket frame");
  }

} // namespace

int main()
{
  test_broken_pipe_is_normal_disconnect();
  test_connection_reset_is_normal_disconnect();
  test_cancelled_operations_are_normal_disconnects();
  test_end_of_file_is_normal_disconnect();

  test_protocol_errors_are_not_normal_disconnects();
  test_write_and_internal_errors_are_not_normal_disconnects();
  test_empty_and_unknown_messages_are_not_normal_disconnects();

  if (failures != 0)
  {
    std::cerr
        << failures
        << " WebSocket disconnect test(s) failed\n";

    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
