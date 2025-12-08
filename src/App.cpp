#include <vix/websocket/App.hpp>
#include <vix/websocket/protocol.hpp>

#include <utility>

namespace vix::websocket
{
    using vix::experimental::make_threadpool_executor;

    App::App(const std::string &configPath,
             std::size_t minThreads,
             std::size_t maxThreads,
             int defaultPrio)
        : config_(configPath),
          executor_(make_threadpool_executor(minThreads, maxThreads, defaultPrio)),
          server_(config_, executor_)
    {
        // Install an initial dispatcher (possibly with no routes yet).
        install_dispatcher();
    }

    App &App::ws(const std::string &endpoint, TypedHandler handler)
    {
        routes_.push_back(Route{endpoint, std::move(handler)});

        // Reinstall dispatcher so that new handlers are taken into account.
        // Assumption: ws() is called during startup, before run_blocking().
        install_dispatcher();
        return *this;
    }

    void App::install_dispatcher()
    {
        // NOTE:
        //  - We capture `this` assuming the App object outlives the server.
        //  - Typical usage: configure routes, then call run_blocking().
        //  - If someone calls ws() after run_blocking() has started,
        //    routing is updated for *future* messages (not retroactive).

        server_.on_typed_message(
            [this](Session &session,
                   const std::string &type,
                   const vix::json::kvs &payload)
            {
                // Minimalistic version: invoke all registered handlers.
                // The `endpoint` field is currently a logical label,
                // ready for future path-based routing.
                for (auto &route : routes_)
                {
                    if (route.handler)
                    {
                        route.handler(session, type, payload);
                    }
                }
            });
    }

    void App::run_blocking()
    {
        server_.listen_blocking();
    }

    void App::stop()
    {
        // Delegate to underlying server (must expose a stop() API).
        server_.stop();
    }

} // namespace vix::websocket
