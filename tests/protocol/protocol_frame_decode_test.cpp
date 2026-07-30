/**
 *
 * @file protocol_frame_decode_test.cpp
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
  using vix::websocket::detail::build_text_frame;
  using vix::websocket::detail::decode_frame;

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
    if (value.empty())
    {
      return {};
    }

    return std::string{
        reinterpret_cast<const char *>(value.data()),
        value.size()};
  }

  static void test_decode_empty_text_frame()
  {
    const Frame frame =
        decode_frame(
            build_text_frame("", false));

    assert(frame.fin == true);
    assert(frame.opcode == Opcode::Text);
    assert(frame.masked == false);
    assert(frame.payload.empty());
  }

  static void test_decode_short_text_frame()
  {
    const Frame frame =
        decode_frame(
            build_text_frame(
                "hello",
                false));

    assert(frame.fin == true);
    assert(frame.opcode == Opcode::Text);
    assert(frame.masked == false);

    assert(
        string_from_bytes(frame.payload) ==
        "hello");
  }

  static void test_decode_non_final_text_frame()
  {
    const auto encoded =
        build_frame(
            Opcode::Text,
            bytes_from_string("part"),
            false,
            false);

    const Frame frame =
        decode_frame(encoded);

    assert(frame.fin == false);
    assert(frame.opcode == Opcode::Text);
    assert(frame.masked == false);

    assert(
        string_from_bytes(frame.payload) ==
        "part");
  }

  static void test_decode_continuation_frame()
  {
    const auto encoded =
        build_frame(
            Opcode::Continuation,
            bytes_from_string("continuation"),
            true,
            false);

    const Frame frame =
        decode_frame(encoded);

    assert(frame.fin == true);
    assert(frame.opcode == Opcode::Continuation);

    assert(
        string_from_bytes(frame.payload) ==
        "continuation");
  }

  static void test_decode_binary_frame()
  {
    const std::vector<std::byte> payload{
        std::byte{0x00},
        std::byte{0x01},
        std::byte{0x7F},
        std::byte{0x80},
        std::byte{0xFE},
        std::byte{0xFF}};

    const auto encoded =
        build_frame(
            Opcode::Binary,
            payload,
            true,
            false);

    const Frame frame =
        decode_frame(encoded);

    assert(frame.fin == true);
    assert(frame.opcode == Opcode::Binary);
    assert(frame.masked == false);
    assert(frame.payload == payload);
  }

  static void test_decode_masked_text_frame()
  {
    const std::string payload =
        "masked websocket payload";

    const Frame frame =
        decode_frame(
            build_text_frame(
                payload,
                true));

    assert(frame.fin == true);
    assert(frame.opcode == Opcode::Text);
    assert(frame.masked == true);

    assert(
        string_from_bytes(frame.payload) ==
        payload);
  }

  static void test_decode_masked_binary_frame()
  {
    const std::vector<std::byte> payload{
        std::byte{0x00},
        std::byte{0x10},
        std::byte{0x20},
        std::byte{0x30},
        std::byte{0x40},
        std::byte{0xFF}};

    const auto encoded =
        build_frame(
            Opcode::Binary,
            payload,
            true,
            true);

    const Frame frame =
        decode_frame(encoded);

    assert(frame.opcode == Opcode::Binary);
    assert(frame.masked == true);
    assert(frame.payload == payload);
  }

  static void test_decode_preserves_mask_key()
  {
    const auto encoded =
        build_text_frame(
            "mask",
            true);

    const Frame frame =
        decode_frame(encoded);

    assert(frame.masked == true);

    assert(frame.mask_key[0] == encoded[2]);
    assert(frame.mask_key[1] == encoded[3]);
    assert(frame.mask_key[2] == encoded[4]);
    assert(frame.mask_key[3] == encoded[5]);
  }

  static void test_decode_unmasked_frame_has_zero_mask_key()
  {
    const Frame frame =
        decode_frame(
            build_text_frame(
                "plain",
                false));

    assert(frame.masked == false);

    for (const std::byte value : frame.mask_key)
    {
      assert(value == std::byte{0x00});
    }
  }

  static void test_decode_payload_length_125()
  {
    const std::vector<std::byte> payload(
        125u,
        std::byte{0x41});

    const Frame frame =
        decode_frame(
            build_frame(
                Opcode::Binary,
                payload,
                true,
                false));

    assert(frame.payload.size() == 125u);
    assert(frame.payload == payload);
  }

  static void test_decode_payload_length_126()
  {
    const std::vector<std::byte> payload(
        126u,
        std::byte{0x42});

    const Frame frame =
        decode_frame(
            build_frame(
                Opcode::Binary,
                payload,
                true,
                false));

    assert(frame.payload.size() == 126u);
    assert(frame.payload == payload);
  }

  static void test_decode_payload_length_65535()
  {
    const std::vector<std::byte> payload(
        65535u,
        std::byte{0x43});

    const Frame frame =
        decode_frame(
            build_frame(
                Opcode::Binary,
                payload,
                true,
                false));

    assert(frame.payload.size() == 65535u);
    assert(frame.payload == payload);
  }

  static void test_decode_payload_length_65536()
  {
    const std::vector<std::byte> payload(
        65536u,
        std::byte{0x44});

    const Frame frame =
        decode_frame(
            build_frame(
                Opcode::Binary,
                payload,
                true,
                false));

    assert(frame.payload.size() == 65536u);
    assert(frame.payload == payload);
  }

  static void test_decode_masked_extended_payload()
  {
    const std::vector<std::byte> payload(
        126u,
        std::byte{0x55});

    const Frame frame =
        decode_frame(
            build_frame(
                Opcode::Binary,
                payload,
                true,
                true));

    assert(frame.masked == true);
    assert(frame.payload.size() == 126u);
    assert(frame.payload == payload);
  }

  static void test_decode_ping_frame()
  {
    const Frame frame =
        decode_frame(
            build_ping_frame(false));

    assert(frame.fin == true);
    assert(frame.opcode == Opcode::Ping);
    assert(frame.masked == false);
    assert(frame.payload.empty());
  }

  static void test_decode_masked_ping_frame()
  {
    const Frame frame =
        decode_frame(
            build_ping_frame(true));

    assert(frame.fin == true);
    assert(frame.opcode == Opcode::Ping);
    assert(frame.masked == true);
    assert(frame.payload.empty());
  }

  static void test_decode_pong_frame()
  {
    const std::vector<std::byte> payload =
        bytes_from_string("pong");

    const Frame frame =
        decode_frame(
            build_pong_frame(
                payload,
                false));

    assert(frame.fin == true);
    assert(frame.opcode == Opcode::Pong);
    assert(frame.masked == false);

    assert(
        string_from_bytes(frame.payload) ==
        "pong");
  }

  static void test_decode_masked_pong_frame()
  {
    const std::vector<std::byte> payload =
        bytes_from_string("pong");

    const Frame frame =
        decode_frame(
            build_pong_frame(
                payload,
                true));

    assert(frame.opcode == Opcode::Pong);
    assert(frame.masked == true);

    assert(
        string_from_bytes(frame.payload) ==
        "pong");
  }

  static void test_decode_close_frame()
  {
    const Frame frame =
        decode_frame(
            build_close_frame(false));

    assert(frame.fin == true);
    assert(frame.opcode == Opcode::Close);
    assert(frame.masked == false);
    assert(frame.payload.empty());
  }

  static void test_decode_masked_close_frame()
  {
    const Frame frame =
        decode_frame(
            build_close_frame(true));

    assert(frame.fin == true);
    assert(frame.opcode == Opcode::Close);
    assert(frame.masked == true);
    assert(frame.payload.empty());
  }

  static void test_decode_manual_masked_rfc_frame()
  {
    /*
     * RFC 6455 example:
     *
     * Payload: "Hello"
     * Mask key: 37 fa 21 3d
     * Masked payload: 7f 9f 4d 51 58
     */
    const std::vector<std::byte> encoded{
        std::byte{0x81},
        std::byte{0x85},

        std::byte{0x37},
        std::byte{0xFA},
        std::byte{0x21},
        std::byte{0x3D},

        std::byte{0x7F},
        std::byte{0x9F},
        std::byte{0x4D},
        std::byte{0x51},
        std::byte{0x58}};

    const Frame frame =
        decode_frame(encoded);

    assert(frame.fin == true);
    assert(frame.opcode == Opcode::Text);
    assert(frame.masked == true);

    assert(frame.mask_key[0] == std::byte{0x37});
    assert(frame.mask_key[1] == std::byte{0xFA});
    assert(frame.mask_key[2] == std::byte{0x21});
    assert(frame.mask_key[3] == std::byte{0x3D});

    assert(
        string_from_bytes(frame.payload) ==
        "Hello");
  }

  static void test_decode_ignores_trailing_bytes()
  {
    std::vector<std::byte> encoded =
        build_text_frame(
            "hello",
            false);

    encoded.push_back(std::byte{0xAA});
    encoded.push_back(std::byte{0xBB});
    encoded.push_back(std::byte{0xCC});

    const Frame frame =
        decode_frame(encoded);

    assert(frame.opcode == Opcode::Text);

    assert(
        string_from_bytes(frame.payload) ==
        "hello");
  }

  static void test_decode_does_not_modify_input()
  {
    const std::vector<std::byte> encoded =
        build_text_frame(
            "immutable",
            true);

    const std::vector<std::byte> original =
        encoded;

    const Frame frame =
        decode_frame(encoded);

    assert(
        string_from_bytes(frame.payload) ==
        "immutable");

    assert(encoded == original);
  }

  static void test_repeated_decode_is_stable()
  {
    const std::vector<std::byte> encoded =
        build_text_frame(
            "repeatable",
            true);

    const Frame first =
        decode_frame(encoded);

    const Frame second =
        decode_frame(encoded);

    const Frame third =
        decode_frame(encoded);

    assert(first.fin == second.fin);
    assert(second.fin == third.fin);

    assert(first.opcode == second.opcode);
    assert(second.opcode == third.opcode);

    assert(first.masked == second.masked);
    assert(second.masked == third.masked);

    assert(first.mask_key == second.mask_key);
    assert(second.mask_key == third.mask_key);

    assert(first.payload == second.payload);
    assert(second.payload == third.payload);
  }

  static void test_unknown_opcode_is_preserved()
  {
    const std::vector<std::byte> encoded{
        std::byte{0x83},
        std::byte{0x01},
        std::byte{0x41}};

    const Frame frame =
        decode_frame(encoded);

    assert(frame.fin == true);

    assert(
        static_cast<std::uint8_t>(frame.opcode) ==
        0x03u);

    assert(frame.payload.size() == 1u);
    assert(frame.payload[0] == std::byte{0x41});
  }

  static void test_empty_input_throws()
  {
    const std::vector<std::byte> encoded;

    assert_runtime_error(
        [&encoded]()
        {
          decode_frame(encoded);
        },
        "websocket frame too short");
  }

  static void test_one_byte_input_throws()
  {
    const std::vector<std::byte> encoded{
        std::byte{0x81}};

    assert_runtime_error(
        [&encoded]()
        {
          decode_frame(encoded);
        },
        "websocket frame too short");
  }

  static void test_missing_short_payload_throws()
  {
    const std::vector<std::byte> encoded{
        std::byte{0x81},
        std::byte{0x05},
        std::byte{0x48},
        std::byte{0x69}};

    assert_runtime_error(
        [&encoded]()
        {
          decode_frame(encoded);
        },
        "incomplete websocket frame payload");
  }

  static void test_missing_16_bit_length_throws()
  {
    const std::vector<std::byte> encoded{
        std::byte{0x82},
        std::byte{0x7E},
        std::byte{0x00}};

    assert_runtime_error(
        [&encoded]()
        {
          decode_frame(encoded);
        },
        "incomplete websocket extended length (16-bit)");
  }

  static void test_missing_64_bit_length_throws()
  {
    const std::vector<std::byte> encoded{
        std::byte{0x82},
        std::byte{0x7F},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00}};

    assert_runtime_error(
        [&encoded]()
        {
          decode_frame(encoded);
        },
        "incomplete websocket extended length (64-bit)");
  }

  static void test_missing_mask_key_throws()
  {
    const std::vector<std::byte> encoded{
        std::byte{0x81},
        std::byte{0x80},
        std::byte{0x01},
        std::byte{0x02},
        std::byte{0x03}};

    assert_runtime_error(
        [&encoded]()
        {
          decode_frame(encoded);
        },
        "incomplete websocket mask key");
  }

  static void test_missing_masked_payload_throws()
  {
    const std::vector<std::byte> encoded{
        std::byte{0x81},
        std::byte{0x85},

        std::byte{0x37},
        std::byte{0xFA},
        std::byte{0x21},
        std::byte{0x3D},

        std::byte{0x7F},
        std::byte{0x9F}};

    assert_runtime_error(
        [&encoded]()
        {
          decode_frame(encoded);
        },
        "incomplete websocket frame payload");
  }

  static void test_missing_extended_payload_throws()
  {
    const std::vector<std::byte> encoded{
        std::byte{0x82},
        std::byte{0x7E},
        std::byte{0x00},
        std::byte{0x7E},
        std::byte{0x01},
        std::byte{0x02}};

    assert_runtime_error(
        [&encoded]()
        {
          decode_frame(encoded);
        },
        "incomplete websocket frame payload");
  }

} // namespace

