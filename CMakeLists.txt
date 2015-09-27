cmake_minimum_required(VERSION 3.0)
project(mcpe_viz
  VERSION 0.0.1)

add_definitions(-Wall -std=c++11 -DDLLX= -std=c++0x -fno-builtin-memcmp -pthread -DOS_LINUX -DLEVELDB_PLATFORM_POSIX -DLEVELDB_ATOMIC_PRESENT -O2 -DNDEBUG)
include_directories(leveldb-mcpe/include .)

find_package(PNG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

add_executable(mcpe_viz mcpe_viz.cc)
target_link_libraries(mcpe_viz ${PROJECT_SOURCE_DIR}/leveldb-mcpe/libleveldb.a ${PNG_LIBRARIES} ${ZLIB_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
