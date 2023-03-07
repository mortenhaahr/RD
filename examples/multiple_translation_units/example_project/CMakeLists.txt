cmake_minimum_required(VERSION 3.13.4)

# Generate a CompilationDatabase (compile_commands.json file) for our build,
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_compile_options(-fno-rtti)

project(ExampleProject)

add_executable(example_project main.cpp Printer.cpp)