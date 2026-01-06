cmake_minimum_required(VERSION 3.20)
project(rag-cli VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find system dependencies
find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(CLI11 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(CMARK REQUIRED libcmark)

# Fetch cpp-httplib for HTTP server (header-only, no install needed)
include(FetchContent)
set(HTTPLIB_INSTALL OFF CACHE BOOL "Disable httplib installation")
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.3
)
FetchContent_MakeAvailable(httplib)

# Fetch IXWebSocket for WebSocket server (static lib, no install needed)
set(USE_TLS OFF CACHE BOOL "Disable TLS for IXWebSocket")
set(USE_OPEN_SSL OFF CACHE BOOL "Disable OpenSSL for IXWebSocket")
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "Disable IXWebSocket installation")
FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG v11.4.5
)
FetchContent_MakeAvailable(ixwebsocket)

# Fetch esbuild for TypeScript compilation
include(cmake/fetch_esbuild.cmake)

# Elm build - compile Elm to JavaScript
find_program(ELM_EXECUTABLE elm)
if(ELM_EXECUTABLE)
    set(ELM_OUTPUT "${CMAKE_BINARY_DIR}/www/elm.js")
    set(ELM_SOURCE "${CMAKE_SOURCE_DIR}/src/elm/Main.elm")
    set(WWW_DIR "${CMAKE_BINARY_DIR}/www")

    # Create www directory
    file(MAKE_DIRECTORY ${WWW_DIR})

    # Custom command to compile Elm
    add_custom_command(
        OUTPUT ${ELM_OUTPUT}
        COMMAND ${ELM_EXECUTABLE} make ${ELM_SOURCE} --output=${ELM_OUTPUT} --optimize
        DEPENDS ${ELM_SOURCE}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Compiling Elm application"
    )

    # Custom command to compile TypeScript with esbuild
    set(TS_SOURCE "${CMAKE_SOURCE_DIR}/www/index.ts")
    set(TS_OUTPUT "${WWW_DIR}/bundle.js")
    add_custom_command(
        OUTPUT ${TS_OUTPUT}
        COMMAND ${ESBUILD_EXECUTABLE} ${TS_SOURCE}
            --bundle
            --outfile=${TS_OUTPUT}
            --target=es2020
            --format=iife
        DEPENDS ${TS_SOURCE}
        COMMENT "Compiling TypeScript with esbuild"
    )

    # Copy index.html to build directory
    add_custom_command(
        OUTPUT ${WWW_DIR}/index.html
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/www/index.html ${WWW_DIR}/index.html
        DEPENDS ${CMAKE_SOURCE_DIR}/www/index.html
        COMMENT "Copying index.html"
    )

    # Custom target for web build (Elm + TypeScript)
    add_custom_target(web_build ALL DEPENDS ${ELM_OUTPUT} ${TS_OUTPUT} ${WWW_DIR}/index.html)

    # Embed www resources as a zip file in C++ header
    set(WWW_ZIP "${CMAKE_BINARY_DIR}/www_resources.zip")
    set(WWW_HEADER "${CMAKE_BINARY_DIR}/generated/www_resources.hpp")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")

    add_custom_command(
        OUTPUT ${WWW_HEADER}
        COMMAND ${CMAKE_COMMAND}
            -DWWW_DIR=${WWW_DIR}
            -DOUTPUT_ZIP=${WWW_ZIP}
            -DOUTPUT_HEADER=${WWW_HEADER}
            -P ${CMAKE_SOURCE_DIR}/cmake/embed_zip.cmake
        DEPENDS ${ELM_OUTPUT} ${TS_OUTPUT} ${WWW_DIR}/index.html ${CMAKE_SOURCE_DIR}/cmake/embed_zip.cmake
        COMMENT "Embedding www resources"
    )

    add_custom_target(embed_www ALL DEPENDS ${WWW_HEADER})
    add_dependencies(embed_www web_build)