int main()
{
  test_decode_empty_text_frame();
  test_decode_short_text_frame();
  test_decode_non_final_text_frame();
  test_decode_continuation_frame();

  test_decode_binary_frame();

  test_decode_masked_text_frame();
  test_decode_masked_binary_frame();

  test_decode_preserves_mask_key();
  test_decode_unmasked_frame_has_zero_mask_key();

  test_decode_payload_length_125();
  test_decode_payload_length_126();
  test_decode_payload_length_65535();
  test_decode_payload_length_65536();

  test_decode_masked_extended_payload();

  test_decode_ping_frame();
  test_decode_masked_ping_frame();

  test_decode_pong_frame();
  test_decode_masked_pong_frame();

  test_decode_close_frame();
  test_decode_masked_close_frame();

  test_decode_manual_masked_rfc_frame();

  test_decode_ignores_trailing_bytes();
  test_decode_does_not_modify_input();
  test_repeated_decode_is_stable();

  test_unknown_opcode_is_preserved();

  test_empty_input_throws();
  test_one_byte_input_throws();

  test_missing_short_payload_throws();
  test_missing_16_bit_length_throws();
  test_missing_64_bit_length_throws();

  test_missing_mask_key_throws();
  test_missing_masked_payload_throws();
  test_missing_extended_payload_throws();

  return 0;
}
