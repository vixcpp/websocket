/**
 *
 * @file protocol_opcode_test.cpp
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
#include <cstdint>
#include <type_traits>

#include <vix/websocket/protocol.hpp>

namespace
{
  using Opcode = vix::websocket::detail::Opcode;

  static constexpr std::uint8_t opcode_value(Opcode opcode) noexcept
  {
    return static_cast<std::uint8_t>(opcode);
  }

  static void test_opcode_type_traits()
  {
    static_assert(std::is_enum_v<Opcode>);

    static_assert(
        std::is_same_v<
            std::underlying_type_t<Opcode>,
            std::uint8_t>);

    static_assert(std::is_copy_constructible_v<Opcode>);
    static_assert(std::is_copy_assignable_v<Opcode>);

    static_assert(std::is_move_constructible_v<Opcode>);
    static_assert(std::is_move_assignable_v<Opcode>);

    static_assert(std::is_trivially_copyable_v<Opcode>);
    static_assert(std::is_trivially_destructible_v<Opcode>);

    static_assert(sizeof(Opcode) == sizeof(std::uint8_t));
  }

  static void test_continuation_opcode_value()
  {
    static_assert(opcode_value(Opcode::Continuation) == 0x0u);

    assert(opcode_value(Opcode::Continuation) == 0x0u);
  }

  static void test_text_opcode_value()
  {
    static_assert(opcode_value(Opcode::Text) == 0x1u);

    assert(opcode_value(Opcode::Text) == 0x1u);
  }

  static void test_binary_opcode_value()
  {
    static_assert(opcode_value(Opcode::Binary) == 0x2u);

    assert(opcode_value(Opcode::Binary) == 0x2u);
  }

  static void test_close_opcode_value()
  {
    static_assert(opcode_value(Opcode::Close) == 0x8u);

    assert(opcode_value(Opcode::Close) == 0x8u);
  }

  static void test_ping_opcode_value()
  {
    static_assert(opcode_value(Opcode::Ping) == 0x9u);

    assert(opcode_value(Opcode::Ping) == 0x9u);
  }

  static void test_pong_opcode_value()
  {
    static_assert(opcode_value(Opcode::Pong) == 0xAu);

    assert(opcode_value(Opcode::Pong) == 0xAu);
  }

  static void test_data_opcodes_are_in_data_range()
  {
    assert(opcode_value(Opcode::Continuation) < 0x8u);
    assert(opcode_value(Opcode::Text) < 0x8u);
    assert(opcode_value(Opcode::Binary) < 0x8u);
  }

  static void test_control_opcodes_are_in_control_range()
  {
    assert(opcode_value(Opcode::Close) >= 0x8u);
    assert(opcode_value(Opcode::Ping) >= 0x8u);
    assert(opcode_value(Opcode::Pong) >= 0x8u);

    assert(opcode_value(Opcode::Close) <= 0xFu);
    assert(opcode_value(Opcode::Ping) <= 0xFu);
    assert(opcode_value(Opcode::Pong) <= 0xFu);
  }

  static void test_all_opcode_values_fit_low_nibble()
  {
    constexpr std::array<Opcode, 6> opcodes{
        Opcode::Continuation,
        Opcode::Text,
        Opcode::Binary,
        Opcode::Close,
        Opcode::Ping,
        Opcode::Pong};

    for (const Opcode opcode : opcodes)
    {
      assert((opcode_value(opcode) & 0xF0u) == 0u);
    }
  }

  static void test_opcode_values_are_unique()
  {
    constexpr std::array<Opcode, 6> opcodes{
        Opcode::Continuation,
        Opcode::Text,
        Opcode::Binary,
        Opcode::Close,
        Opcode::Ping,
        Opcode::Pong};

    for (std::size_t left = 0; left < opcodes.size(); ++left)
    {
      for (
          std::size_t right = left + 1;
          right < opcodes.size();
          ++right)
      {
        assert(
            opcode_value(opcodes[left]) !=
            opcode_value(opcodes[right]));
      }
    }
  }

  static void test_opcode_round_trip_from_wire_values()
  {
    constexpr std::array<std::uint8_t, 6> wire_values{
        0x0u,
        0x1u,
        0x2u,
        0x8u,
        0x9u,
        0xAu};

    for (const std::uint8_t wire_value : wire_values)
    {
      const Opcode opcode =
          static_cast<Opcode>(wire_value);

      assert(opcode_value(opcode) == wire_value);
    }
  }

  static void test_data_and_control_opcode_groups_are_distinct()
  {
    constexpr std::array<Opcode, 3> data_opcodes{
        Opcode::Continuation,
        Opcode::Text,
        Opcode::Binary};

    constexpr std::array<Opcode, 3> control_opcodes{
        Opcode::Close,
        Opcode::Ping,
        Opcode::Pong};

    for (const Opcode data_opcode : data_opcodes)
    {
      for (const Opcode control_opcode : control_opcodes)
      {
        assert(
            opcode_value(data_opcode) !=
            opcode_value(control_opcode));
      }
    }
  }

} // namespace

int main()
{
  test_opcode_type_traits();

  test_continuation_opcode_value();
  test_text_opcode_value();
  test_binary_opcode_value();

  test_close_opcode_value();
  test_ping_opcode_value();
  test_pong_opcode_value();

  test_data_opcodes_are_in_data_range();
  test_control_opcodes_are_in_control_range();

  test_all_opcode_values_fit_low_nibble();
  test_opcode_values_are_unique();
  test_opcode_round_trip_from_wire_values();
  test_data_and_control_opcode_groups_are_distinct();

  return 0;
}
