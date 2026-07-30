/**
 *
 * @file protocol_close_frame_test.cpp
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

  using vix::websocket::detail::build_close_frame;
  using vix::websocket::detail::build_frame;
  using vix::websocket::detail::decode_frame;
  using vix::websocket::detail::parse_frame_header;
  using vix::websocket::detail::to_u8;

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

  static std::string string_from_bytes(
      const std::vector<std::byte> &value)
  {
    if (value.empty())
    {
      return {};
    }

    return std::string{
        reinterpret_cast<const char *>(value.data()),
        value.size()};
  }

  static std::uint16_t read_close_code(
      const Frame &frame)
  {
    assert(frame.payload.size() >= 2u);

    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(
             to_u8(frame.payload[0]))
         << 8u) |
        static_cast<std::uint16_t>(
            to_u8(frame.payload[1])));
  }

  static std::vector<std::byte> make_close_payload(
      std::uint16_t code,
      std::string_view reason = {})
  {
    std::vector<std::byte> payload;

    payload.push_back(
        static_cast<std::byte>(
            static_cast<std::uint8_t>(
                (code >> 8u) & 0xFFu)));

    payload.push_back(
        static_cast<std::byte>(
            static_cast<std::uint8_t>(
                code & 0xFFu)));

    const auto reason_bytes =
        bytes_from_string(reason);

    payload.insert(
        payload.end(),
        reason_bytes.begin(),
        reason_bytes.end());

    return payload;
  }

  static void test_empty_unmasked_close_frame_bytes()
  {
    const auto frame =
        build_close_frame(false);

    assert(frame.size() == 2u);
    assert(to_u8(frame[0]) == 0x88u);
    assert(to_u8(frame[1]) == 0x00u);
  }

  static void test_empty_masked_close_frame_bytes()
  {
    const auto frame =
        build_close_frame(true);

    assert(frame.size() == 6u);
    assert(to_u8(frame[0]) == 0x88u);
    assert(to_u8(frame[1]) == 0x80u);
  }

  static void test_empty_unmasked_close_frame_header()
  {
    const auto frame =
        build_close_frame(false);

    const auto header =
        parse_frame_header(
            frame.data(),
            frame.size());

    assert(header.fin == true);
    assert(header.opcode == Opcode::Close);
    assert(header.masked == false);
    assert(header.payload_length == 0u);
    assert(header.header_size == 2u);
  }

  static void test_empty_masked_close_frame_header()
  {
    const auto frame =
        build_close_frame(true);

    const auto header =
        parse_frame_header(
            frame.data(),
            frame.size());

    assert(header.fin == true);
    assert(header.opcode == Opcode::Close);
    assert(header.masked == true);
    assert(header.payload_length == 0u);
    assert(header.header_size == 6u);
  }

  static void test_empty_unmasked_close_frame_decode()
  {
    const Frame frame =
        decode_frame(
            build_close_frame(false));

    assert(frame.fin == true);
    assert(frame.opcode == Opcode::Close);
    assert(frame.masked == false);
    assert(frame.payload.empty());
  }

  static void test_empty_masked_close_frame_decode()
  {
    const Frame frame =
        decode_frame(
            build_close_frame(true));

    assert(frame.fin == true);
    assert(frame.opcode == Opcode::Close);
    assert(frame.masked == true);
    assert(frame.payload.empty());
  }

  static void test_normal_closure_code()
  {
    const auto payload =
        make_close_payload(1000u);

    const Frame frame =
        decode_frame(
            build_frame(
                Opcode::Close,
                payload,
                true,
                false));

    assert(frame.fin == true);
    assert(frame.opcode == Opcode::Close);
    assert(frame.masked == false);
    assert(frame.payload.size() == 2u);
    assert(read_close_code(frame) == 1000u);
  }

  static void test_going_away_code()
  {
    const auto payload =
        make_close_payload(1001u);

    const Frame frame =
        decode_frame(
            build_frame(
                Opcode::Close,
                payload,
                true,
                false));

    assert(read_close_code(frame) == 1001u);
  }

  static void test_protocol_error_code()
  {
    const auto payload =
        make_close_payload(1002u);

    const Frame frame =
        decode_frame(
            build_frame(
                Opcode::Close,
                payload,
                true,
                false));

    assert(read_close_code(frame) == 1002u);
  }

  static void test_unsupported_data_code()
  {
    const auto payload =
        make_close_payload(1003u);

    const Frame frame =
        decode_frame(
            build_frame(
                Opcode::Close,
                payload,
                true,
                false));

    assert(read_close_code(frame) == 1003u);
  }

  static void test_invalid_payload_data_code()
  {
    const auto payload =
        make_close_payload(1007u);

    const Frame frame =
        decode_frame(
            build_frame(
                Opcode::Close,
                payload,
                true,
                false));

    assert(read_close_code(frame) == 1007u);
  }

  static void test_policy_violation_code()
  {
    const auto payload =
        make_close_payload(1008u);

    const Frame frame =
        decode_frame(
            build_frame(
                Opcode::Close,
                payload,
                true,
                false));

    assert(read_close_code(frame) == 1008u);
  }

  static void test_message_too_big_code()
  {
    const auto payload =
        make_close_payload(1009u);

    const Frame frame =
        decode_frame(
            build_frame(
                Opcode::Close,
                payload,
                true,
                false));

    assert(read_close_code(frame) == 1009u);
  }

  static void test_internal_error_code()
  {
    const auto payload =
        make_close_payload(1011u);

    const Frame frame =
        decode_frame(
            build_frame(
                Opcode::Close,
                payload,
                true,
                false));

    assert(read_close_code(frame) == 1011u);
  }

  static void test_private_application_code()
  {
    const auto payload =
        make_close_payload(4000u);

    const Frame frame =
        decode_frame(
            build_frame(
                Opcode::Close,
                payload,
                true,
                false));

    assert(read_close_code(frame) == 4000u);
  }

  static void test_close_code_is_big_endian()
  {
    const auto payload =
        make_close_payload(1000u);

    assert(payload.size() == 2u);
    assert(to_u8(payload[0]) == 0x03u);
    assert(to_u8(payload[1]) == 0xE8u);

    const auto encoded =
        build_frame(
            Opcode::Close,
            payload,
            true,
            false);

    assert(to_u8(encoded[2]) == 0x03u);
    assert(to_u8(encoded[3]) == 0xE8u);
  }

  static void test_close_frame_with_reason()
  {
    const auto payload =
        make_close_payload(
            1000u,
            "normal closure");

    const Frame frame =
        decode_frame(
            build_frame(
                Opcode::Close,
                payload,
                true,
                false));

    assert(read_close_code(frame) == 1000u);

    const std::vector<std::byte> reason(
        frame.payload.begin() + 2,
        frame.payload.end());

    assert(
        string_from_bytes(reason) ==
        "normal closure");
  }

  static void test_masked_close_frame_with_reason()
  {
    const auto payload =
        make_close_payload(
            1001u,
            "server shutdown");

    const Frame frame =
        decode_frame(
            build_frame(
                Opcode::Close,
                payload,
                true,
                true));

    assert(frame.fin == true);
    assert(frame.opcode == Opcode::Close);
    assert(frame.masked == true);
    assert(read_close_code(frame) == 1001u);

    const std::vector<std::byte> reason(
        frame.payload.begin() + 2,
        frame.payload.end());

    assert(
        string_from_bytes(reason) ==
        "server shutdown");
  }

  static void test_close_frame_with_empty_reason()
  {
    const auto payload =
        make_close_payload(
            1000u,
            "");

    const Frame frame =
        decode_frame(
            build_frame(
                Opcode::Close,
                payload,
                true,
                false));

    assert(frame.payload.size() == 2u);
    assert(read_close_code(frame) == 1000u);
  }

  static void test_close_frame_with_utf8_reason()
  {
    const auto payload =
        make_close_payload(
            1000u,
            "fermé correctement");

    const Frame frame =
        decode_frame(
            build_frame(
                Opcode::Close,
                payload,
                true,
                false));

    const std::vector<std::byte> reason(
        frame.payload.begin() + 2,
        frame.payload.end());

    assert(
        string_from_bytes(reason) ==
        "fermé correctement");
  }

  static void test_close_frame_payload_length_125()
  {
    std::vector<std::byte> payload =
        make_close_payload(1000u);

    payload.resize(
        125u,
        std::byte{0x41});

    const auto encoded =
        build_frame(
            Opcode::Close,
            payload,
            true,
            false);

    const auto header =
        parse_frame_header(
            encoded.data(),
            encoded.size());

    assert(header.opcode == Opcode::Close);
    assert(header.payload_length == 125u);
    assert(header.header_size == 2u);

    const Frame frame =
        decode_frame(encoded);

    assert(frame.payload == payload);
  }

  static void test_masked_close_payload_length_125()
  {
    std::vector<std::byte> payload =
        make_close_payload(1000u);

    payload.resize(
        125u,
        std::byte{0x42});

    const auto encoded =
        build_frame(
            Opcode::Close,
            payload,
            true,
            true);

    const auto header =
        parse_frame_header(
            encoded.data(),
            encoded.size());

    assert(header.opcode == Opcode::Close);
    assert(header.masked == true);
    assert(header.payload_length == 125u);
    assert(header.header_size == 6u);

    const Frame frame =
        decode_frame(encoded);

    assert(frame.payload == payload);
  }

  static void test_close_frame_is_final()
  {
    const auto frame =
        build_close_frame(false);

    const auto header =
        parse_frame_header(
            frame.data(),
            frame.size());

    assert(header.fin == true);
  }

  static void test_close_frame_opcode_is_distinct()
  {
    assert(Opcode::Close != Opcode::Text);
    assert(Opcode::Close != Opcode::Binary);
    assert(Opcode::Close != Opcode::Ping);
    assert(Opcode::Close != Opcode::Pong);
  }

  static void test_close_payload_is_not_modified()
  {
    const auto payload =
        make_close_payload(
            1000u,
            "immutable");

    const auto original = payload;

    const auto unmasked =
        build_frame(
            Opcode::Close,
            payload,
            true,
            false);

    assert(!unmasked.empty());
    assert(payload == original);

    const auto masked =
        build_frame(
            Opcode::Close,
            payload,
            true,
            true);

    assert(!masked.empty());
    assert(payload == original);
  }

  static void test_repeated_empty_close_frames()
  {
    for (std::size_t i = 0; i < 100u; ++i)
    {
      const Frame unmasked =
          decode_frame(
              build_close_frame(false));

      const Frame masked =
          decode_frame(
              build_close_frame(true));

      assert(unmasked.opcode == Opcode::Close);
      assert(unmasked.payload.empty());

      assert(masked.opcode == Opcode::Close);
      assert(masked.payload.empty());
    }
  }

} // namespace

int main()
{
  test_empty_unmasked_close_frame_bytes();
  test_empty_masked_close_frame_bytes();

  test_empty_unmasked_close_frame_header();
  test_empty_masked_close_frame_header();

  test_empty_unmasked_close_frame_decode();
  test_empty_masked_close_frame_decode();

  test_normal_closure_code();
  test_going_away_code();
  test_protocol_error_code();
  test_unsupported_data_code();
  test_invalid_payload_data_code();
  test_policy_violation_code();
  test_message_too_big_code();
  test_internal_error_code();
  test_private_application_code();

  test_close_code_is_big_endian();

  test_close_frame_with_reason();
  test_masked_close_frame_with_reason();
  test_close_frame_with_empty_reason();
  test_close_frame_with_utf8_reason();

  test_close_frame_payload_length_125();
  test_masked_close_payload_length_125();

  test_close_frame_is_final();
  test_close_frame_opcode_is_distinct();

  test_close_payload_is_not_modified();
  test_repeated_empty_close_frames();

  return 0;
}
