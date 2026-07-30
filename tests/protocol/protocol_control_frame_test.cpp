/**
 *
 * @file protocol_control_frame_test.cpp
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
  using vix::websocket::detail::build_ping_frame;
  using vix::websocket::detail::build_pong_frame;
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

  static void test_control_opcode_values()
  {
    assert(
        static_cast<std::uint8_t>(Opcode::Close) ==
        0x08u);

    assert(
        static_cast<std::uint8_t>(Opcode::Ping) ==
        0x09u);

    assert(
        static_cast<std::uint8_t>(Opcode::Pong) ==
        0x0Au);
  }

  static void test_control_opcodes_use_high_opcode_bit()
  {
    assert(
        static_cast<std::uint8_t>(Opcode::Close) >=
        0x08u);

    assert(
        static_cast<std::uint8_t>(Opcode::Ping) >=
        0x08u);

    assert(
        static_cast<std::uint8_t>(Opcode::Pong) >=
        0x08u);
  }

  static void test_empty_ping_frame()
  {
    const auto encoded =
        build_ping_frame(false);

    assert(encoded.size() == 2u);
    assert(to_u8(encoded[0]) == 0x89u);
    assert(to_u8(encoded[1]) == 0x00u);

    const Frame frame =
        decode_frame(encoded);

    assert(frame.fin == true);
    assert(frame.opcode == Opcode::Ping);
    assert(frame.masked == false);
    assert(frame.payload.empty());
  }

  static void test_masked_empty_ping_frame()
  {
    const auto encoded =
        build_ping_frame(true);

    assert(encoded.size() == 6u);
    assert(to_u8(encoded[0]) == 0x89u);
    assert(to_u8(encoded[1]) == 0x80u);

    const Frame frame =
        decode_frame(encoded);

    assert(frame.fin == true);
    assert(frame.opcode == Opcode::Ping);
    assert(frame.masked == true);
    assert(frame.payload.empty());
  }

  static void test_ping_frame_with_payload()
  {
    const auto payload =
        bytes_from_string("heartbeat");

    const auto encoded =
        build_frame(
            Opcode::Ping,
            payload,
            true,
            false);

    const Frame frame =
        decode_frame(encoded);

    assert(frame.fin == true);
    assert(frame.opcode == Opcode::Ping);
    assert(frame.masked == false);

    assert(
        string_from_bytes(frame.payload) ==
        "heartbeat");
  }

  static void test_masked_ping_frame_with_payload()
  {
    const auto payload =
        bytes_from_string("heartbeat");

    const auto encoded =
        build_frame(
            Opcode::Ping,
            payload,
            true,
            true);

    const Frame frame =
        decode_frame(encoded);

    assert(frame.fin == true);
    assert(frame.opcode == Opcode::Ping);
    assert(frame.masked == true);

    assert(
        string_from_bytes(frame.payload) ==
        "heartbeat");
  }

  static void test_empty_pong_frame()
  {
    const auto encoded =
        build_pong_frame({}, false);

    assert(encoded.size() == 2u);
    assert(to_u8(encoded[0]) == 0x8Au);
    assert(to_u8(encoded[1]) == 0x00u);

    const Frame frame =
        decode_frame(encoded);

    assert(frame.fin == true);
    assert(frame.opcode == Opcode::Pong);
    assert(frame.masked == false);
    assert(frame.payload.empty());
  }

  static void test_masked_empty_pong_frame()
  {
    const auto encoded =
        build_pong_frame({}, true);

    assert(encoded.size() == 6u);
    assert(to_u8(encoded[0]) == 0x8Au);
    assert(to_u8(encoded[1]) == 0x80u);

    const Frame frame =
        decode_frame(encoded);

    assert(frame.fin == true);
    assert(frame.opcode == Opcode::Pong);
    assert(frame.masked == true);
    assert(frame.payload.empty());
  }

  static void test_pong_frame_preserves_ping_payload()
  {
    const auto payload =
        bytes_from_string("ping payload");

    const auto encoded =
        build_pong_frame(
            payload,
            false);

    const Frame frame =
        decode_frame(encoded);

    assert(frame.fin == true);
    assert(frame.opcode == Opcode::Pong);
    assert(frame.payload == payload);
  }

  static void test_masked_pong_frame_preserves_payload()
  {
    const auto payload =
        bytes_from_string("ping payload");

    const auto encoded =
        build_pong_frame(
            payload,
            true);

    const Frame frame =
        decode_frame(encoded);

    assert(frame.fin == true);
    assert(frame.opcode == Opcode::Pong);
    assert(frame.masked == true);
    assert(frame.payload == payload);
  }

  static void test_empty_close_frame()
  {
    const auto encoded =
        build_close_frame(false);

    assert(encoded.size() == 2u);
    assert(to_u8(encoded[0]) == 0x88u);
    assert(to_u8(encoded[1]) == 0x00u);

    const Frame frame =
        decode_frame(encoded);

    assert(frame.fin == true);
    assert(frame.opcode == Opcode::Close);
    assert(frame.masked == false);
    assert(frame.payload.empty());
  }

  static void test_masked_empty_close_frame()
  {
    const auto encoded =
        build_close_frame(true);

    assert(encoded.size() == 6u);
    assert(to_u8(encoded[0]) == 0x88u);
    assert(to_u8(encoded[1]) == 0x80u);

    const Frame frame =
        decode_frame(encoded);

    assert(frame.fin == true);
    assert(frame.opcode == Opcode::Close);
    assert(frame.masked == true);
    assert(frame.payload.empty());
  }

  static void test_close_frame_with_normal_closure_code()
  {
    const std::vector<std::byte> payload{
        std::byte{0x03},
        std::byte{0xE8}};

    const auto encoded =
        build_frame(
            Opcode::Close,
            payload,
            true,
            false);

    const Frame frame =
        decode_frame(encoded);

    assert(frame.opcode == Opcode::Close);
    assert(frame.payload == payload);

    const std::uint16_t code =
        static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(
                 to_u8(frame.payload[0]))
             << 8u) |
            static_cast<std::uint16_t>(
                to_u8(frame.payload[1])));

    assert(code == 1000u);
  }

  static void test_close_frame_with_going_away_code()
  {
    const std::vector<std::byte> payload{
        std::byte{0x03},
        std::byte{0xE9}};

    const Frame frame =
        decode_frame(
            build_frame(
                Opcode::Close,
                payload,
                true,
                false));

    const std::uint16_t code =
        static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(
                 to_u8(frame.payload[0]))
             << 8u) |
            static_cast<std::uint16_t>(
                to_u8(frame.payload[1])));

    assert(code == 1001u);
  }

  static void test_close_frame_with_code_and_reason()
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

    const Frame frame =
        decode_frame(
            build_frame(
                Opcode::Close,
                payload,
                true,
                false));

    assert(frame.opcode == Opcode::Close);
    assert(frame.payload.size() == payload.size());

    const std::vector<std::byte> decoded_reason(
        frame.payload.begin() + 2,
        frame.payload.end());

    assert(
        string_from_bytes(decoded_reason) ==
        "normal closure");
  }

  static void test_masked_close_frame_with_code_and_reason()
  {
    std::vector<std::byte> payload{
        std::byte{0x03},
        std::byte{0xE8}};

    const auto reason =
        bytes_from_string("done");

    payload.insert(
        payload.end(),
        reason.begin(),
        reason.end());

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
    assert(frame.payload == payload);
  }

  static void test_control_frames_are_final_by_default()
  {
    const auto ping =
        parse_frame_header(
            build_ping_frame(false).data(),
            build_ping_frame(false).size());

    const auto pong_bytes =
        build_pong_frame({}, false);

    const auto pong =
        parse_frame_header(
            pong_bytes.data(),
            pong_bytes.size());

    const auto close_bytes =
        build_close_frame(false);

    const auto close =
        parse_frame_header(
            close_bytes.data(),
            close_bytes.size());

    assert(ping.fin == true);
    assert(pong.fin == true);
    assert(close.fin == true);
  }

  static void test_control_frame_payload_length_125()
  {
    const std::vector<std::byte> payload(
        125u,
        std::byte{0x41});

    const auto encoded =
        build_frame(
            Opcode::Ping,
            payload,
            true,
            false);

    const auto header =
        parse_frame_header(
            encoded.data(),
            encoded.size());

    assert(header.opcode == Opcode::Ping);
    assert(header.payload_length == 125u);
    assert(header.header_size == 2u);

    const Frame frame =
        decode_frame(encoded);

    assert(frame.payload == payload);
  }

  static void test_masked_control_frame_payload_length_125()
  {
    const std::vector<std::byte> payload(
        125u,
        std::byte{0x42});

    const auto encoded =
        build_frame(
            Opcode::Pong,
            payload,
            true,
            true);

    const auto header =
        parse_frame_header(
            encoded.data(),
            encoded.size());

    assert(header.opcode == Opcode::Pong);
    assert(header.masked == true);
    assert(header.payload_length == 125u);
    assert(header.header_size == 6u);

    const Frame frame =
        decode_frame(encoded);

    assert(frame.payload == payload);
  }

  static void test_control_frame_payload_is_not_modified()
  {
    const auto payload =
        bytes_from_string("control payload");

    const auto original = payload;

    const auto ping =
        build_frame(
            Opcode::Ping,
            payload,
            true,
            true);

    assert(!ping.empty());
    assert(payload == original);

    const auto pong =
        build_pong_frame(
            payload,
            true);

    assert(!pong.empty());
    assert(payload == original);

    const auto close =
        build_frame(
            Opcode::Close,
            payload,
            true,
            true);

    assert(!close.empty());
    assert(payload == original);
  }

  static void test_control_frames_have_distinct_opcodes()
  {
    const Frame close =
        decode_frame(
            build_close_frame(false));

    const Frame ping =
        decode_frame(
            build_ping_frame(false));

    const Frame pong =
        decode_frame(
            build_pong_frame({}, false));

    assert(close.opcode != ping.opcode);
    assert(close.opcode != pong.opcode);
    assert(ping.opcode != pong.opcode);
  }

  static void test_ping_and_pong_can_share_payload()
  {
    const auto payload =
        bytes_from_string("same-payload");

    const Frame ping =
        decode_frame(
            build_frame(
                Opcode::Ping,
                payload,
                true,
                false));

    const Frame pong =
        decode_frame(
            build_pong_frame(
                ping.payload,
                false));

    assert(ping.opcode == Opcode::Ping);
    assert(pong.opcode == Opcode::Pong);

    assert(ping.payload == payload);
    assert(pong.payload == payload);
    assert(ping.payload == pong.payload);
  }

} // namespace

int main()
{
  test_control_opcode_values();
  test_control_opcodes_use_high_opcode_bit();

  test_empty_ping_frame();
  test_masked_empty_ping_frame();
  test_ping_frame_with_payload();
  test_masked_ping_frame_with_payload();

  test_empty_pong_frame();
  test_masked_empty_pong_frame();
  test_pong_frame_preserves_ping_payload();
  test_masked_pong_frame_preserves_payload();

  test_empty_close_frame();
  test_masked_empty_close_frame();

  test_close_frame_with_normal_closure_code();
  test_close_frame_with_going_away_code();
  test_close_frame_with_code_and_reason();
  test_masked_close_frame_with_code_and_reason();

  test_control_frames_are_final_by_default();

  test_control_frame_payload_length_125();
  test_masked_control_frame_payload_length_125();

  test_control_frame_payload_is_not_modified();
  test_control_frames_have_distinct_opcodes();
  test_ping_and_pong_can_share_payload();

  return 0;
}
