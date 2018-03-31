cmake_minimum_required(VERSION 3.10)
project(pony_intellisense_cli)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#include_directories(src /home/j.jensch/ponyc/src /home/j.jensch/ponyc/src/common /home/j.jensch/ponyc/src/libponyc /home/j.jensch/pony/src/libponyrt)
include_directories(include lib lib/pony lib/ponyc/src lib/ponyc/src/libponyc lib/ponyc/src/libponyrt lib/ponyc/src/common)

link_directories(lib/pony)
link_libraries(pthread stdc++fs ponyc ponyrt dl atomic LLVM-4.0 m blake2)

include(FindProtobuf)
find_package(Protobuf REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIRS})

set(PROTOBUF_SRC_ROOT_FOLDER ${CMAKE_SOURCE_DIR}/protocols)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS protocols/scope.proto)

add_executable(pony_intellisense_cli
        ${PROTO_SRCS}
        src/main.cpp
        src/pos.cpp
        src/dump_ast.cpp
        src/dump_scope.cpp
        src/ast_transformations.cpp
        src/get_symbol.cpp
        src/PonyType.cpp
        src/ExpressionTypeResolver.cpp
        )

target_link_libraries(pony_intellisense_cli ${PROTOBUF_LIBRARIES})
