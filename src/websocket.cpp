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
        void set_affinity(int thread_id)
        {
#ifdef __linux__
            unsigned int hc = std::thread::hardware_concurrency();
            if (hc == 0)
                hc = 1;

            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(thread_id % hc, &cpuset);

            (void)pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#else
            (void)thread_id;
#endif
        }
    } // namespace

    Server::Server(vix::config::Config &coreConfig,
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

    Server::~Server() = default;

    void Server::init_acceptor(unsigned short port)
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

    void Server::run()
    {
        start_accept();
        start_io_threads();
    }

    void Server::start_accept()
    {
        // AVANT (qui plante avec ton Boost)
        // auto socket = std::make_shared<tcp::socket>(net::make_strand(*ioContext_));

        // APRÈS : on crée juste le socket sur l'io_context
        auto socket = std::make_shared<tcp::socket>(*ioContext_);

        acceptor_->async_accept(
            *socket,
            [this, socket](boost::system::error_code ec)
            {
                if (!ec && !stopRequested_)
                {
                    // on transfère le tcp::socket (par valeur) à la Session
                    handle_client(std::move(*socket));
                }

                if (!stopRequested_)
                {
                    start_accept();
                }
            });
    }

    void Server::handle_client(tcp::socket socket)
    {
        auto session = std::make_shared<Session>(
            std::move(socket), // move dans ws::stream
            wsConfig_,
            router_,
            executor_);

        session->run();
    }

    int Server::compute_io_thread_count() const
    {
        unsigned int hc = std::thread::hardware_concurrency();
        return static_cast<int>(std::max(1u, hc ? hc / 2 : 1u));
    }

    void Server::start_io_threads()
    {
        int n = compute_io_thread_count();
        ioThreads_.reserve(static_cast<std::size_t>(n));

        for (int i = 0; i < n; ++i)
        {
            ioThreads_.emplace_back(
                [this, i]()
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
                               "[WebSocket][Server] IO thread {} finished", i);
                });
        }
    }

    void Server::stop_async()
    {
        stopRequested_.store(true);

        if (acceptor_ && acceptor_->is_open())
        {
            boost::system::error_code ec;
            acceptor_->close(ec);
        }

        ioContext_->stop();
    }

    void Server::join_threads()
    {
        for (auto &t : ioThreads_)
        {
            if (t.joinable())
                t.join();
        }
    }

} // namespace vix::websocket
