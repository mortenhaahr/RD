
cmake_minimum_required(VERSION 3.13.4)
include(cmake/functions.cmake)

set(CMAKE_CXX_COMPILER clang++) # Must come before project line
message(STATUS "Using compiler ${CMAKE_CXX_COMPILER}")

# Generate a CompilationDatabase (compile_commands.json file) for our build,
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

# Stick to C++17 since LLVM and Clang libraries are built with
# See: https://stackoverflow.com/questions/67500470/are-there-hidden-dangers-to-link-libraries-compiled-with-different-c-standard
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Needed because Clang is compiled with this (per default)
add_compile_options(-fno-rtti)

project(CStyleArrayConverter)

 configure_clang_lib()

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

add_executable(converter c_style_array_converter_tool.cpp)

# Link against LLVM libraries
target_link_libraries(converter
        clangAST
        clangASTMatchers
        clangBasic
        clangFrontend
        clangSerialization
        clangTooling
        clangTransformer
    )