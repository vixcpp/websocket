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
        install_dispatcher();
    }

    App &App::ws(const std::string &endpoint, TypedHandler handler)
    {
        routes_.push_back(Route{endpoint, std::move(handler)});

        install_dispatcher();
        return *this;
    }

    void App::install_dispatcher()
    {
        server_.on_typed_message(
            [this](Session &session,
                   const std::string &type,
                   const vix::json::kvs &payload)
            {
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
        server_.stop();
    }

} // namespace vix::websocket
