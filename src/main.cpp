// #include <iostream>
// #include <memory>
// #include <csignal>

// #include <vix/config/Config.hpp>
// #include <vix/websocket/websocket.hpp>
// #include <vix/websocket/router.hpp>
// #include <vix/websocket/session.hpp>
// #include <vix/experimental/ThreadPoolExecutor.hpp>

// int main()
// {
//     try
//     {
//         vix::config::Config coreConfig{
//             "./config/config.json" // adapte si besoin
//         };

//         //    make_threadpool_executor -> std::unique_ptr<IExecutor>
//         auto execUnique = vix::experimental::make_threadpool_executor(
//             4, // threads de base
//             8, // max threads
//             0  // priorité par défaut
//         );

//         std::shared_ptr<vix::executor::IExecutor> executor(
//             std::move(execUnique) // shared_ptr prend la propriété
//         );

//         auto router = std::make_shared<vix::websocket::Router>();

//         router->on_open([](vix::websocket::Session &session)
//                         { session.send_text("Welcome to Vix WebSocket 👋"); });

//         router->on_message([](vix::websocket::Session &session, std::string_view msg)
//                            {
//             std::string payload = "echo: ";
//             payload.append(msg.begin(), msg.end());
//             session.send_text(payload); });

//         vix::websocket::Server server(coreConfig, executor, router);

//         server.run();

//         std::cout << "[main] WebSocket server started. "
//                      "Press ENTER to stop..."
//                   << std::endl;

//         std::cin.get();

//         server.stop_async();
//         server.join_threads();

//         return 0;
//     }
//     catch (const std::exception &e)
//     {
//         std::cerr << "[main] Fatal error: " << e.what() << std::endl;
//         return 1;
//     }
// }
