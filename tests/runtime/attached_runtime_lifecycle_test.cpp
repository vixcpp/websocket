/**
 * @file attached_runtime_lifecycle_test.cpp
 * @brief Verifies AttachedRuntime borrows externally supplied executor lifecycle.
 */

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>

#include <vix/app/App.hpp>
#include <vix/config/Config.hpp>
#include <vix/executor/RuntimeExecutor.hpp>
#include <vix/websocket/AttachedRuntime.hpp>
#include <vix/websocket/server.hpp>

namespace
{
  using RuntimeExecutor = vix::executor::RuntimeExecutor;
  using WebSocketServer = vix::websocket::Server;

  static std::uint16_t test_port()
  {
    const char *value = std::getenv("VIX_WEBSOCKET_TEST_PORT");

    if (value == nullptr || *value == '\0')
    {
      return 19260u;
    }

    try
    {
      const unsigned long parsed = std::stoul(value);

      assert(parsed > 0u);
      assert(parsed <= 65535u);

      return static_cast<std::uint16_t>(parsed);
    }
    catch (...)
    {
      assert(false);
      return 19260u;
    }
  }

  static void test_external_executor_is_preserved_by_attached_runtime_finalization()
  {
    auto executor = std::make_shared<RuntimeExecutor>(1u);
    executor->start();

    assert(executor->started() == true);
    assert(executor->running() == true);
    assert(executor->accepting() == true);

    std::atomic<bool> executed_before_orchestration{false};

    assert(executor->post(
        [&executed_before_orchestration]()
        {
          executed_before_orchestration.store(true, std::memory_order_release);
        }) == true);

    executor->wait_idle();

    assert(executed_before_orchestration.load(std::memory_order_acquire) == true);

    vix::config::Config config{};
    config.set("websocket.host", "127.0.0.1");
    config.set("websocket.port", test_port());

    vix::App app{executor};
    WebSocketServer websocket{config, executor};

    {
      // Construction starts the local WebSocket listener. finalize_shutdown()
      // provides the blocking, joined shutdown boundary for that listener.
      vix::websocket::AttachedRuntime runtime{app, websocket, executor};

      assert(executor->started() == true);
      assert(executor->running() == true);
      assert(executor->accepting() == true);

      // Close the app while the attached WebSocket server is still alive so
      // its registered shutdown callback follows the normal lifecycle path.
      app.close();

      runtime.finalize_shutdown();
      runtime.finalize_shutdown();
    }

    assert(executor->started() == true);
    assert(executor->running() == true);
    assert(executor->accepting() == true);

    std::atomic<bool> executed_after_finalization{false};

    assert(executor->post(
        [&executed_after_finalization]()
        {
          executed_after_finalization.store(true, std::memory_order_release);
        }) == true);

    executor->wait_idle();

    assert(executed_after_finalization.load(std::memory_order_acquire) == true);

    executor->stop();
  }
} // namespace

int main()
{
  test_external_executor_is_preserved_by_attached_runtime_finalization();
  return 0;
}
