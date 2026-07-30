/**
 *
 * @file config_defaults_test.cpp
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

#include <vix/websocket/config.hpp>

namespace
{
  using Config = vix::websocket::Config;

  static void test_config_type_traits()
  {
    static_assert(std::is_default_constructible_v<Config>);

    static_assert(std::is_copy_constructible_v<Config>);
    static_assert(std::is_copy_assignable_v<Config>);

    static_assert(std::is_move_constructible_v<Config>);
    static_assert(std::is_move_assignable_v<Config>);

    static_assert(std::is_destructible_v<Config>);
  }

  static void test_default_max_message_size()
  {
    Config config;

    assert(config.maxMessageSize == 64u * 1024u);
    assert(config.maxMessageSize == 65536u);
  }

  static void test_default_idle_timeout()
  {
    Config config;

    assert(config.idleTimeout == std::chrono::seconds{60});
    assert(config.idleTimeout.count() == 60);
  }

  static void test_default_per_message_deflate()
  {
    Config config;

    assert(config.enablePerMessageDeflate == true);
  }

  static void test_default_auto_ping_pong()
  {
    Config config;

    assert(config.autoPingPong == true);
  }

  static void test_default_ping_interval()
  {
    Config config;

    assert(config.pingInterval == std::chrono::seconds{30});
    assert(config.pingInterval.count() == 30);
  }

  static void test_all_default_values_together()
  {
    Config config;

    assert(config.maxMessageSize == 65536u);
    assert(config.idleTimeout == std::chrono::seconds{60});
    assert(config.enablePerMessageDeflate == true);
    assert(config.autoPingPong == true);
    assert(config.pingInterval == std::chrono::seconds{30});
  }

  static void test_multiple_default_configs_are_stable()
  {
    Config first;
    Config second;
    Config third;

    assert(first.maxMessageSize == second.maxMessageSize);
    assert(second.maxMessageSize == third.maxMessageSize);

    assert(first.idleTimeout == second.idleTimeout);
    assert(second.idleTimeout == third.idleTimeout);

    assert(
        first.enablePerMessageDeflate ==
        second.enablePerMessageDeflate);

    assert(
        second.enablePerMessageDeflate ==
        third.enablePerMessageDeflate);

    assert(first.autoPingPong == second.autoPingPong);
    assert(second.autoPingPong == third.autoPingPong);

    assert(first.pingInterval == second.pingInterval);
    assert(second.pingInterval == third.pingInterval);
  }

  static void test_default_configs_are_independent()
  {
    Config first;
    Config second;

    first.maxMessageSize = 256u * 1024u;
    first.idleTimeout = std::chrono::seconds{300};
    first.enablePerMessageDeflate = false;
    first.autoPingPong = false;
    first.pingInterval = std::chrono::seconds{10};

    assert(first.maxMessageSize == 262144u);
    assert(first.idleTimeout == std::chrono::seconds{300});
    assert(first.enablePerMessageDeflate == false);
    assert(first.autoPingPong == false);
    assert(first.pingInterval == std::chrono::seconds{10});

    assert(second.maxMessageSize == 65536u);
    assert(second.idleTimeout == std::chrono::seconds{60});
    assert(second.enablePerMessageDeflate == true);
    assert(second.autoPingPong == true);
    assert(second.pingInterval == std::chrono::seconds{30});
  }

  static void test_zero_values_can_be_represented()
  {
    Config config;

    config.maxMessageSize = 0u;
    config.idleTimeout = std::chrono::seconds{0};
    config.pingInterval = std::chrono::seconds{0};

    assert(config.maxMessageSize == 0u);
    assert(config.idleTimeout.count() == 0);
    assert(config.pingInterval.count() == 0);
  }

} // namespace

int main()
{
  test_config_type_traits();

  test_default_max_message_size();
  test_default_idle_timeout();
  test_default_per_message_deflate();
  test_default_auto_ping_pong();
  test_default_ping_interval();

  test_all_default_values_together();
  test_multiple_default_configs_are_stable();
  test_default_configs_are_independent();

  test_zero_values_can_be_represented();

  return 0;
}
