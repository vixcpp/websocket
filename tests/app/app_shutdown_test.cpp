/**
 * @file app_shutdown_test.cpp
 * @brief Verifies that WebSocket App borrows an injected executor.
 */

#include <atomic>
#include <cassert>
#include <memory>

#include <vix/executor/RuntimeExecutor.hpp>
#include <vix/websocket/App.hpp>

namespace
{
  using RuntimeExecutor = vix::executor::RuntimeExecutor;
  using WebSocketApp = vix::websocket::App;

  static void test_stop_preserves_the_injected_executor()
  {
    auto executor = std::make_shared<RuntimeExecutor>(1u);

    executor->start();

    assert(executor->started() == true);
    assert(executor->running() == true);
    assert(executor->accepting() == true);

    WebSocketApp app{"", executor};

    assert(app.executor() == executor);

    std::atomic<bool> executed_before_stop{false};

    assert(executor->post(
        [&executed_before_stop]()
        {
          executed_before_stop.store(true, std::memory_order_release);
        }) == true);

    executor->wait_idle();

    assert(executed_before_stop.load(std::memory_order_acquire) == true);

    app.stop();

    assert(executor->started() == true);
    assert(executor->running() == true);
    assert(executor->accepting() == true);

    std::atomic<bool> executed_after_stop{false};

    assert(executor->post(
        [&executed_after_stop]()
        {
          executed_after_stop.store(true, std::memory_order_release);
        }) == true);

    executor->wait_idle();

    assert(executed_after_stop.load(std::memory_order_acquire) == true);

    app.stop();

    assert(executor->started() == true);
    assert(executor->running() == true);
    assert(executor->accepting() == true);

    std::atomic<bool> executed_after_repeated_stop{false};

    assert(executor->post(
        [&executed_after_repeated_stop]()
        {
          executed_after_repeated_stop.store(true, std::memory_order_release);
        }) == true);

    executor->wait_idle();

    assert(executed_after_repeated_stop.load(std::memory_order_acquire) == true);

    executor->stop();
  }
} // namespace

int main()
{
  test_stop_preserves_the_injected_executor();
  return 0;
}
