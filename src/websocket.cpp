#include <vix/websocket/websocket.hpp>
#include <vix/websocket/session.hpp>

#include <system_error>
#include <algorithm>
#include <chrono>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace vix::websocket
{
    static Logger &logger = Logger::getInstance();

    namespace
    {
        void set_affinity(std::size_t thread_index)
        {
#ifdef __linux__
            unsigned int hc = std::thread::hardware_concurrency();
            if (hc == 0u)
                hc = 1u;

            const unsigned int cpu =
                static_cast<unsigned int>(thread_index % static_cast<std::size_t>(hc));

            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(cpu, &cpuset);

            (void)pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#else
            (void)thread_index;
#endif
        }
    }

    LowLevelServer::LowLevelServer(vix::config::Config &coreConfig,
                                   std::shared_ptr<vix::executor::IExecutor> executor,
                                   std::shared_ptr<Router> router)
        : coreConfig_(coreConfig),
          wsConfig_(Config::from_core(coreConfig_)),
          executor_(std::move(executor)),
          router_(std::move(router)),
          ioContext_(std::make_shared<net::io_context>()),
          acceptor_(nullptr),
          ioThreads_(),
          stopRequested_(false)
    {
        int port = coreConfig_.getInt("websocket.port", 9090);
        if (port < 1024 || port > 65535)
        {
            logger.log(Logger::Level::ERROR,
                       "[WebSocket][Server] Port {} out of range (1024-65535)", port);
            throw std::invalid_argument("Invalid WebSocket port");
        }

        init_acceptor(static_cast<unsigned short>(port));

        logger.log(Logger::Level::INFO,
                   "[WebSocket][Server] Config -> maxMessageSize={} idleTimeout={}s pingInterval={}s",
                   wsConfig_.maxMessageSize,
                   wsConfig_.idleTimeout.count(),
                   wsConfig_.pingInterval.count());
    }

    LowLevelServer::~LowLevelServer() = default;

    void LowLevelServer::init_acceptor(unsigned short port)
    {
        acceptor_ = std::make_unique<tcp::acceptor>(*ioContext_);
        boost::system::error_code ec;

        tcp::endpoint endpoint(tcp::v4(), port);
        acceptor_->open(endpoint.protocol(), ec);
        if (ec)
            throw std::system_error(ec, "open acceptor");

        acceptor_->set_option(boost::asio::socket_base::reuse_address(true), ec);
        if (ec)
            throw std::system_error(ec, "reuse_address");

        acceptor_->bind(endpoint, ec);
        if (ec)
            throw std::system_error(ec, "bind acceptor");

        acceptor_->listen(boost::asio::socket_base::max_connections, ec);
        if (ec)
            throw std::system_error(ec, "listen acceptor");

        logger.log(Logger::Level::INFO,
                   "[WebSocket][Server] Listening on port {}", port);
    }

    void LowLevelServer::run()
    {
        start_accept();
        start_io_threads();
    }

    void LowLevelServer::start_accept()
    {
        auto socket = std::make_shared<tcp::socket>(*ioContext_);

        acceptor_->async_accept(
            *socket,
            [this, socket](boost::system::error_code ec)
            {
                if (!ec && !stopRequested_)
                {
                    // transfer tcp::socket by value to Session
                    handle_client(std::move(*socket));
                }

                if (!stopRequested_)
                {
                    start_accept();
                }
            });
    }

    void LowLevelServer::handle_client(tcp::socket socket)
    {
        auto session = std::make_shared<Session>(
            std::move(socket), // move into ws::stream
            wsConfig_,
            router_,
            executor_);

        session->run();
    }

    std::size_t LowLevelServer::compute_io_thread_count() const
    {
        const unsigned int hc = std::thread::hardware_concurrency();
        const unsigned int v = (hc != 0u) ? (hc / 2u) : 1u;
        return static_cast<std::size_t>(std::max(1u, v));
    }

    void LowLevelServer::start_io_threads()
    {
        const std::size_t n = static_cast<std::size_t>(compute_io_thread_count());
        ioThreads_.reserve(n);

        for (std::size_t i = 0; i < n; ++i)
        {
            ioThreads_.emplace_back([this, i]()
                                    {
        try
        {
            set_affinity(i);
            ioContext_->run();
        }
        catch (const std::exception &e)
        {
            logger.log(Logger::Level::ERROR,
                       "[WebSocket][Server] IO thread {} error: {}", i, e.what());
        }

        logger.log(Logger::Level::INFO,
                   "[WebSocket][Server] IO thread {} finished", i); });
        }
    }

    void LowLevelServer::stop_async()
    {
        stopRequested_.store(true);

        if (acceptor_ && acceptor_->is_open())
        {
            boost::system::error_code ec;
            acceptor_->close(ec);
        }

        ioContext_->stop();
    }

    void LowLevelServer::join_threads()
    {
        for (auto &t : ioThreads_)
        {
            if (t.joinable())
                t.join();
        }
    }

} // namespace vix::websocket
