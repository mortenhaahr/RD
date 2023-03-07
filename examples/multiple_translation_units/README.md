# Transformation base project

Simple program that transforms functions named `MkX` to `MakeX`. <br>
How to use:
- Create build directory: `mkdir build && cd build`
- Choose one of the two below. <path-to-llvm-build> is the path to where you have compiled LLVM and Clang:
  - `cmake .. -G Ninja -DLLVM_BUILD=<path-to-llvm-build>`
  - `export LLVM_BUILD=<path-to-llvm-build> && cmake .. -G Ninja`
- Build project: `ninja`
- Copy the input file: `cp input_file_orig.cpp input_file.cpp`
- Run the tool: `./bin/transformer ../input_file.cpp --`
- Observe that the function names have been modified