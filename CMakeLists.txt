cmake_minimum_required(VERSION 3.27)
project(WaibeiluServer)

set(CMAKE_CXX_STANDARD 20)

find_package(boost_asio CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(WaibeiluServer main.cpp
       chat_message.h
)

target_link_libraries(WaibeiluServer
        PRIVATE
        Boost::asio
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        ws2_32
        mswsock)
