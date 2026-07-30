/**
 *
 * @file protocol_sha1_test.cpp
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
#include <iomanip>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include <vix/websocket/protocol.hpp>

namespace
{
  using Digest =
      std::array<unsigned char, 20>;

  using vix::websocket::detail::sha1_bytes;

  static Digest digest(const std::string &value)
  {
    return sha1_bytes(
        reinterpret_cast<const unsigned char *>(value.data()),
        value.size());
  }

  static Digest digest(
      const std::vector<unsigned char> &value)
  {
    return sha1_bytes(
        value.data(),
        value.size());
  }

  static std::string to_hex(const Digest &value)
  {
    std::ostringstream output;

    output
        << std::hex
        << std::setfill('0');

    for (const unsigned char byte : value)
    {
      output
          << std::setw(2)
          << static_cast<unsigned int>(byte);
    }

    return output.str();
  }

  static void test_digest_type_contract()
  {
    static_assert(
        std::is_same_v<
            decltype(sha1_bytes(
                static_cast<const unsigned char *>(nullptr),
                std::size_t{0})),
            Digest>);

    static_assert(
        std::tuple_size_v<Digest> == 20u);

    static_assert(
        sizeof(Digest) == 20u);
  }

  static void test_empty_input()
  {
    const std::array<unsigned char, 1> storage{0u};

    const Digest value =
        sha1_bytes(storage.data(), 0u);

    assert(
        to_hex(value) ==
        "da39a3ee5e6b4b0d3255bfef95601890afd80709");
  }

  static void test_single_character()
  {
    assert(
        to_hex(digest("a")) ==
        "86f7e437faa5a7fce15d1ddcb9eaeaea377667b8");
  }

  static void test_abc_reference_vector()
  {
    assert(
        to_hex(digest("abc")) ==
        "a9993e364706816aba3e25717850c26c9cd0d89d");
  }

  static void test_long_reference_vector()
  {
    const std::string input =
        "abcdbcdecdefdefgefghfghighij"
        "hijkijkljklmklmnlmnomnopnopq";

    assert(
        to_hex(digest(input)) ==
        "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
  }

  static void test_quick_brown_fox_vector()
  {
    assert(
        to_hex(
            digest(
                "The quick brown fox jumps over the lazy dog")) ==
        "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12");
  }

  static void test_quick_brown_fox_period_vector()
  {
    assert(
        to_hex(
            digest(
                "The quick brown fox jumps over the lazy dog.")) ==
        "408d94384216f890ff7a0c3528e8bed1e0b01621");
  }

  static void test_case_sensitivity()
  {
    const Digest lowercase = digest("websocket");
    const Digest uppercase = digest("WebSocket");

    assert(lowercase != uppercase);

    assert(
        to_hex(lowercase) ==
        "b27046fda0594576f3b1bc6c50ddf06529e415a0");

    assert(
        to_hex(uppercase) ==
        "9bebd32cb4afc55d8c0c71e6ac3ac76cd6e865a4");
  }

  static void test_embedded_null_bytes()
  {
    const std::vector<unsigned char> input{
        static_cast<unsigned char>('a'),
        0x00u,
        static_cast<unsigned char>('b')};

    assert(
        to_hex(digest(input)) ==
        "4a3dec2d1f8245280855c42db0ee4239f917fdb8");
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
        to_hex(digest(input)) ==
        "56178b86a57fac22899a9964185c2cc96e7da589");
  }

  static void test_padding_boundary_at_55_bytes()
  {
    const std::string input(55u, 'a');

    assert(
        to_hex(digest(input)) ==
        "c1c8bbdc22796e28c0e15163d20899b65621d65a");
  }

  static void test_padding_boundary_at_56_bytes()
  {
    const std::string input(56u, 'a');

    assert(
        to_hex(digest(input)) ==
        "c2db330f6083854c99d4b5bfb6e8f29f201be699");
  }

  static void test_exact_block_size()
  {
    const std::string input(64u, 'a');

    assert(
        to_hex(digest(input)) ==
        "0098ba824b5c16427bd7a1122a5a442a25ec644d");
  }

  static void test_one_byte_over_block_size()
  {
    const std::string input(65u, 'a');

    assert(
        to_hex(digest(input)) ==
        "11655326c708d70319be2610e8a57d9a5b959d3b");
  }

  static void test_million_a_reference_vector()
  {
    const std::string input(
        1'000'000u,
        'a');

    assert(
        to_hex(digest(input)) ==
        "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
  }

  static void test_digest_has_twenty_bytes()
  {
    const Digest value = digest("Vix.cpp");

    assert(value.size() == 20u);
  }

  static void test_hex_digest_has_forty_characters()
  {
    const std::string value =
        to_hex(digest("Vix.cpp"));

    assert(value.size() == 40u);
  }

  static void test_repeated_calls_are_deterministic()
  {
    const std::string input =
        "WebSocket deterministic SHA-1 input";

    const Digest first = digest(input);
    const Digest second = digest(input);
    const Digest third = digest(input);

    assert(first == second);
    assert(second == third);
  }

  static void test_different_inputs_produce_different_digests()
  {
    const Digest first = digest("message-1");
    const Digest second = digest("message-2");

    assert(first != second);
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

    const Digest value = digest(input);

    assert(value.size() == 20u);
    assert(input == original);
  }

} // namespace

int main()
{
  test_digest_type_contract();

  test_empty_input();
  test_single_character();
  test_abc_reference_vector();
  test_long_reference_vector();

  test_quick_brown_fox_vector();
  test_quick_brown_fox_period_vector();
  test_case_sensitivity();

  test_embedded_null_bytes();
  test_binary_sequence();

  test_padding_boundary_at_55_bytes();
  test_padding_boundary_at_56_bytes();
  test_exact_block_size();
  test_one_byte_over_block_size();

  test_million_a_reference_vector();

  test_digest_has_twenty_bytes();
  test_hex_digest_has_forty_characters();

  test_repeated_calls_are_deterministic();
  test_different_inputs_produce_different_digests();
  test_input_is_not_modified();

  return 0;
}