else()
    message(WARNING "Elm compiler not found. Web UI will not be built.")
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/console.cpp
    src/settings.cpp
    src/file_resolver.cpp
    src/openai_client.cpp
    src/vector_store.cpp
    src/chat.cpp
    src/markdown_renderer.cpp
    src/terminal.cpp
    src/input_editor.cpp
    src/http_server.cpp
    src/websocket_server.cpp
    src/mcp_server.cpp
    src/embedded_resources.cpp
    src/third_party/miniz.c
)

add_executable(crag ${SOURCES})

# Add dependency on embedded resources
if(ELM_EXECUTABLE)
    add_dependencies(crag embed_www)
endif()

# Include generated headers directory
target_include_directories(crag PRIVATE
    src
    ${CMAKE_BINARY_DIR}/generated
    ${CMARK_INCLUDE_DIRS}
)

target_link_libraries(crag PRIVATE
    nlohmann_json::nlohmann_json
    CLI11::CLI11
    CURL::libcurl
    ${CMARK_LIBRARIES}
    httplib::httplib
    ixwebsocket
)

# Install
install(TARGETS crag RUNTIME DESTINATION bin)

# Testing
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    find_package(Catch2 REQUIRED)
    enable_testing()

    # Library target for testing (excludes main.cpp)
    set(LIB_SOURCES
        src/console.cpp
        src/settings.cpp
        src/file_resolver.cpp
        src/openai_client.cpp
        src/vector_store.cpp
        src/chat.cpp
        src/markdown_renderer.cpp
        src/terminal.cpp
        src/input_editor.cpp
        src/http_server.cpp
        src/websocket_server.cpp
        src/mcp_server.cpp
        src/embedded_resources.cpp
        src/third_party/miniz.c
    )

    add_library(crag_lib STATIC ${LIB_SOURCES})

    if(ELM_EXECUTABLE)
        add_dependencies(crag_lib embed_www)
    endif()

    target_include_directories(crag_lib PUBLIC
        src
        ${CMAKE_BINARY_DIR}/generated
        ${CMARK_INCLUDE_DIRS}
    )
    target_link_libraries(crag_lib PUBLIC
        nlohmann_json::nlohmann_json
        CLI11::CLI11
        CURL::libcurl
        ${CMARK_LIBRARIES}
        httplib::httplib
        ixwebsocket
    )

    # Test executable
    add_executable(test_markdown_renderer tests/test_markdown_renderer.cpp)
    target_link_libraries(test_markdown_renderer PRIVATE
        crag_lib
        Catch2::Catch2
    )

    include(CTest)
    include(Catch)
    catch_discover_tests(test_markdown_renderer)
endif()

# CPack configuration for Debian package
set(CPACK_PACKAGE_NAME "crag")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "RAG CLI - Chat with your documents using OpenAI")
set(CPACK_PACKAGE_DESCRIPTION "A command-line tool that builds a searchable knowledge base from your documents, code, and data files, enabling interactive AI conversations with retrieval-augmented generation.")
set(CPACK_PACKAGE_CONTACT "Rupert Smith <rupertlssmith@gmail.com>")
set(CPACK_PACKAGE_MAINTAINER "Rupert Smith <rupertlssmith@gmail.com>")
set(CPACK_PACKAGE_VENDOR "The Sett")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/the-sett/rag-cli")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

# Debian specific settings
# Dependencies:
# - libcurl4: HTTP client for OpenAI API
# - libcmark0.30.2: Markdown parsing
# - libssl3: SSL/TLS for HTTPS (required by libcurl for OpenAI API)
# - libbrotli1: Brotli compression (required by cpp-httplib)
# - zlib1g: Compression (required by cpp-httplib)
# - ca-certificates: SSL certificates for HTTPS verification (critical for OpenAI API)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libcurl4 (>= 7.0), libcmark0.30.2 (>= 0.30.0) | libcmark-dev, libssl3, libbrotli1, zlib1g, ca-certificates")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

# Generator selection
set(CPACK_GENERATOR "DEB")

include(CPack)
