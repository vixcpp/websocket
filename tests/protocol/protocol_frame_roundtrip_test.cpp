/**
 *
 * @file protocol_frame_roundtrip_test.cpp
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
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <vix/websocket/protocol.hpp>

namespace
{
  using Frame = vix::websocket::detail::Frame;
  using Opcode = vix::websocket::detail::Opcode;

  using vix::websocket::detail::build_frame;
  using vix::websocket::detail::decode_frame;

  static std::vector<std::byte> bytes_from_string(
      std::string_view value)
  {
    std::vector<std::byte> bytes;
    bytes.reserve(value.size());

    for (const char ch : value)
    {
      bytes.push_back(
          static_cast<std::byte>(
              static_cast<unsigned char>(ch)));
    }

    return bytes;
  }

  static std::vector<std::byte> make_pattern_payload(
      std::size_t size)
  {
    std::vector<std::byte> payload;
    payload.reserve(size);

    for (std::size_t i = 0; i < size; ++i)
    {
      payload.push_back(
          static_cast<std::byte>(
              static_cast<std::uint8_t>(i % 256u)));
    }

    return payload;
  }

  static void assert_roundtrip(
      Opcode opcode,
      const std::vector<std::byte> &payload,
      bool fin,
      bool masked)
  {
    const std::vector<std::byte> encoded =
        build_frame(
            opcode,
            payload,
            fin,
            masked);

    const Frame decoded =
        decode_frame(encoded);

    assert(decoded.fin == fin);
    assert(decoded.opcode == opcode);
    assert(decoded.masked == masked);
    assert(decoded.payload == payload);
  }

  static void test_empty_text_frame_roundtrip()
  {
    assert_roundtrip(
        Opcode::Text,
        {},
        true,
        false);
  }

  static void test_empty_masked_text_frame_roundtrip()
  {
    assert_roundtrip(
        Opcode::Text,
        {},
        true,
        true);
  }

  static void test_short_text_frame_roundtrip()
  {
    assert_roundtrip(
        Opcode::Text,
        bytes_from_string("Hello WebSocket"),
        true,
        false);
  }

  static void test_short_masked_text_frame_roundtrip()
  {
    assert_roundtrip(
        Opcode::Text,
        bytes_from_string("Hello WebSocket"),
        true,
        true);
  }

  static void test_non_final_text_frame_roundtrip()
  {
    assert_roundtrip(
        Opcode::Text,
        bytes_from_string("fragment"),
        false,
        false);
  }

  static void test_non_final_masked_text_frame_roundtrip()
  {
    assert_roundtrip(
        Opcode::Text,
        bytes_from_string("fragment"),
        false,
        true);
  }

  static void test_continuation_frame_roundtrip()
  {
    assert_roundtrip(
        Opcode::Continuation,
        bytes_from_string("continued payload"),
        true,
        false);
  }

  static void test_masked_continuation_frame_roundtrip()
  {
    assert_roundtrip(
        Opcode::Continuation,
        bytes_from_string("continued payload"),
        true,
        true);
  }

  static void test_binary_frame_roundtrip()
  {
    const std::vector<std::byte> payload{
        std::byte{0x00},
        std::byte{0x01},
        std::byte{0x7F},
        std::byte{0x80},
        std::byte{0xFE},
        std::byte{0xFF}};

    assert_roundtrip(
        Opcode::Binary,
        payload,
        true,
        false);
  }

  static void test_masked_binary_frame_roundtrip()
  {
    const std::vector<std::byte> payload{
        std::byte{0x00},
        std::byte{0x01},
        std::byte{0x7F},
        std::byte{0x80},
        std::byte{0xFE},
        std::byte{0xFF}};

    assert_roundtrip(
        Opcode::Binary,
        payload,
        true,
        true);
  }

  static void test_payload_length_1_roundtrip()
  {
    assert_roundtrip(
        Opcode::Binary,
        make_pattern_payload(1u),
        true,
        false);

    assert_roundtrip(
        Opcode::Binary,
        make_pattern_payload(1u),
        true,
        true);
  }

  static void test_payload_length_2_roundtrip()
  {
    assert_roundtrip(
        Opcode::Binary,
        make_pattern_payload(2u),
        true,
        false);

    assert_roundtrip(
        Opcode::Binary,
        make_pattern_payload(2u),
        true,
        true);
  }

  static void test_payload_length_125_roundtrip()
  {
    const auto payload =
        make_pattern_payload(125u);

    assert_roundtrip(
        Opcode::Binary,
        payload,
        true,
        false);

    assert_roundtrip(
        Opcode::Binary,
        payload,
        true,
        true);
  }

  static void test_payload_length_126_roundtrip()
  {
    const auto payload =
        make_pattern_payload(126u);

    assert_roundtrip(
        Opcode::Binary,
        payload,
        true,
        false);

    assert_roundtrip(
        Opcode::Binary,
        payload,
        true,
        true);
  }

  static void test_payload_length_127_roundtrip()
  {
    const auto payload =
        make_pattern_payload(127u);

    assert_roundtrip(
        Opcode::Binary,
        payload,
        true,
        false);

    assert_roundtrip(
        Opcode::Binary,
        payload,
        true,
        true);
  }

  static void test_payload_length_255_roundtrip()
  {
    const auto payload =
        make_pattern_payload(255u);

    assert_roundtrip(
        Opcode::Binary,
        payload,
        true,
        false);

    assert_roundtrip(
        Opcode::Binary,
        payload,
        true,
        true);
  }

  static void test_payload_length_256_roundtrip()
  {
    const auto payload =
        make_pattern_payload(256u);

    assert_roundtrip(
        Opcode::Binary,
        payload,
        true,
        false);

    assert_roundtrip(
        Opcode::Binary,
        payload,
        true,
        true);
  }

  static void test_payload_length_65535_roundtrip()
  {
    const auto payload =
        make_pattern_payload(65535u);

    assert_roundtrip(
        Opcode::Binary,
        payload,
        true,
        false);

    assert_roundtrip(
        Opcode::Binary,
        payload,
        true,
        true);
  }

  static void test_payload_length_65536_roundtrip()
  {
    const auto payload =
        make_pattern_payload(65536u);

    assert_roundtrip(
        Opcode::Binary,
        payload,
        true,
        false);

    assert_roundtrip(
        Opcode::Binary,
        payload,
        true,
        true);
  }

  static void test_large_payload_roundtrip()
  {
    const auto payload =
        make_pattern_payload(100000u);

    assert_roundtrip(
        Opcode::Binary,
        payload,
        true,
        false);

    assert_roundtrip(
        Opcode::Binary,
        payload,
        true,
        true);
  }

  static void test_ping_frame_roundtrip()
  {
    assert_roundtrip(
        Opcode::Ping,
        {},
        true,
        false);

    assert_roundtrip(
        Opcode::Ping,
        {},
        true,
        true);
  }

  static void test_ping_payload_roundtrip()
  {
    const auto payload =
        bytes_from_string("ping-data");

    assert_roundtrip(
        Opcode::Ping,
        payload,
        true,
        false);

    assert_roundtrip(
        Opcode::Ping,
        payload,
        true,
        true);
  }

  static void test_pong_frame_roundtrip()
  {
    assert_roundtrip(
        Opcode::Pong,
        {},
        true,
        false);

    assert_roundtrip(
        Opcode::Pong,
        {},
        true,
        true);
  }

  static void test_pong_payload_roundtrip()
  {
    const auto payload =
        bytes_from_string("pong-data");

    assert_roundtrip(
        Opcode::Pong,
        payload,
        true,
        false);

    assert_roundtrip(
        Opcode::Pong,
        payload,
        true,
        true);
  }

  static void test_close_frame_roundtrip()
  {
    assert_roundtrip(
        Opcode::Close,
        {},
        true,
        false);

    assert_roundtrip(
        Opcode::Close,
        {},
        true,
        true);
  }

  static void test_close_code_payload_roundtrip()
  {
    const std::vector<std::byte> payload{
        std::byte{0x03},
        std::byte{0xE8}};

    assert_roundtrip(
        Opcode::Close,
        payload,
        true,
        false);

    assert_roundtrip(
        Opcode::Close,
        payload,
        true,
        true);
  }

  static void test_close_code_and_reason_roundtrip()
  {
    std::vector<std::byte> payload{
        std::byte{0x03},
        std::byte{0xE8}};

    const auto reason =
        bytes_from_string("normal closure");

    payload.insert(
        payload.end(),
        reason.begin(),
        reason.end());

    assert_roundtrip(
        Opcode::Close,
        payload,
        true,
        false);

    assert_roundtrip(
        Opcode::Close,
        payload,
        true,
        true);
  }

  static void test_all_byte_values_roundtrip()
  {
    const auto payload =
        make_pattern_payload(256u);

    assert_roundtrip(
        Opcode::Binary,
        payload,
        true,
        false);

    assert_roundtrip(
        Opcode::Binary,
        payload,
        true,
        true);
  }

  static void test_repeated_masked_roundtrips()
  {
    const auto payload =
        make_pattern_payload(512u);

    for (std::size_t i = 0; i < 100u; ++i)
    {
      assert_roundtrip(
          Opcode::Binary,
          payload,
          true,
          true);
    }
  }

  static void test_all_supported_opcodes_roundtrip()
  {
    const std::vector<Opcode> opcodes{
        Opcode::Continuation,
        Opcode::Text,
        Opcode::Binary,
        Opcode::Close,
        Opcode::Ping,
        Opcode::Pong};

    for (const Opcode opcode : opcodes)
    {
      const std::vector<std::byte> payload =
          opcode == Opcode::Close
              ? std::vector<std::byte>{
                    std::byte{0x03},
                    std::byte{0xE8}}
              : bytes_from_string("payload");

      assert_roundtrip(
          opcode,
          payload,
          true,
          false);

      assert_roundtrip(
          opcode,
          payload,
          true,
          true);
    }
  }

  static void test_roundtrip_does_not_modify_payload()
  {
    const std::vector<std::byte> payload =
        make_pattern_payload(1024u);

    const std::vector<std::byte> original =
        payload;

    assert_roundtrip(
        Opcode::Binary,
        payload,
        true,
        false);

    assert(payload == original);

    assert_roundtrip(
        Opcode::Binary,
        payload,
        true,
        true);

    assert(payload == original);
  }

} // namespace

int main()
{
  test_empty_text_frame_roundtrip();
  test_empty_masked_text_frame_roundtrip();

  test_short_text_frame_roundtrip();
  test_short_masked_text_frame_roundtrip();

  test_non_final_text_frame_roundtrip();
  test_non_final_masked_text_frame_roundtrip();

  test_continuation_frame_roundtrip();
  test_masked_continuation_frame_roundtrip();

  test_binary_frame_roundtrip();
  test_masked_binary_frame_roundtrip();

  test_payload_length_1_roundtrip();
  test_payload_length_2_roundtrip();

  test_payload_length_125_roundtrip();
  test_payload_length_126_roundtrip();
  test_payload_length_127_roundtrip();

  test_payload_length_255_roundtrip();
  test_payload_length_256_roundtrip();

  test_payload_length_65535_roundtrip();
  test_payload_length_65536_roundtrip();
  test_large_payload_roundtrip();

  test_ping_frame_roundtrip();
  test_ping_payload_roundtrip();

  test_pong_frame_roundtrip();
  test_pong_payload_roundtrip();

  test_close_frame_roundtrip();
  test_close_code_payload_roundtrip();
  test_close_code_and_reason_roundtrip();

  test_all_byte_values_roundtrip();
  test_repeated_masked_roundtrips();
  test_all_supported_opcodes_roundtrip();

  test_roundtrip_does_not_modify_payload();

  return 0;
}
