/**
 *
 * @file protocol_client_key_test.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira. All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license that can be found in the License file.
 *
 * Vix.cpp
 *
 */

#include <array>
#include <cassert>
#include <cstddef>
#include <set>
#include <string>
#include <type_traits>

#include <vix/websocket/protocol.hpp>

namespace
{
  using vix::websocket::detail::generate_websocket_key;

  static bool is_base64_character(char ch)
  {
    const bool is_uppercase =
        ch >= 'A' && ch <= 'Z';

    const bool is_lowercase =
        ch >= 'a' && ch <= 'z';

    const bool is_digit =
        ch >= '0' && ch <= '9';

    return is_uppercase ||
           is_lowercase ||
           is_digit ||
           ch == '+' ||
           ch == '/';
  }

  static void assert_valid_client_key(
      const std::string &key)
  {
    /*
     * A WebSocket client key represents exactly 16 random bytes.
     *
     * Base64 encoding 16 bytes produces 24 characters:
     * 22 data characters followed by "==".
     */
    assert(key.size() == 24u);

    assert(key[22] == '=');
    assert(key[23] == '=');

    for (std::size_t i = 0; i < 22u; ++i)
    {
      assert(is_base64_character(key[i]));
    }
  }

  static void test_function_return_type()
  {
    static_assert(
        std::is_same_v<
            decltype(generate_websocket_key()),
            std::string>);
  }

  static void test_generated_key_is_not_empty()
  {
    const std::string key =
        generate_websocket_key();

    assert(!key.empty());
  }

  static void test_generated_key_has_expected_length()
  {
    const std::string key =
        generate_websocket_key();

    assert(key.size() == 24u);
  }

  static void test_generated_key_has_expected_padding()
  {
    const std::string key =
        generate_websocket_key();

    assert(key.size() == 24u);
    assert(key.ends_with("=="));
  }

  static void test_generated_key_uses_base64_alphabet()
  {
    const std::string key =
        generate_websocket_key();

    assert_valid_client_key(key);
  }

  static void test_multiple_generated_keys_are_valid()
  {
    constexpr std::size_t count = 256u;

    for (std::size_t i = 0; i < count; ++i)
    {
      const std::string key =
          generate_websocket_key();

      assert_valid_client_key(key);
    }
  }

  static void test_generated_keys_are_not_constant()
  {
    constexpr std::size_t count = 64u;

    std::set<std::string> keys;

    for (std::size_t i = 0; i < count; ++i)
    {
      keys.insert(generate_websocket_key());
    }

    /*
     * This does not demand perfect uniqueness. It only protects against
     * an implementation accidentally returning one constant key.
     */
    assert(keys.size() > 1u);
  }

  static void test_generated_keys_have_high_uniqueness()
  {
    constexpr std::size_t count = 256u;

    std::set<std::string> keys;

    for (std::size_t i = 0; i < count; ++i)
    {
      keys.insert(generate_websocket_key());
    }

    /*
     * With 128 bits of random input, collisions in this sample should be
     * practically impossible. A lower threshold avoids turning the test
     * into a strict probabilistic equality check.
     */
    assert(keys.size() >= count - 1u);
  }

  static void test_generated_key_contains_no_whitespace()
  {
    const std::string key =
        generate_websocket_key();

    for (const char ch : key)
    {
      assert(ch != ' ');
      assert(ch != '\t');
      assert(ch != '\n');
      assert(ch != '\r');
    }
  }

  static void test_generated_key_contains_no_http_delimiters()
  {
    const std::string key =
        generate_websocket_key();

    assert(key.find(':') == std::string::npos);
    assert(key.find(';') == std::string::npos);
    assert(key.find(',') == std::string::npos);

    assert(key.find('\r') == std::string::npos);
    assert(key.find('\n') == std::string::npos);
  }

  static void test_generated_key_is_safe_for_http_header()
  {
    const std::string key =
        generate_websocket_key();

    const std::string header =
        "Sec-WebSocket-Key: " + key + "\r\n";

    assert(
        header ==
        std::string{"Sec-WebSocket-Key: "} +
            key +
            "\r\n");

    assert(
        header.find(
            "\r\n",
            std::string{"Sec-WebSocket-Key: "}.size()) ==
        std::string{"Sec-WebSocket-Key: "}.size() +
            key.size());
  }

  static void test_consecutive_keys_remain_valid()
  {
    const std::array<std::string, 8> keys{
        generate_websocket_key(),
        generate_websocket_key(),
        generate_websocket_key(),
        generate_websocket_key(),
        generate_websocket_key(),
        generate_websocket_key(),
        generate_websocket_key(),
        generate_websocket_key(),
    };

    for (const std::string &key : keys)
    {
      assert_valid_client_key(key);
    }
  }

  static void test_generated_key_can_produce_accept_key()
  {
    const std::string client_key =
        generate_websocket_key();

    const std::string accept_key =
        vix::websocket::detail::websocket_accept_from_key(
            client_key);

    assert_valid_client_key(client_key);

    assert(!accept_key.empty());
    assert(accept_key.size() == 28u);
    assert(accept_key.ends_with("="));
  }

  static void test_different_client_keys_produce_accept_keys()
  {
    std::string first_client_key =
        generate_websocket_key();

    std::string second_client_key =
        generate_websocket_key();

    /*
     * A collision is extraordinarily unlikely, but generating again keeps
     * the test deterministic in intent.
     */
    for (
        std::size_t attempt = 0;
        attempt < 8u &&
        first_client_key == second_client_key;
        ++attempt)
    {
      second_client_key =
          generate_websocket_key();
    }

    assert(first_client_key != second_client_key);

    const std::string first_accept_key =
        vix::websocket::detail::websocket_accept_from_key(
            first_client_key);

    const std::string second_accept_key =
        vix::websocket::detail::websocket_accept_from_key(
            second_client_key);

    assert(first_accept_key != second_accept_key);
  }

} // namespace

int main()
{
  test_function_return_type();

  test_generated_key_is_not_empty();
  test_generated_key_has_expected_length();
  test_generated_key_has_expected_padding();
  test_generated_key_uses_base64_alphabet();

  test_multiple_generated_keys_are_valid();
  test_generated_keys_are_not_constant();
  test_generated_keys_have_high_uniqueness();

  test_generated_key_contains_no_whitespace();
  test_generated_key_contains_no_http_delimiters();
  test_generated_key_is_safe_for_http_header();

  test_consecutive_keys_remain_valid();

  test_generated_key_can_produce_accept_key();
  test_different_client_keys_produce_accept_keys();

  return 0;
}
