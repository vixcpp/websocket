/**
 *
 * @file protocol_accept_key_test.cpp
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
#include <type_traits>
#include <utility>

#include <vix/websocket/protocol.hpp>

namespace
{
  using vix::websocket::detail::websocket_accept_from_key;

  static void test_function_return_type()
  {
    static_assert(
        std::is_same_v<
            decltype(websocket_accept_from_key(
                std::declval<const std::string &>())),
            std::string>);
  }

  static void test_rfc_6455_reference_key()
  {
    const std::string accept =
        websocket_accept_from_key(
            "dGhlIHNhbXBsZSBub25jZQ==");

    assert(
        accept ==
        "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
  }

  static void test_empty_client_key()
  {
    const std::string accept =
        websocket_accept_from_key("");

    assert(
        accept ==
        "Kfh9QIsMVZcl6xEPYxPHzW8SZ8w=");
  }

  static void test_simple_client_key()
  {
    const std::string accept =
        websocket_accept_from_key("test");

    assert(
        accept ==
        "tNpbgC8ZQDOcSkHAWopKzQjJ1hI=");
  }

  static void test_base64_client_key()
  {
    const std::string accept =
        websocket_accept_from_key(
            "MDEyMzQ1Njc4OWFiY2RlZg==");

    assert(
        accept ==
        "BACScCJPNqyz+UBoqMH89VmURoA=");
  }

  static void test_accept_key_has_expected_length()
  {
    const std::string accept =
        websocket_accept_from_key(
            "dGhlIHNhbXBsZSBub25jZQ==");

    /*
     * SHA-1 produces 20 bytes. Their Base64 representation
     * always contains 28 characters.
     */
    assert(accept.size() == 28u);
  }

  static void test_accept_key_has_expected_padding()
  {
    const std::string accept =
        websocket_accept_from_key(
            "dGhlIHNhbXBsZSBub25jZQ==");

    assert(!accept.empty());
    assert(accept.back() == '=');

    assert(accept.size() >= 2u);
    assert(accept[accept.size() - 2u] != '=');
  }

  static void test_accept_key_uses_base64_alphabet()
  {
    const std::string accept =
        websocket_accept_from_key(
            "MDEyMzQ1Njc4OWFiY2RlZg==");

    for (std::size_t i = 0; i < accept.size(); ++i)
    {
      const char ch = accept[i];

      const bool is_uppercase =
          ch >= 'A' && ch <= 'Z';

      const bool is_lowercase =
          ch >= 'a' && ch <= 'z';

      const bool is_digit =
          ch >= '0' && ch <= '9';

      const bool is_symbol =
          ch == '+' || ch == '/';

      const bool is_padding =
          ch == '=' && i == accept.size() - 1u;

      assert(
          is_uppercase ||
          is_lowercase ||
          is_digit ||
          is_symbol ||
          is_padding);
    }
  }

  static void test_same_key_produces_same_accept_key()
  {
    const std::string client_key =
        "dGhlIHNhbXBsZSBub25jZQ==";

    const std::string first =
        websocket_accept_from_key(client_key);

    const std::string second =
        websocket_accept_from_key(client_key);

    const std::string third =
        websocket_accept_from_key(client_key);

    assert(first == second);
    assert(second == third);
  }

  static void test_different_keys_produce_different_accept_keys()
  {
    const std::string first =
        websocket_accept_from_key(
            "MDEyMzQ1Njc4OWFiY2RlZg==");

    const std::string second =
        websocket_accept_from_key(
            "ZmVkY2JhOTg3NjU0MzIxMA==");

    assert(first != second);
  }

  static void test_client_key_is_not_modified()
  {
    const std::string client_key =
        "dGhlIHNhbXBsZSBub25jZQ==";

    const std::string original = client_key;

    const std::string accept =
        websocket_accept_from_key(client_key);

    assert(!accept.empty());
    assert(client_key == original);
  }

  static void test_case_sensitive_client_key()
  {
    const std::string lowercase =
        websocket_accept_from_key("websocket");

    const std::string uppercase =
        websocket_accept_from_key("WebSocket");

    assert(lowercase != uppercase);
  }

  static void test_whitespace_is_part_of_client_key()
  {
    const std::string plain =
        websocket_accept_from_key("test");

    const std::string leading_space =
        websocket_accept_from_key(" test");

    const std::string trailing_space =
        websocket_accept_from_key("test ");

    assert(plain != leading_space);
    assert(plain != trailing_space);
    assert(leading_space != trailing_space);
  }

  static void test_known_key_accept_pairs()
  {
    struct TestCase
    {
      const char *clientKey;
      const char *expectedAccept;
    };

    const TestCase cases[]{
        {
            "dGhlIHNhbXBsZSBub25jZQ==",
            "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=",
        },
        {
            "",
            "Kfh9QIsMVZcl6xEPYxPHzW8SZ8w=",
        },
        {
            "test",
            "tNpbgC8ZQDOcSkHAWopKzQjJ1hI=",
        },
        {
            "MDEyMzQ1Njc4OWFiY2RlZg==",
            "BACScCJPNqyz+UBoqMH89VmURoA=",
        },
    };

    for (const TestCase &test : cases)
    {
      assert(
          websocket_accept_from_key(test.clientKey) ==
          test.expectedAccept);
    }
  }

} // namespace

int main()
{
  test_function_return_type();

  test_rfc_6455_reference_key();
  test_empty_client_key();
  test_simple_client_key();
  test_base64_client_key();

  test_accept_key_has_expected_length();
  test_accept_key_has_expected_padding();
  test_accept_key_uses_base64_alphabet();

  test_same_key_produces_same_accept_key();
  test_different_keys_produce_different_accept_keys();

  test_client_key_is_not_modified();
  test_case_sensitive_client_key();
  test_whitespace_is_part_of_client_key();

  test_known_key_accept_pairs();

  return 0;
}
