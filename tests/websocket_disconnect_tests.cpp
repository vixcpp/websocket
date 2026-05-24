#include <vix/utils/NetworkError.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
  int failures = 0;

  void expect_true(bool value, const std::string &name)
  {
    if (!value)
    {
      std::cerr << "FAILED: expected true: " << name << "\n";
      ++failures;
    }
  }

  void expect_false(bool value, const std::string &name)
  {
    if (value)
    {
      std::cerr << "FAILED: expected false: " << name << "\n";
      ++failures;
    }
  }

  void test_websocket_disconnect_messages()
  {
    expect_true(
        vix::utils::is_normal_network_disconnect_message("Broken pipe"),
        "Broken pipe");

    expect_true(
        vix::utils::is_normal_network_disconnect_message("connection reset"),
        "connection reset");

    expect_true(
        vix::utils::is_normal_network_disconnect_message("Connection reset by peer"),
        "Connection reset by peer");

    expect_true(
        vix::utils::is_normal_network_disconnect_message("canceled"),
        "canceled");

    expect_true(
        vix::utils::is_normal_network_disconnect_message("cancelled"),
        "cancelled");

    expect_true(
        vix::utils::is_normal_network_disconnect_message("unexpected EOF while reading websocket HTTP head"),
        "unexpected EOF while reading websocket HTTP head");
  }

  void test_websocket_protocol_errors_stay_visible()
  {
    expect_false(
        vix::utils::is_normal_network_disconnect_message("websocket handshake must use GET"),
        "websocket handshake must use GET");

    expect_false(
        vix::utils::is_normal_network_disconnect_message("missing Upgrade: websocket"),
        "missing Upgrade: websocket");

    expect_false(
        vix::utils::is_normal_network_disconnect_message("missing Sec-WebSocket-Key"),
        "missing Sec-WebSocket-Key");

    expect_false(
        vix::utils::is_normal_network_disconnect_message("unsupported Sec-WebSocket-Version"),
        "unsupported Sec-WebSocket-Version");

    expect_false(
        vix::utils::is_normal_network_disconnect_message("websocket frame write failed"),
        "websocket frame write failed");
  }
}

int main()
{
  test_websocket_disconnect_messages();
  test_websocket_protocol_errors_stay_visible();

  if (failures != 0)
  {
    std::cerr << "websocket_disconnect_tests failed with "
              << failures
              << " failure(s)\n";

    return EXIT_FAILURE;
  }

  std::cout << "websocket_disconnect_tests passed\n";
  return EXIT_SUCCESS;
}
