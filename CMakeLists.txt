cmake_minimum_required(VERSION 3.0)
project(mcpe_viz
  VERSION 0.0.1)

# choose one of these:
# todo - use these based on "options"?
# regular
add_definitions(-Wall -std=c++11 -DDLLX= -std=c++0x -fno-builtin-memcmp -pthread -DOS_LINUX -DLEVELDB_PLATFORM_POSIX -DLEVELDB_ATOMIC_PRESENT -O2 -DNDEBUG)

# debug/valgrind
# add_definitions(-Wall -O0 -std=c++11 -DDLLX= -std=c++0x -fno-builtin-memcmp -pthread -DOS_LINUX -DLEVELDB_PLATFORM_POSIX -DLEVELDB_ATOMIC_PRESENT -DNDEBUG)
# set(CMAKE_BUILD_TYPE Debug)

# gprof profiling
# add_definitions(-Wall -pg -O0 -std=c++11 -DDLLX= -std=c++0x -fno-builtin-memcmp -pthread -DOS_LINUX -DLEVELDB_PLATFORM_POSIX -DLEVELDB_ATOMIC_PRESENT -DNDEBUG)
# set(CMAKE_BUILD_TYPE Debug)
# set(CMAKE_EXE_LINKER_FLAGS -pg)

# end choose

include_directories(leveldb-mcpe/include libnbtplusplus/include libnbtplusplus/build .)

find_package(PNG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

find_package(LibXml2 REQUIRED)
include_directories(${LIBXML2_INCLUDE_DIR})

add_executable(mcpe_viz mcpe_viz.cc)
target_link_libraries(mcpe_viz ${PROJECT_SOURCE_DIR}/leveldb-mcpe/libleveldb.a ${PROJECT_SOURCE_DIR}/libnbtplusplus/build/libnbt++.a ${PNG_LIBRARIES} ${ZLIB_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${LIBXML2_LIBRARIES})

# copy the xml file - kind of ugly
configure_file(${PROJECT_SOURCE_DIR}/mcpe_viz.xml mcpe_viz.xml COPYONLY)
configure_file(${PROJECT_SOURCE_DIR}/mcpe_viz.cfg mcpe_viz.cfg COPYONLY)

