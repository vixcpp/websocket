/**
 *
 * @file protocol_frame_build_test.cpp
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
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <vix/websocket/protocol.hpp>

namespace
{
  using Opcode = vix::websocket::detail::Opcode;

  using vix::websocket::detail::build_close_frame;
  using vix::websocket::detail::build_frame;
  using vix::websocket::detail::build_ping_frame;
  using vix::websocket::detail::build_pong_frame;
  using vix::websocket::detail::build_text_frame;
  using vix::websocket::detail::decode_frame;
  using vix::websocket::detail::parse_frame_header;
  using vix::websocket::detail::to_u8;

  static std::vector<std::byte> bytes_from_string(
      std::string_view value)
  {
    std::vector<std::byte> output;
    output.reserve(value.size());

    for (const char ch : value)
    {
      output.push_back(
          static_cast<std::byte>(
              static_cast<unsigned char>(ch)));
    }

    return output;
  }

  static std::string string_from_bytes(
      const std::vector<std::byte> &value)
  {
    return std::string{
        reinterpret_cast<const char *>(value.data()),
        value.size()};
  }

  static void test_empty_text_frame_unmasked()
  {
    const std::vector<std::byte> frame =
        build_text_frame("", false);

    assert(frame.size() == 2u);

    assert(to_u8(frame[0]) == 0x81u);
    assert(to_u8(frame[1]) == 0x00u);
  }

  static void test_short_text_frame_unmasked()
  {
    const std::vector<std::byte> frame =
        build_text_frame("Hello", false);

    assert(frame.size() == 7u);

    assert(to_u8(frame[0]) == 0x81u);
    assert(to_u8(frame[1]) == 0x05u);

    assert(to_u8(frame[2]) == 'H');
    assert(to_u8(frame[3]) == 'e');
    assert(to_u8(frame[4]) == 'l');
    assert(to_u8(frame[5]) == 'l');
    assert(to_u8(frame[6]) == 'o');
  }

  static void test_text_frame_sets_fin_and_text_opcode()
  {
    const std::vector<std::byte> frame =
        build_text_frame("message", false);

    const auto header =
        parse_frame_header(
            frame.data(),
            frame.size());

    assert(header.fin == true);
    assert(header.opcode == Opcode::Text);
    assert(header.masked == false);
    assert(header.payload_length == 7u);
    assert(header.header_size == 2u);
  }

  static void test_generic_non_final_text_frame()
  {
    const std::vector<std::byte> payload =
        bytes_from_string("part");

    const std::vector<std::byte> frame =
        build_frame(
            Opcode::Text,
            payload,
            false,
            false);

    assert(to_u8(frame[0]) == 0x01u);
    assert(to_u8(frame[1]) == 0x04u);

    const auto header =
        parse_frame_header(
            frame.data(),
            frame.size());

    assert(header.fin == false);
    assert(header.opcode == Opcode::Text);
    assert(header.masked == false);
    assert(header.payload_length == 4u);
  }

  static void test_continuation_frame()
  {
    const std::vector<std::byte> payload =
        bytes_from_string("next");

    const std::vector<std::byte> frame =
        build_frame(
            Opcode::Continuation,
            payload,
            true,
            false);

    assert(to_u8(frame[0]) == 0x80u);
    assert(to_u8(frame[1]) == 0x04u);

    const auto decoded = decode_frame(frame);

    assert(decoded.fin == true);
    assert(decoded.opcode == Opcode::Continuation);
    assert(decoded.masked == false);
    assert(string_from_bytes(decoded.payload) == "next");
  }

  static void test_binary_frame_unmasked()
  {
    const std::vector<std::byte> payload{
        std::byte{0x00},
        std::byte{0x01},
        std::byte{0xFE},
        std::byte{0xFF}};

    const std::vector<std::byte> frame =
        build_frame(
            Opcode::Binary,
            payload,
            true,
            false);

    assert(frame.size() == 6u);

    assert(to_u8(frame[0]) == 0x82u);
    assert(to_u8(frame[1]) == 0x04u);

    const auto decoded = decode_frame(frame);

    assert(decoded.fin == true);
    assert(decoded.opcode == Opcode::Binary);
    assert(decoded.masked == false);
    assert(decoded.payload == payload);
  }

  static void test_payload_length_125_uses_short_encoding()
  {
    const std::vector<std::byte> payload(
        125u,
        std::byte{'a'});

    const std::vector<std::byte> frame =
        build_frame(
            Opcode::Binary,
            payload,
            true,
            false);

    assert(frame.size() == 127u);

    assert(to_u8(frame[0]) == 0x82u);
    assert(to_u8(frame[1]) == 125u);

    const auto header =
        parse_frame_header(
            frame.data(),
            frame.size());

    assert(header.payload_length == 125u);
    assert(header.header_size == 2u);
  }

  static void test_payload_length_126_uses_16_bit_encoding()
  {
    const std::vector<std::byte> payload(
        126u,
        std::byte{'b'});

    const std::vector<std::byte> frame =
        build_frame(
            Opcode::Binary,
            payload,
            true,
            false);

    assert(frame.size() == 130u);

    assert(to_u8(frame[0]) == 0x82u);
    assert(to_u8(frame[1]) == 126u);

    assert(to_u8(frame[2]) == 0x00u);
    assert(to_u8(frame[3]) == 0x7Eu);

    const auto header =
        parse_frame_header(
            frame.data(),
            frame.size());

    assert(header.payload_length == 126u);
    assert(header.header_size == 4u);
  }

  static void test_payload_length_65535_uses_16_bit_encoding()
  {
    const std::vector<std::byte> payload(
        65535u,
        std::byte{'c'});

    const std::vector<std::byte> frame =
        build_frame(
            Opcode::Binary,
            payload,
            true,
            false);

    assert(frame.size() == 65539u);

    assert(to_u8(frame[1]) == 126u);
    assert(to_u8(frame[2]) == 0xFFu);
    assert(to_u8(frame[3]) == 0xFFu);

    const auto header =
        parse_frame_header(
            frame.data(),
            frame.size());

    assert(header.payload_length == 65535u);
    assert(header.header_size == 4u);
  }

  static void test_payload_length_65536_uses_64_bit_encoding()
  {
    const std::vector<std::byte> payload(
        65536u,
        std::byte{'d'});

    const std::vector<std::byte> frame =
        build_frame(
            Opcode::Binary,
            payload,
            true,
            false);

    assert(frame.size() == 65546u);

    assert(to_u8(frame[1]) == 127u);

    assert(to_u8(frame[2]) == 0x00u);
    assert(to_u8(frame[3]) == 0x00u);
    assert(to_u8(frame[4]) == 0x00u);
    assert(to_u8(frame[5]) == 0x00u);
    assert(to_u8(frame[6]) == 0x00u);
    assert(to_u8(frame[7]) == 0x01u);
    assert(to_u8(frame[8]) == 0x00u);
    assert(to_u8(frame[9]) == 0x00u);

    const auto header =
        parse_frame_header(
            frame.data(),
            frame.size());

    assert(header.payload_length == 65536u);
    assert(header.header_size == 10u);
  }

  static void test_masked_empty_text_frame()
  {
    const std::vector<std::byte> frame =
        build_text_frame("", true);

    assert(frame.size() == 6u);

    assert(to_u8(frame[0]) == 0x81u);
    assert(to_u8(frame[1]) == 0x80u);

    const auto header =
        parse_frame_header(
            frame.data(),
            frame.size());

    assert(header.fin == true);
    assert(header.opcode == Opcode::Text);
    assert(header.masked == true);
    assert(header.payload_length == 0u);
    assert(header.header_size == 6u);
  }

  static void test_masked_short_text_frame_structure()
  {
    const std::vector<std::byte> frame =
        build_text_frame("Hello", true);

    assert(frame.size() == 11u);

    assert(to_u8(frame[0]) == 0x81u);
    assert((to_u8(frame[1]) & 0x80u) != 0u);
    assert((to_u8(frame[1]) & 0x7Fu) == 5u);

    const auto header =
        parse_frame_header(
            frame.data(),
            frame.size());

    assert(header.masked == true);
    assert(header.payload_length == 5u);
    assert(header.header_size == 6u);
  }

  static void test_masked_text_frame_decodes_original_payload()
  {
    const std::string original =
        "masked websocket message";

    const std::vector<std::byte> frame =
        build_text_frame(original, true);

    const auto decoded =
        decode_frame(frame);

    assert(decoded.fin == true);
    assert(decoded.opcode == Opcode::Text);
    assert(decoded.masked == true);

    assert(
        string_from_bytes(decoded.payload) ==
        original);
  }

  static void test_masked_binary_frame_decodes_original_payload()
  {
    const std::vector<std::byte> payload{
        std::byte{0x00},
        std::byte{0x10},
        std::byte{0x20},
        std::byte{0x30},
        std::byte{0xFF}};

    const std::vector<std::byte> frame =
        build_frame(
            Opcode::Binary,
            payload,
            true,
            true);

    const auto decoded =
        decode_frame(frame);

    assert(decoded.opcode == Opcode::Binary);
    assert(decoded.masked == true);
    assert(decoded.payload == payload);
  }

  static void test_masked_extended_length_frame()
  {
    const std::vector<std::byte> payload(
        126u,
        std::byte{'x'});

    const std::vector<std::byte> frame =
        build_frame(
            Opcode::Binary,
            payload,
            true,
            true);

    /*
     * 2-byte base header + 2-byte extended length +
     * 4-byte mask + 126-byte payload.
     */
    assert(frame.size() == 134u);

    assert(to_u8(frame[0]) == 0x82u);
    assert(to_u8(frame[1]) == 0xFEu);

    const auto header =
        parse_frame_header(
            frame.data(),
            frame.size());

    assert(header.masked == true);
    assert(header.payload_length == 126u);
    assert(header.header_size == 8u);

    const auto decoded = decode_frame(frame);

    assert(decoded.payload == payload);
  }

  static void test_ping_frame_unmasked()
  {
    const std::vector<std::byte> frame =
        build_ping_frame(false);

    assert(frame.size() == 2u);
    assert(to_u8(frame[0]) == 0x89u);
    assert(to_u8(frame[1]) == 0x00u);

    const auto decoded = decode_frame(frame);

    assert(decoded.fin == true);
    assert(decoded.opcode == Opcode::Ping);
    assert(decoded.masked == false);
    assert(decoded.payload.empty());
  }

  static void test_ping_frame_masked()
  {
    const std::vector<std::byte> frame =
        build_ping_frame(true);

    assert(frame.size() == 6u);
    assert(to_u8(frame[0]) == 0x89u);
    assert(to_u8(frame[1]) == 0x80u);

    const auto decoded = decode_frame(frame);

    assert(decoded.fin == true);
    assert(decoded.opcode == Opcode::Ping);
    assert(decoded.masked == true);
    assert(decoded.payload.empty());
  }

  static void test_pong_frame_with_payload()
  {
    const std::vector<std::byte> payload =
        bytes_from_string("pong-data");

    const std::vector<std::byte> frame =
        build_pong_frame(payload, false);

    const auto decoded = decode_frame(frame);

    assert(decoded.fin == true);
    assert(decoded.opcode == Opcode::Pong);
    assert(decoded.masked == false);

    assert(
        string_from_bytes(decoded.payload) ==
        "pong-data");
  }

  static void test_masked_pong_frame_with_payload()
  {
    const std::vector<std::byte> payload =
        bytes_from_string("pong-data");

    const std::vector<std::byte> frame =
        build_pong_frame(payload, true);

    const auto decoded = decode_frame(frame);

    assert(decoded.fin == true);
    assert(decoded.opcode == Opcode::Pong);
    assert(decoded.masked == true);

    assert(
        string_from_bytes(decoded.payload) ==
        "pong-data");
  }

  static void test_close_frame_unmasked()
  {
    const std::vector<std::byte> frame =
        build_close_frame(false);

    assert(frame.size() == 2u);
    assert(to_u8(frame[0]) == 0x88u);
    assert(to_u8(frame[1]) == 0x00u);

    const auto decoded = decode_frame(frame);

    assert(decoded.fin == true);
    assert(decoded.opcode == Opcode::Close);
    assert(decoded.masked == false);
    assert(decoded.payload.empty());
  }

  static void test_close_frame_masked()
  {
    const std::vector<std::byte> frame =
        build_close_frame(true);

    assert(frame.size() == 6u);
    assert(to_u8(frame[0]) == 0x88u);
    assert(to_u8(frame[1]) == 0x80u);

    const auto decoded = decode_frame(frame);

    assert(decoded.fin == true);
    assert(decoded.opcode == Opcode::Close);
    assert(decoded.masked == true);
    assert(decoded.payload.empty());
  }

  static void test_build_frame_does_not_modify_payload()
  {
    const std::vector<std::byte> payload{
        std::byte{0x01},
        std::byte{0x02},
        std::byte{0x03},
        std::byte{0x04}};

    const std::vector<std::byte> original = payload;

    const auto unmasked =
        build_frame(
            Opcode::Binary,
            payload,
            true,
            false);

    assert(!unmasked.empty());
    assert(payload == original);

    const auto masked =
        build_frame(
            Opcode::Binary,
            payload,
            true,
            true);

    assert(!masked.empty());
    assert(payload == original);
  }

  static void test_unmasked_frame_build_is_deterministic()
  {
    const std::vector<std::byte> payload =
        bytes_from_string("deterministic");

    const auto first =
        build_frame(
            Opcode::Text,
            payload,
            true,
            false);

    const auto second =
        build_frame(
            Opcode::Text,
            payload,
            true,
            false);

    assert(first == second);
  }

  static void test_every_builtin_frame_round_trips()
  {
    const auto text =
        decode_frame(
            build_text_frame("hello", false));

    const auto binary =
        decode_frame(
            build_frame(
                Opcode::Binary,
                bytes_from_string("binary"),
                true,
                false));

    const auto ping =
        decode_frame(
            build_ping_frame(false));

    const auto pong =
        decode_frame(
            build_pong_frame(
                bytes_from_string("pong"),
                false));

    const auto close =
        decode_frame(
            build_close_frame(false));

    assert(text.opcode == Opcode::Text);
    assert(string_from_bytes(text.payload) == "hello");

    assert(binary.opcode == Opcode::Binary);
    assert(string_from_bytes(binary.payload) == "binary");

    assert(ping.opcode == Opcode::Ping);
    assert(ping.payload.empty());

    assert(pong.opcode == Opcode::Pong);
    assert(string_from_bytes(pong.payload) == "pong");

    assert(close.opcode == Opcode::Close);
    assert(close.payload.empty());
  }

} // namespace

int main()
{
  test_empty_text_frame_unmasked();
  test_short_text_frame_unmasked();
  test_text_frame_sets_fin_and_text_opcode();

  test_generic_non_final_text_frame();
  test_continuation_frame();
  test_binary_frame_unmasked();

  test_payload_length_125_uses_short_encoding();
  test_payload_length_126_uses_16_bit_encoding();
  test_payload_length_65535_uses_16_bit_encoding();
  test_payload_length_65536_uses_64_bit_encoding();

  test_masked_empty_text_frame();
  test_masked_short_text_frame_structure();
  test_masked_text_frame_decodes_original_payload();
  test_masked_binary_frame_decodes_original_payload();
  test_masked_extended_length_frame();

  test_ping_frame_unmasked();
  test_ping_frame_masked();

  test_pong_frame_with_payload();
  test_masked_pong_frame_with_payload();

  test_close_frame_unmasked();
  test_close_frame_masked();

  test_build_frame_does_not_modify_payload();
  test_unmasked_frame_build_is_deterministic();
  test_every_builtin_frame_round_trips();

  return 0;
}
