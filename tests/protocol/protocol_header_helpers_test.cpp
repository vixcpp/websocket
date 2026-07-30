/**
 *
 * @file protocol_header_helpers_test.cpp
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
#include <string>

#include <vix/websocket/protocol.hpp>

namespace
{
  using vix::websocket::detail::get_header_value;
  using vix::websocket::detail::header_contains_token;
  using vix::websocket::detail::to_lower_copy;
  using vix::websocket::detail::trim_copy;

  static void test_trim_empty_string()
  {
    assert(trim_copy("").empty());
  }

  static void test_trim_string_without_whitespace()
  {
    assert(trim_copy("websocket") == "websocket");
  }

  static void test_trim_leading_whitespace()
  {
    assert(trim_copy("   websocket") == "websocket");
    assert(trim_copy("\twebsocket") == "websocket");
    assert(trim_copy("\r\nwebsocket") == "websocket");
  }

  static void test_trim_trailing_whitespace()
  {
    assert(trim_copy("websocket   ") == "websocket");
    assert(trim_copy("websocket\t") == "websocket");
    assert(trim_copy("websocket\r\n") == "websocket");
  }

  static void test_trim_both_sides()
  {
    assert(
        trim_copy(" \t websocket \r\n") ==
        "websocket");
  }

  static void test_trim_preserves_internal_whitespace()
  {
    assert(
        trim_copy("  keep internal spaces  ") ==
        "keep internal spaces");
  }

  static void test_trim_whitespace_only_string()
  {
    assert(trim_copy("   \t\r\n").empty());
  }

  static void test_lowercase_empty_string()
  {
    assert(to_lower_copy("").empty());
  }

  static void test_lowercase_already_lowercase_string()
  {
    assert(
        to_lower_copy("websocket") ==
        "websocket");
  }

  static void test_lowercase_uppercase_string()
  {
    assert(
        to_lower_copy("WEBSOCKET") ==
        "websocket");
  }

  static void test_lowercase_mixed_string()
  {
    assert(
        to_lower_copy("Sec-WebSocket-Key") ==
        "sec-websocket-key");
  }

  static void test_lowercase_preserves_non_letters()
  {
    assert(
        to_lower_copy("HTTP/1.1 101") ==
        "http/1.1 101");

    assert(
        to_lower_copy("A-B_C:123") ==
        "a-b_c:123");
  }

  static void test_get_header_value_from_valid_request()
  {
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "\r\n";

    assert(
        get_header_value(request, "Host") ==
        "example.com");

    assert(
        get_header_value(request, "Upgrade") ==
        "websocket");

    assert(
        get_header_value(request, "Connection") ==
        "Upgrade");

    assert(
        get_header_value(request, "Sec-WebSocket-Key") ==
        "dGhlIHNhbXBsZSBub25jZQ==");
  }

  static void test_get_header_value_is_case_insensitive()
  {
    const std::string request =
        "GET / HTTP/1.1\r\n"
        "uPgRaDe: WebSocket\r\n"
        "cOnNeCtIoN: keep-alive, Upgrade\r\n"
        "\r\n";

    assert(
        get_header_value(request, "upgrade") ==
        "WebSocket");

    assert(
        get_header_value(request, "UPGRADE") ==
        "WebSocket");

    assert(
        get_header_value(request, "connection") ==
        "keep-alive, Upgrade");
  }

  static void test_get_header_value_trims_value()
  {
    const std::string request =
        "GET / HTTP/1.1\r\n"
        "Upgrade:    websocket   \r\n"
        "Connection:\tUpgrade\t\r\n"
        "\r\n";

    assert(
        get_header_value(request, "Upgrade") ==
        "websocket");

    assert(
        get_header_value(request, "Connection") ==
        "Upgrade");
  }

  static void test_get_header_value_missing_header()
  {
    const std::string request =
        "GET / HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";

    assert(
        get_header_value(request, "Upgrade").empty());

    assert(
        get_header_value(request, "Connection").empty());
  }

  static void test_get_header_value_empty_input()
  {
    assert(
        get_header_value("", "Upgrade").empty());
  }

  static void test_get_header_value_empty_header_name()
  {
    const std::string request =
        "GET / HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "\r\n";

    assert(
        get_header_value(request, "").empty());
  }

  static void test_get_header_value_empty_value()
  {
    const std::string request =
        "GET / HTTP/1.1\r\n"
        "Upgrade:\r\n"
        "\r\n";

    assert(
        get_header_value(request, "Upgrade").empty());
  }

  static void test_get_header_value_returns_first_duplicate()
  {
    const std::string request =
        "GET / HTTP/1.1\r\n"
        "X-Test: first\r\n"
        "X-Test: second\r\n"
        "\r\n";

    assert(
        get_header_value(request, "X-Test") ==
        "first");
  }

  static void test_get_header_does_not_match_partial_name()
  {
    const std::string request =
        "GET / HTTP/1.1\r\n"
        "X-Upgrade: custom\r\n"
        "\r\n";

    assert(
        get_header_value(request, "Upgrade").empty());
  }

  static void test_get_header_without_final_crlf()
  {
    const std::string request =
        "GET / HTTP/1.1\r\n"
        "Upgrade: websocket";

    assert(
        get_header_value(request, "Upgrade") ==
        "websocket");
  }

  static void test_header_contains_single_token()
  {
    const std::string request =
        "GET / HTTP/1.1\r\n"
        "Connection: Upgrade\r\n"
        "\r\n";

    assert(
        header_contains_token(
            request,
            "Connection",
            "Upgrade"));
  }

  static void test_header_contains_token_is_case_insensitive()
  {
    const std::string request =
        "GET / HTTP/1.1\r\n"
        "Connection: uPgRaDe\r\n"
        "\r\n";

    assert(
        header_contains_token(
            request,
            "connection",
            "upgrade"));

    assert(
        header_contains_token(
            request,
            "CONNECTION",
            "UPGRADE"));
  }

  static void test_header_contains_first_comma_token()
  {
    const std::string request =
        "GET / HTTP/1.1\r\n"
        "Connection: keep-alive, Upgrade\r\n"
        "\r\n";

    assert(
        header_contains_token(
            request,
            "Connection",
            "keep-alive"));
  }

  static void test_header_contains_last_comma_token()
  {
    const std::string request =
        "GET / HTTP/1.1\r\n"
        "Connection: keep-alive, Upgrade\r\n"
        "\r\n";

    assert(
        header_contains_token(
            request,
            "Connection",
            "Upgrade"));
  }

  static void test_header_contains_middle_comma_token()
  {
    const std::string request =
        "GET / HTTP/1.1\r\n"
        "Connection: keep-alive, Upgrade, close\r\n"
        "\r\n";

    assert(
        header_contains_token(
            request,
            "Connection",
            "Upgrade"));
  }

  static void test_header_contains_token_trims_parts()
  {
    const std::string request =
        "GET / HTTP/1.1\r\n"
        "Connection:  keep-alive ,  Upgrade  , close \r\n"
        "\r\n";

    assert(
        header_contains_token(
            request,
            "Connection",
            "keep-alive"));

    assert(
        header_contains_token(
            request,
            "Connection",
            "Upgrade"));

    assert(
        header_contains_token(
            request,
            "Connection",
            "close"));
  }

  static void test_header_contains_token_requires_exact_token()
  {
    const std::string request =
        "GET / HTTP/1.1\r\n"
        "Connection: WebSocket-Upgrade\r\n"
        "\r\n";

    assert(
        !header_contains_token(
            request,
            "Connection",
            "Upgrade"));
  }

  static void test_header_contains_token_missing_header()
  {
    const std::string request =
        "GET / HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";

    assert(
        !header_contains_token(
            request,
            "Connection",
            "Upgrade"));
  }

  static void test_header_contains_missing_token()
  {
    const std::string request =
        "GET / HTTP/1.1\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";

    assert(
        !header_contains_token(
            request,
            "Connection",
            "Upgrade"));
  }

  static void test_header_contains_empty_value()
  {
    const std::string request =
        "GET / HTTP/1.1\r\n"
        "Connection:\r\n"
        "\r\n";

    assert(
        !header_contains_token(
            request,
            "Connection",
            "Upgrade"));
  }

} // namespace

int main()
{
  test_trim_empty_string();
  test_trim_string_without_whitespace();
  test_trim_leading_whitespace();
  test_trim_trailing_whitespace();
  test_trim_both_sides();
  test_trim_preserves_internal_whitespace();
  test_trim_whitespace_only_string();

  test_lowercase_empty_string();
  test_lowercase_already_lowercase_string();
  test_lowercase_uppercase_string();
  test_lowercase_mixed_string();
  test_lowercase_preserves_non_letters();

  test_get_header_value_from_valid_request();
  test_get_header_value_is_case_insensitive();
  test_get_header_value_trims_value();
  test_get_header_value_missing_header();
  test_get_header_value_empty_input();
  test_get_header_value_empty_header_name();
  test_get_header_value_empty_value();
  test_get_header_value_returns_first_duplicate();
  test_get_header_does_not_match_partial_name();
  test_get_header_without_final_crlf();

  test_header_contains_single_token();
  test_header_contains_token_is_case_insensitive();
  test_header_contains_first_comma_token();
  test_header_contains_last_comma_token();
  test_header_contains_middle_comma_token();
  test_header_contains_token_trims_parts();
  test_header_contains_token_requires_exact_token();
  test_header_contains_token_missing_header();
  test_header_contains_missing_token();
  test_header_contains_empty_value();

  return 0;
}
