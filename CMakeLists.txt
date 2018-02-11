cmake_minimum_required(VERSION 3.10)
project(ponylang_llvm_tester)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#include_directories(src /home/j.jensch/ponyc/src /home/j.jensch/ponyc/src/common /home/j.jensch/ponyc/src/libponyc /home/j.jensch/pony/src/libponyrt)
include_directories(src lib lib/pony lib/ponyc/src lib/ponyc/src/libponyc lib/ponyc/src/libponyrt lib/ponyc/src/common)

link_directories(lib/pony)
link_libraries(pthread stdc++fs ponyc ponyrt dl atomic LLVM-4.0 m blake2)

add_executable(ponylang_llvm_tester
        src/main.cpp
        src/pos.cpp
        src/dump_ast.cpp src/dump_scope.cpp src/dump_scope.hpp src/ast_transformations.cpp src/ast_transformations.hpp src/ponyc_includes.hpp src/main.hpp)

#target_link_libraries(ponylang_llvm_tester pthread ponyc ponyrt dl atomic LLVM-4.0 m blake2)
