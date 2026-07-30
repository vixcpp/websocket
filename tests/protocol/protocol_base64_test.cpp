/**
 *
 * @file protocol_base64_test.cpp
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
#include <string>
#include <vector>

#include <vix/websocket/protocol.hpp>

namespace
{
  using vix::websocket::detail::base64_encode;

  static std::string encode(const std::string &value)
  {
    return base64_encode(
        reinterpret_cast<const unsigned char *>(value.data()),
        value.size());
  }

  static std::string encode(
      const std::vector<unsigned char> &value)
  {
    return base64_encode(
        value.data(),
        value.size());
  }

  static void test_empty_input()
  {
    const std::array<unsigned char, 1> storage{0u};

    const std::string encoded =
        base64_encode(storage.data(), 0u);

    assert(encoded.empty());
  }

  static void test_single_character_input()
  {
    assert(encode("f") == "Zg==");
  }

  static void test_two_character_input()
  {
    assert(encode("fo") == "Zm8=");
  }

  static void test_three_character_input()
  {
    assert(encode("foo") == "Zm9v");
  }

  static void test_four_character_input()
  {
    assert(encode("foob") == "Zm9vYg==");
  }

  static void test_five_character_input()
  {
    assert(encode("fooba") == "Zm9vYmE=");
  }

  static void test_six_character_input()
  {
    assert(encode("foobar") == "Zm9vYmFy");
  }

  static void test_common_text_input()
  {
    assert(
        encode("Hello, World!") ==
        "SGVsbG8sIFdvcmxkIQ==");

    assert(
        encode("Vix.cpp WebSocket") ==
        "Vml4LmNwcCBXZWJTb2NrZXQ=");
  }

  static void test_space_and_newline_are_encoded()
  {
    assert(encode(" ") == "IA==");
    assert(encode("\n") == "Cg==");
    assert(encode("\r\n") == "DQo=");
  }

  static void test_embedded_null_bytes()
  {
    const std::vector<unsigned char> input{
        static_cast<unsigned char>('a'),
        0x00u,
        static_cast<unsigned char>('b')};

    assert(encode(input) == "YQBi");
  }

  static void test_single_zero_byte()
  {
    const std::vector<unsigned char> input{
        0x00u};

    assert(encode(input) == "AA==");
  }

  static void test_single_high_byte()
  {
    const std::vector<unsigned char> input{
        0xFFu};

    assert(encode(input) == "/w==");
  }

  static void test_binary_values_using_plus_and_slash()
  {
    const std::vector<unsigned char> input{
        0xFBu,
        0xFFu,
        0xFFu};

    assert(encode(input) == "+///");
  }

  static void test_binary_sequence()
  {
    const std::vector<unsigned char> input{
        0x00u,
        0x01u,
        0x02u,
        0x03u,
        0x04u,
        0x05u,
        0x06u,
        0x07u,
        0x08u,
        0x09u,
        0x0Au,
        0x0Bu,
        0x0Cu,
        0x0Du,
        0x0Eu,
        0x0Fu};

    assert(
        encode(input) ==
        "AAECAwQFBgcICQoLDA0ODw==");
  }

  static void test_output_length_for_complete_groups()
  {
    const std::string input = "abcdef";

    const std::string encoded = encode(input);

    assert(encoded.size() == 8u);
    assert(encoded == "YWJjZGVm");
  }

  static void test_output_length_for_one_byte_remainder()
  {
    const std::string input = "abcd";

    const std::string encoded = encode(input);

    assert(encoded.size() == 8u);
    assert(encoded == "YWJjZA==");
    assert(encoded.ends_with("=="));
  }

  static void test_output_length_for_two_byte_remainder()
  {
    const std::string input = "abcde";

    const std::string encoded = encode(input);

    assert(encoded.size() == 8u);
    assert(encoded == "YWJjZGU=");
    assert(encoded.ends_with("="));
    assert(!encoded.ends_with("=="));
  }

  static void test_output_length_is_multiple_of_four()
  {
    for (std::size_t size = 0; size <= 128u; ++size)
    {
      const std::vector<unsigned char> input(
          size,
          static_cast<unsigned char>('a'));

      const std::string encoded = encode(input);

      assert(encoded.size() % 4u == 0u);

      const std::size_t expected_size =
          ((size + 2u) / 3u) * 4u;

      assert(encoded.size() == expected_size);
    }
  }

  static void test_repeated_calls_are_deterministic()
  {
    const std::string input =
        "deterministic-websocket-key-material";

    const std::string first = encode(input);
    const std::string second = encode(input);
    const std::string third = encode(input);

    assert(first == second);
    assert(second == third);
  }

  static void test_input_is_not_modified()
  {
    const std::vector<unsigned char> input{
        0x10u,
        0x20u,
        0x30u,
        0x40u,
        0x50u};

    const std::vector<unsigned char> original = input;

    const std::string encoded = encode(input);

    assert(!encoded.empty());
    assert(input == original);
  }

} // namespace

int main()
{
  test_empty_input();

  test_single_character_input();
  test_two_character_input();
  test_three_character_input();
  test_four_character_input();
  test_five_character_input();
  test_six_character_input();

  test_common_text_input();
  test_space_and_newline_are_encoded();

  test_embedded_null_bytes();
  test_single_zero_byte();
  test_single_high_byte();
  test_binary_values_using_plus_and_slash();
  test_binary_sequence();

  test_output_length_for_complete_groups();
  test_output_length_for_one_byte_remainder();
  test_output_length_for_two_byte_remainder();
  test_output_length_is_multiple_of_four();

  test_repeated_calls_are_deterministic();
  test_input_is_not_modified();

  return 0;
}
