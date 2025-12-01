cmake_minimum_required(VERSION 3.20)
project(rag-cli VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find system dependencies
find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(CLI11 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(CMARK REQUIRED libcmark)

# Fetch cpp-httplib for HTTP server
include(FetchContent)
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.3
)
FetchContent_MakeAvailable(httplib)

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

    # Copy index.html to build directory
    add_custom_command(
        OUTPUT ${WWW_DIR}/index.html
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/www/index.html ${WWW_DIR}/index.html
        DEPENDS ${CMAKE_SOURCE_DIR}/www/index.html
        COMMENT "Copying index.html"
    )

    # Custom target for Elm build
    add_custom_target(elm_build ALL DEPENDS ${ELM_OUTPUT} ${WWW_DIR}/index.html)
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
)

add_executable(crag ${SOURCES})

# Pass www directory path as compile definition
target_compile_definitions(crag PRIVATE WWW_DIR="${CMAKE_BINARY_DIR}/www")

target_include_directories(crag PRIVATE src ${CMARK_INCLUDE_DIRS})
target_link_libraries(crag PRIVATE
    nlohmann_json::nlohmann_json
    CLI11::CLI11
    CURL::libcurl
    ${CMARK_LIBRARIES}
    httplib::httplib
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
    )

    add_library(crag_lib STATIC ${LIB_SOURCES})
    target_compile_definitions(crag_lib PUBLIC WWW_DIR="${CMAKE_BINARY_DIR}/www")
    target_include_directories(crag_lib PUBLIC src ${CMARK_INCLUDE_DIRS})
    target_link_libraries(crag_lib PUBLIC
        nlohmann_json::nlohmann_json
        CLI11::CLI11
        CURL::libcurl
        ${CMARK_LIBRARIES}
        httplib::httplib
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
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libcurl4 (>= 7.0), libcmark0.30.2 (>= 0.30.0) | libcmark-dev")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

# Generator selection
set(CPACK_GENERATOR "DEB")

include(CPack)
