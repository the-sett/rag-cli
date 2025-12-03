# fetch_esbuild.cmake
# Downloads esbuild binary for Linux without requiring npm

# Determine architecture
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(ESBUILD_ARCH "x64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64")
    set(ESBUILD_ARCH "arm64")
else()
    message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

set(ESBUILD_PLATFORM "linux-${ESBUILD_ARCH}")
set(ESBUILD_DIR "${CMAKE_BINARY_DIR}/_deps/esbuild")
set(ESBUILD_EXECUTABLE "${ESBUILD_DIR}/bin/esbuild")

# Only download if not already present
if(NOT EXISTS "${ESBUILD_EXECUTABLE}")
    message(STATUS "Fetching latest esbuild version...")

    # First, get the latest version from npm registry
    set(ESBUILD_METADATA "${CMAKE_BINARY_DIR}/_deps/esbuild_metadata.json")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/_deps")
    file(DOWNLOAD
        "https://registry.npmjs.org/@esbuild/${ESBUILD_PLATFORM}/latest"
        "${ESBUILD_METADATA}"
        STATUS METADATA_STATUS
    )

    list(GET METADATA_STATUS 0 METADATA_CODE)
    if(NOT METADATA_CODE EQUAL 0)
        message(FATAL_ERROR "Failed to fetch esbuild metadata")
    endif()

    # Parse version from JSON (simple regex extraction)
    file(READ "${ESBUILD_METADATA}" METADATA_CONTENT)
    string(REGEX MATCH "\"version\":\"([0-9]+\\.[0-9]+\\.[0-9]+)\"" VERSION_MATCH "${METADATA_CONTENT}")
    set(ESBUILD_VERSION "${CMAKE_MATCH_1}")

    if(NOT ESBUILD_VERSION)
        message(FATAL_ERROR "Could not parse esbuild version from metadata")
    endif()

    message(STATUS "Latest esbuild version: ${ESBUILD_VERSION}")
    message(STATUS "Downloading esbuild for ${ESBUILD_PLATFORM}...")

    # Download URL from npm registry
    set(ESBUILD_URL "https://registry.npmjs.org/@esbuild/${ESBUILD_PLATFORM}/-/${ESBUILD_PLATFORM}-${ESBUILD_VERSION}.tgz")
    set(ESBUILD_TARBALL "${CMAKE_BINARY_DIR}/_deps/esbuild.tgz")

    # Create directory
    file(MAKE_DIRECTORY "${ESBUILD_DIR}")

    # Download the tarball
    file(DOWNLOAD
        "${ESBUILD_URL}"
        "${ESBUILD_TARBALL}"
        STATUS DOWNLOAD_STATUS
        SHOW_PROGRESS
    )

    list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
    if(NOT STATUS_CODE EQUAL 0)
        list(GET DOWNLOAD_STATUS 1 ERROR_MESSAGE)
        message(FATAL_ERROR "Failed to download esbuild: ${ERROR_MESSAGE}")
    endif()

    # Extract the tarball
    message(STATUS "Extracting esbuild...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf "${ESBUILD_TARBALL}"
        WORKING_DIRECTORY "${ESBUILD_DIR}"
        RESULT_VARIABLE EXTRACT_RESULT
    )

    if(NOT EXTRACT_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to extract esbuild tarball")
    endif()

    # Move files from package/ subdirectory to esbuild dir
    file(RENAME "${ESBUILD_DIR}/package/bin" "${ESBUILD_DIR}/bin")
    file(REMOVE_RECURSE "${ESBUILD_DIR}/package")
    file(REMOVE "${ESBUILD_TARBALL}")

    # Make executable
    file(CHMOD "${ESBUILD_EXECUTABLE}" PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
    )

    message(STATUS "esbuild downloaded to: ${ESBUILD_EXECUTABLE}")
else()
    message(STATUS "Using cached esbuild: ${ESBUILD_EXECUTABLE}")
endif()

# Verify esbuild works
execute_process(
    COMMAND "${ESBUILD_EXECUTABLE}" --version
    OUTPUT_VARIABLE ESBUILD_VERSION_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE ESBUILD_VERSION_RESULT
)

if(ESBUILD_VERSION_RESULT EQUAL 0)
    message(STATUS "esbuild version: ${ESBUILD_VERSION_OUTPUT}")
else()
    message(FATAL_ERROR "esbuild verification failed")
endif()
