add_executable(vix_ws_server
    src/main.cpp
)

target_link_libraries(vix_ws_server
    PRIVATE
        vix::websocket
)

add_executable(vix_ws_client
    src/ws_client.cpp
)

target_link_libraries(vix_ws_client
    PRIVATE
        vix::websocket
)
