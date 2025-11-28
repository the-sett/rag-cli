cmake_minimum_required(VERSION 3.20)
project(rag-cli VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find system dependencies
find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(CLI11 REQUIRED)

# Source files
set(SOURCES
    src/main.cpp
    src/console.cpp
    src/settings.cpp
    src/file_resolver.cpp
    src/openai_client.cpp
    src/vector_store.cpp
    src/chat.cpp
)

add_executable(rag-cli ${SOURCES})

target_include_directories(rag-cli PRIVATE src)
target_link_libraries(rag-cli PRIVATE
    nlohmann_json::nlohmann_json
    CLI11::CLI11
    CURL::libcurl
)

# Install
install(TARGETS rag-cli RUNTIME DESTINATION bin)
