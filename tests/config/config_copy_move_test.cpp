/**
 *
 * @file config_copy_move_test.cpp
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
#include <chrono>
#include <cstddef>
#include <type_traits>
#include <utility>

#include <vix/websocket/config.hpp>

namespace
{
  using Config = vix::websocket::Config;

  static Config make_custom_config()
  {
    Config config;

    config.maxMessageSize = 256u * 1024u;
    config.idleTimeout = std::chrono::seconds{300};
    config.enablePerMessageDeflate = false;
    config.autoPingPong = false;
    config.pingInterval = std::chrono::seconds{10};

    return config;
  }

  static void assert_default_values(const Config &config)
  {
    assert(config.maxMessageSize == 65536u);
    assert(config.idleTimeout == std::chrono::seconds{60});
    assert(config.enablePerMessageDeflate == true);
    assert(config.autoPingPong == true);
    assert(config.pingInterval == std::chrono::seconds{30});
  }

  static void assert_custom_values(const Config &config)
  {
    assert(config.maxMessageSize == 262144u);
    assert(config.idleTimeout == std::chrono::seconds{300});
    assert(config.enablePerMessageDeflate == false);
    assert(config.autoPingPong == false);
    assert(config.pingInterval == std::chrono::seconds{10});
  }

  static void test_config_copy_move_type_traits()
  {
    static_assert(std::is_copy_constructible_v<Config>);
    static_assert(std::is_copy_assignable_v<Config>);

    static_assert(std::is_move_constructible_v<Config>);
    static_assert(std::is_move_assignable_v<Config>);

    static_assert(std::is_destructible_v<Config>);
  }

  static void test_copy_constructor_preserves_values()
  {
    Config source = make_custom_config();
    Config copy{source};

    assert_custom_values(source);
    assert_custom_values(copy);
  }

  static void test_copy_constructor_creates_independent_config()
  {
    Config source = make_custom_config();
    Config copy{source};

    copy.maxMessageSize = 1024u;
    copy.idleTimeout = std::chrono::seconds{1};
    copy.enablePerMessageDeflate = true;
    copy.autoPingPong = true;
    copy.pingInterval = std::chrono::seconds{2};

    assert_custom_values(source);

    assert(copy.maxMessageSize == 1024u);
    assert(copy.idleTimeout == std::chrono::seconds{1});
    assert(copy.enablePerMessageDeflate == true);
    assert(copy.autoPingPong == true);
    assert(copy.pingInterval == std::chrono::seconds{2});
  }

  static void test_copy_assignment_preserves_values()
  {
    Config source = make_custom_config();
    Config destination;

    assert_default_values(destination);

    destination = source;

    assert_custom_values(source);
    assert_custom_values(destination);
  }

  static void test_copy_assignment_replaces_previous_values()
  {
    Config source;
    Config destination = make_custom_config();

    assert_default_values(source);
    assert_custom_values(destination);

    destination = source;

    assert_default_values(source);
    assert_default_values(destination);
  }

  static void test_copy_assignment_creates_independent_config()
  {
    Config source = make_custom_config();
    Config destination;

    destination = source;

    destination.maxMessageSize = 4096u;
    destination.idleTimeout = std::chrono::seconds{15};
    destination.enablePerMessageDeflate = true;
    destination.autoPingPong = true;
    destination.pingInterval = std::chrono::seconds{5};

    assert_custom_values(source);

    assert(destination.maxMessageSize == 4096u);
    assert(destination.idleTimeout == std::chrono::seconds{15});
    assert(destination.enablePerMessageDeflate == true);
    assert(destination.autoPingPong == true);
    assert(destination.pingInterval == std::chrono::seconds{5});
  }

  static void test_self_copy_assignment_is_stable()
  {
    Config config = make_custom_config();

    Config *same = &config;
    config = *same;

    assert_custom_values(config);
  }

  static void test_move_constructor_preserves_destination_values()
  {
    Config source = make_custom_config();
    Config destination{std::move(source)};

    assert_custom_values(destination);
  }

  static void test_moved_from_config_can_be_reassigned()
  {
    Config source = make_custom_config();
    Config destination{std::move(source)};

    assert_custom_values(destination);

    source = Config{};

    assert_default_values(source);
    assert_custom_values(destination);
  }

  static void test_move_assignment_preserves_destination_values()
  {
    Config source = make_custom_config();
    Config destination;

    destination = std::move(source);

    assert_custom_values(destination);
  }

  static void test_move_assignment_replaces_previous_values()
  {
    Config source;
    Config destination = make_custom_config();

    destination = std::move(source);

    assert_default_values(destination);
  }

  static void test_move_assigned_source_can_be_reused()
  {
    Config source = make_custom_config();
    Config destination;

    destination = std::move(source);

    assert_custom_values(destination);

    source.maxMessageSize = 8192u;
    source.idleTimeout = std::chrono::seconds{90};
    source.enablePerMessageDeflate = true;
    source.autoPingPong = false;
    source.pingInterval = std::chrono::seconds{45};

    assert(source.maxMessageSize == 8192u);
    assert(source.idleTimeout == std::chrono::seconds{90});
    assert(source.enablePerMessageDeflate == true);
    assert(source.autoPingPong == false);
    assert(source.pingInterval == std::chrono::seconds{45});

    assert_custom_values(destination);
  }

  static void test_return_by_value_preserves_values()
  {
    Config config = make_custom_config();

    assert_custom_values(config);
  }

  static void test_copy_chain_preserves_values()
  {
    Config first = make_custom_config();
    Config second{first};
    Config third;

    third = second;

    assert_custom_values(first);
    assert_custom_values(second);
    assert_custom_values(third);
  }

  static void test_move_chain_preserves_final_values()
  {
    Config first = make_custom_config();
    Config second{std::move(first)};
    Config third;

    third = std::move(second);

    assert_custom_values(third);
  }

} // namespace

int main()
{
  test_config_copy_move_type_traits();

  test_copy_constructor_preserves_values();
  test_copy_constructor_creates_independent_config();

  test_copy_assignment_preserves_values();
  test_copy_assignment_replaces_previous_values();
  test_copy_assignment_creates_independent_config();
  test_self_copy_assignment_is_stable();

  test_move_constructor_preserves_destination_values();
  test_moved_from_config_can_be_reassigned();

  test_move_assignment_preserves_destination_values();
  test_move_assignment_replaces_previous_values();
  test_move_assigned_source_can_be_reused();

  test_return_by_value_preserves_values();
  test_copy_chain_preserves_values();
  test_move_chain_preserves_final_values();

  return 0;
}
