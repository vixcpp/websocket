#include <vix/app/App.hpp>
#include <vix/server/ServerReadyPresentation.hpp>
#include <vix/websocket/AttachedRuntime.hpp>

#include <cassert>
#include <type_traits>
#include <utility>

int main()
{
  static_assert(std::is_same_v<decltype(std::declval<const vix::App &>().server_ready_info()),
                               vix::server::ServerReadyInfo>);

  vix::server::ServerReadyInfo info;
  info.host = "127.0.0.1";
  info.port = 8080;
  info.scheme = "https";
  info.show_ws = true;
  info.ws_host = "127.0.0.1";
  info.ws_port = 9090;
  info.ws_scheme = "wss";
  info.ws_path = "/";

  assert(info.host == "127.0.0.1");
  assert(info.port == 8080);
  assert(info.scheme == "https");
  assert(info.show_ws);
  assert(info.ws_host == "127.0.0.1");
  assert(info.ws_port == 9090);
  assert(info.ws_scheme == "wss");
  assert(info.ws_path == "/");
  return 0;
}
