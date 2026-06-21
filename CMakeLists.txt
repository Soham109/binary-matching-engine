cmake_minimum_required(VERSION 3.14)
project(binary_matching_engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.18.3
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(httplib)

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(nlohmann_json)

add_library(engine STATIC src/order_book.cpp)
target_include_directories(engine PUBLIC include/engine)

add_executable(server src/server.cpp)
target_link_libraries(server PRIVATE engine httplib::httplib nlohmann_json::nlohmann_json)
add_executable(fuzz fuzz.cpp)
target_link_libraries(fuzz PRIVATE engine)