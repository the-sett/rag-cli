# embed_zip.cmake - Creates a zip file and embeds it as a C header
# Usage: cmake -DWWW_DIR=<dir> -DOUTPUT_ZIP=<zip> -DOUTPUT_HEADER=<header> -P embed_zip.cmake

if(NOT DEFINED WWW_DIR)
    message(FATAL_ERROR "WWW_DIR not defined")
endif()

if(NOT DEFINED OUTPUT_ZIP)
    message(FATAL_ERROR "OUTPUT_ZIP not defined")
endif()

if(NOT DEFINED OUTPUT_HEADER)
    message(FATAL_ERROR "OUTPUT_HEADER not defined")
endif()

# Create the zip file
execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar cf ${OUTPUT_ZIP} --format=zip .
    WORKING_DIRECTORY ${WWW_DIR}
    RESULT_VARIABLE ZIP_RESULT
)

if(NOT ZIP_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to create zip file")
endif()

# Read the zip file as hex
file(READ ${OUTPUT_ZIP} ZIP_CONTENT HEX)
string(LENGTH "${ZIP_CONTENT}" ZIP_HEX_LEN)
math(EXPR ZIP_SIZE "${ZIP_HEX_LEN} / 2")

# Convert hex string to C array format
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " ZIP_ARRAY "${ZIP_CONTENT}")
# Remove trailing comma and space
string(REGEX REPLACE ", $" "" ZIP_ARRAY "${ZIP_ARRAY}")

# Add line breaks every 12 bytes for readability
string(REGEX REPLACE "(0x[0-9a-f][0-9a-f], ){12}" "\\0\n    " ZIP_ARRAY "${ZIP_ARRAY}")

# Generate the header file
file(WRITE ${OUTPUT_HEADER} "// Auto-generated file - do not edit
// Contains embedded www resources as a zip archive

#pragma once

#include <cstddef>
#include <cstdint>

namespace rag {
namespace embedded {

inline const uint8_t www_zip_data[] = {
    ${ZIP_ARRAY}
};

inline constexpr size_t www_zip_size = ${ZIP_SIZE};

} // namespace embedded
} // namespace rag
")

message(STATUS "Embedded ${ZIP_SIZE} bytes of www resources")
