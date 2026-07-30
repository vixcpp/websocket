/**
 *
 * @file protocol_frame_header_test.cpp
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
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <vix/websocket/protocol.hpp>

namespace
{
  using FrameHeader = vix::websocket::detail::FrameHeader;
  using Opcode = vix::websocket::detail::Opcode;

  using vix::websocket::detail::build_frame;
  using vix::websocket::detail::build_text_frame;
  using vix::websocket::detail::parse_frame_header;

  template <typename Function>
  static void assert_runtime_error(
      Function &&function,
      const std::string &expected_message)
  {
    bool thrown = false;

    try
    {
      function();
    }
    catch (const std::runtime_error &error)
    {
      thrown = true;

      assert(
          std::string{error.what()} ==
          expected_message);
    }

    assert(thrown);
  }

  static std::vector<std::byte> make_payload(
      std::size_t size,
      std::byte value = std::byte{0x41})
  {
    return std::vector<std::byte>(size, value);
  }

  static void test_frame_header_type_traits()
  {
    static_assert(std::is_default_constructible_v<FrameHeader>);
    static_assert(std::is_copy_constructible_v<FrameHeader>);
    static_assert(std::is_copy_assignable_v<FrameHeader>);
    static_assert(std::is_move_constructible_v<FrameHeader>);
    static_assert(std::is_move_assignable_v<FrameHeader>);
    static_assert(std::is_destructible_v<FrameHeader>);
  }

  static void test_default_frame_header_values()
  {
    FrameHeader header;

    assert(header.fin == true);
    assert(header.opcode == Opcode::Text);
    assert(header.masked == false);

    for (const std::byte value : header.mask_key)
    {
      assert(value == std::byte{0x00});
    }

    assert(header.payload_length == 0u);
    assert(header.header_size == 0u);
  }

  static void test_empty_text_frame_header()
  {
    const auto frame =
        build_text_frame("", false);

    const FrameHeader header =
        parse_frame_header(
            frame.data(),
            frame.size());

    assert(header.fin == true);
    assert(header.opcode == Opcode::Text);
    assert(header.masked == false);
    assert(header.payload_length == 0u);
    assert(header.header_size == 2u);
  }

  static void test_short_text_frame_header()
  {
    const auto frame =
        build_text_frame("hello", false);

    const FrameHeader header =
        parse_frame_header(
            frame.data(),
            frame.size());

    assert(header.fin == true);
    assert(header.opcode == Opcode::Text);
    assert(header.masked == false);
    assert(header.payload_length == 5u);
    assert(header.header_size == 2u);
  }

  static void test_non_final_text_frame_header()
  {
    const auto frame =
        build_frame(
            Opcode::Text,
            make_payload(4u),
            false,
            false);

    const FrameHeader header =
        parse_frame_header(
            frame.data(),
            frame.size());

    assert(header.fin == false);
    assert(header.opcode == Opcode::Text);
    assert(header.masked == false);
    assert(header.payload_length == 4u);
    assert(header.header_size == 2u);
  }

  static void test_continuation_frame_header()
  {
    const auto frame =
        build_frame(
            Opcode::Continuation,
            make_payload(3u),
            true,
            false);

    const FrameHeader header =
        parse_frame_header(
            frame.data(),
            frame.size());

    assert(header.fin == true);
    assert(header.opcode == Opcode::Continuation);
    assert(header.payload_length == 3u);
  }

  static void test_binary_frame_header()
  {
    const auto frame =
        build_frame(
            Opcode::Binary,
            make_payload(8u),
            true,
            false);

    const FrameHeader header =
        parse_frame_header(
            frame.data(),
            frame.size());

    assert(header.fin == true);
    assert(header.opcode == Opcode::Binary);
    assert(header.payload_length == 8u);
  }

  static void test_close_frame_header()
  {
    const auto frame =
        build_frame(
            Opcode::Close,
            {},
            true,
            false);

    const FrameHeader header =
        parse_frame_header(
            frame.data(),
            frame.size());

    assert(header.fin == true);
    assert(header.opcode == Opcode::Close);
    assert(header.payload_length == 0u);
  }

  static void test_ping_frame_header()
  {
    const auto frame =
        build_frame(
            Opcode::Ping,
            {},
            true,
            false);

    const FrameHeader header =
        parse_frame_header(
            frame.data(),
            frame.size());

    assert(header.fin == true);
    assert(header.opcode == Opcode::Ping);
    assert(header.payload_length == 0u);
  }

  static void test_pong_frame_header()
  {
    const auto frame =
        build_frame(
            Opcode::Pong,
            make_payload(4u),
            true,
            false);

    const FrameHeader header =
        parse_frame_header(
            frame.data(),
            frame.size());

    assert(header.fin == true);
    assert(header.opcode == Opcode::Pong);
    assert(header.payload_length == 4u);
  }

  static void test_payload_length_125_header()
  {
    const auto frame =
        build_frame(
            Opcode::Binary,
            make_payload(125u),
            true,
            false);

    const FrameHeader header =
        parse_frame_header(
            frame.data(),
            frame.size());

    assert(header.payload_length == 125u);
    assert(header.header_size == 2u);
  }

  static void test_payload_length_126_header()
  {
    const auto frame =
        build_frame(
            Opcode::Binary,
            make_payload(126u),
            true,
            false);

    const FrameHeader header =
        parse_frame_header(
            frame.data(),
            frame.size());

    assert(header.payload_length == 126u);
    assert(header.header_size == 4u);
  }

  static void test_payload_length_65535_header()
  {
    const auto frame =
        build_frame(
            Opcode::Binary,
            make_payload(65535u),
            true,
            false);

    const FrameHeader header =
        parse_frame_header(
            frame.data(),
            frame.size());

    assert(header.payload_length == 65535u);
    assert(header.header_size == 4u);
  }

  static void test_payload_length_65536_header()
  {
    const auto frame =
        build_frame(
            Opcode::Binary,
            make_payload(65536u),
            true,
            false);

    const FrameHeader header =
        parse_frame_header(
            frame.data(),
            frame.size());

    assert(header.payload_length == 65536u);
    assert(header.header_size == 10u);
  }

  static void test_masked_short_frame_header()
  {
    const auto frame =
        build_text_frame("masked", true);

    const FrameHeader header =
        parse_frame_header(
            frame.data(),
            frame.size());

    assert(header.fin == true);
    assert(header.opcode == Opcode::Text);
    assert(header.masked == true);
    assert(header.payload_length == 6u);
    assert(header.header_size == 6u);

    for (std::size_t i = 0; i < 4u; ++i)
    {
      assert(
          header.mask_key[i] ==
          frame[2u + i]);
    }
  }

  static void test_masked_16_bit_frame_header()
  {
    const auto frame =
        build_frame(
            Opcode::Binary,
            make_payload(126u),
            true,
            true);

    const FrameHeader header =
        parse_frame_header(
            frame.data(),
            frame.size());

    assert(header.masked == true);
    assert(header.payload_length == 126u);
    assert(header.header_size == 8u);

    for (std::size_t i = 0; i < 4u; ++i)
    {
      assert(
          header.mask_key[i] ==
          frame[4u + i]);
    }
  }

  static void test_masked_64_bit_frame_header()
  {
    const auto frame =
        build_frame(
            Opcode::Binary,
            make_payload(65536u),
            true,
            true);

    const FrameHeader header =
        parse_frame_header(
            frame.data(),
            frame.size());

    assert(header.masked == true);
    assert(header.payload_length == 65536u);
    assert(header.header_size == 14u);

    for (std::size_t i = 0; i < 4u; ++i)
    {
      assert(
          header.mask_key[i] ==
          frame[10u + i]);
    }
  }

  static void test_header_can_be_parsed_without_payload()
  {
    const std::array<std::byte, 2> bytes{
        std::byte{0x81},
        std::byte{0x05}};

    const FrameHeader header =
        parse_frame_header(
            bytes.data(),
            bytes.size());

    assert(header.fin == true);
    assert(header.opcode == Opcode::Text);
    assert(header.masked == false);
    assert(header.payload_length == 5u);
    assert(header.header_size == 2u);
  }

  static void test_unknown_opcode_is_preserved()
  {
    const std::array<std::byte, 2> bytes{
        std::byte{0x83},
        std::byte{0x00}};

    const FrameHeader header =
        parse_frame_header(
            bytes.data(),
            bytes.size());

    assert(header.fin == true);

    assert(
        static_cast<std::uint8_t>(header.opcode) ==
        0x03u);

    assert(header.payload_length == 0u);
  }

  static void test_reserved_bits_do_not_change_opcode()
  {
    const std::array<std::byte, 2> bytes{
        std::byte{0xF1},
        std::byte{0x00}};

    const FrameHeader header =
        parse_frame_header(
            bytes.data(),
            bytes.size());

    assert(header.fin == true);
    assert(header.opcode == Opcode::Text);
  }

  static void test_empty_input_throws()
  {
    const std::vector<std::byte> bytes;

    assert_runtime_error(
        [&bytes]()
        {
          parse_frame_header(
              bytes.data(),
              bytes.size());
        },
        "websocket frame too short");
  }

  static void test_one_byte_input_throws()
  {
    const std::array<std::byte, 1> bytes{
        std::byte{0x81}};

    assert_runtime_error(
        [&bytes]()
        {
          parse_frame_header(
              bytes.data(),
              bytes.size());
        },
        "websocket frame too short");
  }

  static void test_incomplete_16_bit_length_throws()
  {
    const std::array<std::byte, 3> bytes{
        std::byte{0x82},
        std::byte{0x7E},
        std::byte{0x00}};

    assert_runtime_error(
        [&bytes]()
        {
          parse_frame_header(
              bytes.data(),
              bytes.size());
        },
        "incomplete websocket extended length (16-bit)");
  }

  static void test_incomplete_64_bit_length_throws()
  {
    const std::array<std::byte, 9> bytes{
        std::byte{0x82},
        std::byte{0x7F},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00}};

    assert_runtime_error(
        [&bytes]()
        {
          parse_frame_header(
              bytes.data(),
              bytes.size());
        },
        "incomplete websocket extended length (64-bit)");
  }

  static void test_incomplete_short_mask_key_throws()
  {
    const std::array<std::byte, 5> bytes{
        std::byte{0x81},
        std::byte{0x80},
        std::byte{0x01},
        std::byte{0x02},
        std::byte{0x03}};

    assert_runtime_error(
        [&bytes]()
        {
          parse_frame_header(
              bytes.data(),
              bytes.size());
        },
        "incomplete websocket mask key");
  }

  static void test_incomplete_extended_mask_key_throws()
  {
    const std::array<std::byte, 7> bytes{
        std::byte{0x82},
        std::byte{0xFE},
        std::byte{0x00},
        std::byte{0x7E},
        std::byte{0x01},
        std::byte{0x02},
        std::byte{0x03}};

    assert_runtime_error(
        [&bytes]()
        {
          parse_frame_header(
              bytes.data(),
              bytes.size());
        },
        "incomplete websocket mask key");
  }

} // namespace

int main()
{
  test_frame_header_type_traits();
  test_default_frame_header_values();

  test_empty_text_frame_header();
  test_short_text_frame_header();
  test_non_final_text_frame_header();

  test_continuation_frame_header();
  test_binary_frame_header();
  test_close_frame_header();
  test_ping_frame_header();
  test_pong_frame_header();

  test_payload_length_125_header();
  test_payload_length_126_header();
  test_payload_length_65535_header();
  test_payload_length_65536_header();

  test_masked_short_frame_header();
  test_masked_16_bit_frame_header();
  test_masked_64_bit_frame_header();

  test_header_can_be_parsed_without_payload();
  test_unknown_opcode_is_preserved();
  test_reserved_bits_do_not_change_opcode();

  test_empty_input_throws();
  test_one_byte_input_throws();

  test_incomplete_16_bit_length_throws();
  test_incomplete_64_bit_length_throws();

  test_incomplete_short_mask_key_throws();
  test_incomplete_extended_mask_key_throws();

  return 0;
}
