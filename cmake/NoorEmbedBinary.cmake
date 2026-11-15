# Converts a binary file into a C++ header with a byte array.
# Usage: cmake -DINPUT=<file> -DOUTPUT=<header> -DSYMBOL=<name> -P NoorEmbedBinary.cmake

file(READ "${INPUT}" hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
get_filename_component(input_name "${INPUT}" NAME)
file(WRITE "${OUTPUT}"
    "// Generated from ${input_name} by cmake/NoorEmbedBinary.cmake. Do not edit.\n"
    "#pragma once\n"
    "#include <cstdint>\n"
    "alignas(4) inline constexpr std::uint8_t ${SYMBOL}[] = {${bytes}};\n")
