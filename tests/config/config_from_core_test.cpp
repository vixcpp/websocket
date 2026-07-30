/**
 *
 * @file config_from_core_test.cpp
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
#include <cstdlib>
#include <filesystem>
#include <string>
#include <type_traits>
#include <utility>

#include <vix/config/Config.hpp>
#include <vix/websocket/config.hpp>

namespace
{
  using CoreConfig = vix::config::Config;
  using WebSocketConfig = vix::websocket::Config;

  static void set_env_var(
      const char *name,
      const std::string &value)
  {
#if defined(_WIN32)
    const std::string assignment =
        std::string{name} + "=" + value;

    const int rc = _putenv(assignment.c_str());

    assert(rc == 0);
#else
    const int rc = setenv(name, value.c_str(), 1);

    assert(rc == 0);
#endif
  }

  static void unset_env_var(const char *name)
  {
#if defined(_WIN32)
    const std::string assignment =
        std::string{name} + "=";

    const int rc = _putenv(assignment.c_str());

    assert(rc == 0);
#else
    const int rc = unsetenv(name);

    assert(rc == 0);
#endif
  }

  static void clear_websocket_env()
  {
    unset_env_var("WEBSOCKET_HOST");
    unset_env_var("WEBSOCKET_PORT");

    unset_env_var("WEBSOCKET_MAX_MESSAGE_SIZE");
    unset_env_var("WEBSOCKET_IDLE_TIMEOUT");
    unset_env_var("WEBSOCKET_ENABLE_DEFLATE");
    unset_env_var("WEBSOCKET_AUTO_PING_PONG");
    unset_env_var("WEBSOCKET_PING_INTERVAL");

    set_env_var("VIX_ENV_SILENT", "true");
  }

  static std::filesystem::path make_empty_env_path()
  {
    const auto stamp =
        std::chrono::steady_clock::now()
            .time_since_epoch()
            .count();

    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("vix_websocket_config_from_core_test_" +
         std::to_string(stamp));

    std::error_code ec;

    std::filesystem::remove_all(dir, ec);

    ec.clear();

    std::filesystem::create_directories(dir, ec);

    assert(!ec);

    return dir / ".env";
  }

  static CoreConfig make_core_config_from_current_env()
  {
    const std::filesystem::path env_path =
        make_empty_env_path();

    CoreConfig config{env_path};

    return config;
  }

  static CoreConfig make_clean_core_config()
  {
    clear_websocket_env();

    return make_core_config_from_current_env();
  }

  static void assert_default_values(
      const WebSocketConfig &config)
  {
    assert(config.maxMessageSize == 65536u);
    assert(config.idleTimeout == std::chrono::seconds{60});
    assert(config.enablePerMessageDeflate == true);
    assert(config.autoPingPong == true);
    assert(config.pingInterval == std::chrono::seconds{30});
  }

  static void assert_custom_values(
      const WebSocketConfig &config)
  {
    assert(config.maxMessageSize == 262144u);
    assert(config.idleTimeout == std::chrono::seconds{300});
    assert(config.enablePerMessageDeflate == false);
    assert(config.autoPingPong == false);
    assert(config.pingInterval == std::chrono::seconds{10});
  }

  static void set_custom_core_values(CoreConfig &core)
  {
    core.set(
        "websocket.max_message_size",
        262144);

    core.set(
        "websocket.idle_timeout",
        300);

    core.set(
        "websocket.enable_deflate",
        false);

    core.set(
        "websocket.auto_ping_pong",
        false);

    core.set(
        "websocket.ping_interval",
        10);
  }

  static void test_from_core_type_contract()
  {
    static_assert(
        std::is_same_v<
            decltype(WebSocketConfig::from_core(
                std::declval<const CoreConfig &>())),
            WebSocketConfig>);
  }

  static void test_empty_core_config_uses_websocket_defaults()
  {
    CoreConfig core = make_clean_core_config();

    const WebSocketConfig websocket =
        WebSocketConfig::from_core(core);

    assert_default_values(websocket);
  }

  static void test_max_message_size_from_core()
  {
    CoreConfig core = make_clean_core_config();

    core.set(
        "websocket.max_message_size",
        131072);

    const WebSocketConfig websocket =
        WebSocketConfig::from_core(core);

    assert(websocket.maxMessageSize == 131072u);

    assert(websocket.idleTimeout == std::chrono::seconds{60});
    assert(websocket.enablePerMessageDeflate == true);
    assert(websocket.autoPingPong == true);
    assert(websocket.pingInterval == std::chrono::seconds{30});
  }

  static void test_idle_timeout_from_core()
  {
    CoreConfig core = make_clean_core_config();

    core.set(
        "websocket.idle_timeout",
        120);

    const WebSocketConfig websocket =
        WebSocketConfig::from_core(core);

    assert(websocket.maxMessageSize == 65536u);
    assert(websocket.idleTimeout == std::chrono::seconds{120});
    assert(websocket.enablePerMessageDeflate == true);
    assert(websocket.autoPingPong == true);
    assert(websocket.pingInterval == std::chrono::seconds{30});
  }

  static void test_enable_deflate_from_core()
  {
    CoreConfig core = make_clean_core_config();

    core.set(
        "websocket.enable_deflate",
        false);

    const WebSocketConfig websocket =
        WebSocketConfig::from_core(core);

    assert(websocket.maxMessageSize == 65536u);
    assert(websocket.idleTimeout == std::chrono::seconds{60});
    assert(websocket.enablePerMessageDeflate == false);
    assert(websocket.autoPingPong == true);
    assert(websocket.pingInterval == std::chrono::seconds{30});
  }

  static void test_auto_ping_pong_from_core()
  {
    CoreConfig core = make_clean_core_config();

    core.set(
        "websocket.auto_ping_pong",
        false);

    const WebSocketConfig websocket =
        WebSocketConfig::from_core(core);

    assert(websocket.maxMessageSize == 65536u);
    assert(websocket.idleTimeout == std::chrono::seconds{60});
    assert(websocket.enablePerMessageDeflate == true);
    assert(websocket.autoPingPong == false);
    assert(websocket.pingInterval == std::chrono::seconds{30});
  }

  static void test_ping_interval_from_core()
  {
    CoreConfig core = make_clean_core_config();

    core.set(
        "websocket.ping_interval",
        15);

    const WebSocketConfig websocket =
        WebSocketConfig::from_core(core);

    assert(websocket.maxMessageSize == 65536u);
    assert(websocket.idleTimeout == std::chrono::seconds{60});
    assert(websocket.enablePerMessageDeflate == true);
    assert(websocket.autoPingPong == true);
    assert(websocket.pingInterval == std::chrono::seconds{15});
  }

  static void test_all_values_from_core()
  {
    CoreConfig core = make_clean_core_config();

    set_custom_core_values(core);

    const WebSocketConfig websocket =
        WebSocketConfig::from_core(core);

    assert_custom_values(websocket);
  }

  static void test_zero_idle_timeout_is_preserved()
  {
    CoreConfig core = make_clean_core_config();

    core.set(
        "websocket.idle_timeout",
        0);

    const WebSocketConfig websocket =
        WebSocketConfig::from_core(core);

    assert(websocket.idleTimeout == std::chrono::seconds{0});
    assert(websocket.idleTimeout.count() == 0);

    assert(websocket.maxMessageSize == 65536u);
    assert(websocket.enablePerMessageDeflate == true);
    assert(websocket.autoPingPong == true);
    assert(websocket.pingInterval == std::chrono::seconds{30});
  }

  static void test_zero_ping_interval_is_preserved()
  {
    CoreConfig core = make_clean_core_config();

    core.set(
        "websocket.ping_interval",
        0);

    const WebSocketConfig websocket =
        WebSocketConfig::from_core(core);

    assert(websocket.pingInterval == std::chrono::seconds{0});
    assert(websocket.pingInterval.count() == 0);

    assert(websocket.maxMessageSize == 65536u);
    assert(websocket.idleTimeout == std::chrono::seconds{60});
    assert(websocket.enablePerMessageDeflate == true);
    assert(websocket.autoPingPong == true);
  }

  static void test_environment_values_are_mapped_from_core()
  {
    clear_websocket_env();

    set_env_var(
        "WEBSOCKET_MAX_MESSAGE_SIZE",
        "262144");

    set_env_var(
        "WEBSOCKET_IDLE_TIMEOUT",
        "300");

    set_env_var(
        "WEBSOCKET_ENABLE_DEFLATE",
        "false");

    set_env_var(
        "WEBSOCKET_AUTO_PING_PONG",
        "false");

    set_env_var(
        "WEBSOCKET_PING_INTERVAL",
        "10");

    CoreConfig core =
        make_core_config_from_current_env();

    const WebSocketConfig websocket =
        WebSocketConfig::from_core(core);

    assert_custom_values(websocket);

    clear_websocket_env();
  }

  static void test_true_boolean_environment_values()
  {
    clear_websocket_env();

    set_env_var(
        "WEBSOCKET_ENABLE_DEFLATE",
        "true");

    set_env_var(
        "WEBSOCKET_AUTO_PING_PONG",
        "true");

    CoreConfig core =
        make_core_config_from_current_env();

    const WebSocketConfig websocket =
        WebSocketConfig::from_core(core);

    assert(websocket.enablePerMessageDeflate == true);
    assert(websocket.autoPingPong == true);

    clear_websocket_env();
  }

  static void test_numeric_boolean_environment_values()
  {
    clear_websocket_env();

    set_env_var(
        "WEBSOCKET_ENABLE_DEFLATE",
        "0");

    set_env_var(
        "WEBSOCKET_AUTO_PING_PONG",
        "1");

    CoreConfig core =
        make_core_config_from_current_env();

    const WebSocketConfig websocket =
        WebSocketConfig::from_core(core);

    assert(websocket.enablePerMessageDeflate == false);
    assert(websocket.autoPingPong == true);

    clear_websocket_env();
  }

  static void test_raw_core_values_have_priority_over_environment()
  {
    clear_websocket_env();

    set_env_var(
        "WEBSOCKET_MAX_MESSAGE_SIZE",
        "1024");

    set_env_var(
        "WEBSOCKET_IDLE_TIMEOUT",
        "5");

    set_env_var(
        "WEBSOCKET_ENABLE_DEFLATE",
        "true");

    set_env_var(
        "WEBSOCKET_AUTO_PING_PONG",
        "true");

    set_env_var(
        "WEBSOCKET_PING_INTERVAL",
        "2");

    CoreConfig core =
        make_core_config_from_current_env();

    set_custom_core_values(core);

    const WebSocketConfig websocket =
        WebSocketConfig::from_core(core);

    assert_custom_values(websocket);

    clear_websocket_env();
  }

  static void test_from_core_does_not_modify_core_config()
  {
    CoreConfig core = make_clean_core_config();

    set_custom_core_values(core);

    const WebSocketConfig websocket =
        WebSocketConfig::from_core(core);

    assert_custom_values(websocket);

    assert(
        core.getInt(
            "websocket.max_message_size",
            -1) == 262144);

    assert(
        core.getInt(
            "websocket.idle_timeout",
            -1) == 300);

    assert(
        core.getBool(
            "websocket.enable_deflate",
            true) == false);

    assert(
        core.getBool(
            "websocket.auto_ping_pong",
            true) == false);

    assert(
        core.getInt(
            "websocket.ping_interval",
            -1) == 10);
  }

  static void test_derived_config_is_independent_from_core()
  {
    CoreConfig core = make_clean_core_config();

    set_custom_core_values(core);

    WebSocketConfig websocket =
        WebSocketConfig::from_core(core);

    websocket.maxMessageSize = 4096u;
    websocket.idleTimeout = std::chrono::seconds{1};
    websocket.enablePerMessageDeflate = true;
    websocket.autoPingPong = true;
    websocket.pingInterval = std::chrono::seconds{2};

    assert(
        core.getInt(
            "websocket.max_message_size",
            -1) == 262144);

    assert(
        core.getInt(
            "websocket.idle_timeout",
            -1) == 300);

    assert(
        core.getBool(
            "websocket.enable_deflate",
            true) == false);

    assert(
        core.getBool(
            "websocket.auto_ping_pong",
            true) == false);

    assert(
        core.getInt(
            "websocket.ping_interval",
            -1) == 10);
  }

  static void test_repeated_from_core_calls_are_stable()
  {
    CoreConfig core = make_clean_core_config();

    set_custom_core_values(core);

    const WebSocketConfig first =
        WebSocketConfig::from_core(core);

    const WebSocketConfig second =
        WebSocketConfig::from_core(core);

    const WebSocketConfig third =
        WebSocketConfig::from_core(core);

    assert_custom_values(first);
    assert_custom_values(second);
    assert_custom_values(third);
  }

  static void test_derived_configs_are_independent()
  {
    CoreConfig core = make_clean_core_config();

    set_custom_core_values(core);

    WebSocketConfig first =
        WebSocketConfig::from_core(core);

    const WebSocketConfig second =
        WebSocketConfig::from_core(core);

    first.maxMessageSize = 1024u;
    first.idleTimeout = std::chrono::seconds{1};
    first.enablePerMessageDeflate = true;
    first.autoPingPong = true;
    first.pingInterval = std::chrono::seconds{1};

    assert(first.maxMessageSize == 1024u);
    assert(first.idleTimeout == std::chrono::seconds{1});
    assert(first.enablePerMessageDeflate == true);
    assert(first.autoPingPong == true);
    assert(first.pingInterval == std::chrono::seconds{1});

    assert_custom_values(second);
  }

  static void test_websocket_host_and_port_do_not_change_session_config()
  {
    CoreConfig core = make_clean_core_config();

    core.set(
        "websocket.host",
        "127.0.0.1");

    core.set(
        "websocket.port",
        19090);

    const WebSocketConfig websocket =
        WebSocketConfig::from_core(core);

    assert_default_values(websocket);

    assert(
        core.getString(
            "websocket.host",
            "") == "127.0.0.1");

    assert(
        core.getInt(
            "websocket.port",
            -1) == 19090);
  }

} // namespace

int main()
{
  test_from_core_type_contract();

  test_empty_core_config_uses_websocket_defaults();

  test_max_message_size_from_core();
  test_idle_timeout_from_core();
  test_enable_deflate_from_core();
  test_auto_ping_pong_from_core();
  test_ping_interval_from_core();

  test_all_values_from_core();

  test_zero_idle_timeout_is_preserved();
  test_zero_ping_interval_is_preserved();

  test_environment_values_are_mapped_from_core();
  test_true_boolean_environment_values();
  test_numeric_boolean_environment_values();

  test_raw_core_values_have_priority_over_environment();

  test_from_core_does_not_modify_core_config();
  test_derived_config_is_independent_from_core();

  test_repeated_from_core_calls_are_stable();
  test_derived_configs_are_independent();

  test_websocket_host_and_port_do_not_change_session_config();

  return 0;
}
