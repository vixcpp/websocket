#include <iostream>
#include <string>

#include <vix/websocket/client.hpp>
#include <vix/websocket/protocol.hpp>

int main()
{
    using vix::websocket::Client;
    using vix::websocket::JsonMessage;

    auto client = Client::create("localhost", "9090", "/");

    client->on_open([]
                    { std::cout << "[client] Connected ✅" << std::endl; });

    client->on_message([](const std::string &msg)
                       {
        auto jm = JsonMessage::parse(msg);

        if (!jm)
        {
            std::cout << msg << std::endl;
            return;
        }

        std::string type = jm->type;

        if (type == "chat.system")
        {
            std::string text = jm->get_string("text");
            std::cout << "[system] " << text << std::endl;
        }
        else if (type == "chat.message")
        {
            std::string user = jm->get_string("user");
            std::string text = jm->get_string("text");

            if (user.empty()) user = "anonymous";

            std::cout << "[chat] " << user << ": " << text << std::endl;
        }
        else
        {
            std::cout << msg << std::endl;
        } });

    client->on_close([]
                     { std::cout << "[client] Disconnected." << std::endl; });

    client->on_error([](const boost::system::error_code &ec)
                     { std::cerr << "[client] error: " << ec.message() << std::endl; });

    client->enable_auto_reconnect(true, std::chrono::seconds(3));
    client->enable_heartbeat(std::chrono::seconds(20));

    client->connect();

    // -------------------------
    // Prompt username
    // -------------------------
    std::cout << "Pseudo: ";
    std::string user;
    std::getline(std::cin, user);
    if (user.empty())
        user = "anonymous";

    std::cout << "Type messages, /quit to exit\n";

    // -------------------------
    // Message sending loop
    // -------------------------
    for (std::string line; std::getline(std::cin, line);)
    {
        if (line == "/quit")
            break;

        client->send_json_message("chat.message",
                                  {
                                      "user",
                                      user,
                                      "text",
                                      line,
                                  });
    }

    client->close();
    return 0;
}
