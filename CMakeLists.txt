cmake_minimum_required(VERSION 3.0)
project(mcpe_viz
  VERSION 0.0.3)

option(MCPE_VIZ_WIN32 "Build win32 using mingw" OFF)
option(MCPE_VIZ_WIN64 "Build win64 using mingw" OFF)
option(MCPE_VIZ_DEBUG "Build debug version" OFF)
option(MCPE_VIZ_PROFILE "Build profile version" OFF)

if(MCPE_VIZ_WIN32)

  # win32 build
  if(MCPE_VIZ_DEBUG)
    add_definitions(-g -Wall -std=c++11 -DDLLX= -std=c++0x -fno-builtin-memcmp -pthread -DOS_WIN -DOS_WINDOWS -DWIN32 -DLEVELDB_PLATFORM_POSIX -DLEVELDB_ATOMIC_PRESENT -DNDEBUG -DWIN32_WINNT=0x0600 -D_WIN32_WINDOWS=0x0600 -DWINVER=0x0600)
    set(CMAKE_BUILD_TYPE Debug)
  else()
    add_definitions(-Wall -std=c++11 -DDLLX= -std=c++0x -fno-builtin-memcmp -pthread -DOS_WIN -DOS_WINDOWS -DWIN32 -DLEVELDB_PLATFORM_POSIX -DLEVELDB_ATOMIC_PRESENT -O2 -DNDEBUG -DWIN32_WINNT=0x0600 -D_WIN32_WINDOWS=0x0600 -DWINVER=0x0600)
  endif()
  
elseif(MCPE_VIZ_WIN64)

  # win64 build
  if(MCPE_VIZ_DEBUG)
    add_definitions(-g -Wall -std=c++11 -DDLLX= -std=c++0x -fno-builtin-memcmp -pthread -DOS_WIN -DOS_WINDOWS -DWIN64 -DLEVELDB_PLATFORM_POSIX -DLEVELDB_ATOMIC_PRESENT -DNDEBUG -DWIN64_WINNT=0x0600 -D_WIN64_WINDOWS=0x0600 -DWINVER=0x0600)
    set(CMAKE_BUILD_TYPE Debug)
  else()
    add_definitions(-Wall -std=c++11 -DDLLX= -std=c++0x -fno-builtin-memcmp -pthread -DOS_WIN -DOS_WINDOWS -DWIN64 -DLEVELDB_PLATFORM_POSIX -DLEVELDB_ATOMIC_PRESENT -O2 -DNDEBUG -DWIN64_WINNT=0x0600 -D_WIN64_WINDOWS=0x0600 -DWINVER=0x0600)
  endif()
  
else()
  if(MCPE_VIZ_DEBUG)
    
    # debug/valgrind
    add_definitions(-Wall -O0 -std=c++11 -DDLLX= -std=c++0x -fno-builtin-memcmp -pthread -DOS_LINUX -DLEVELDB_PLATFORM_POSIX -DLEVELDB_ATOMIC_PRESENT -DNDEBUG)
    set(CMAKE_BUILD_TYPE Debug)
    
  elseif(MCPE_VIZ_PROFILE)
    
    # gprof profiling
    add_definitions(-Wall -pg -O0 -std=c++11 -DDLLX= -std=c++0x -fno-builtin-memcmp -pthread -DOS_LINUX -DLEVELDB_PLATFORM_POSIX -DLEVELDB_ATOMIC_PRESENT -DNDEBUG)
    set(CMAKE_BUILD_TYPE Debug)
    # set(CMAKE_EXE_LINKER_FLAGS -pg)
    
  else()
    
    # default linux build
    add_definitions(-Wall -std=c++11 -DDLLX= -std=c++0x -fno-builtin-memcmp -pthread -DOS_LINUX -DLEVELDB_PLATFORM_POSIX -DLEVELDB_ATOMIC_PRESENT -O2 -DNDEBUG)
    
  endif()
endif()

add_executable(mcpe_viz mcpe_viz.cc)

find_package(PNG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)
find_package(LibXml2 REQUIRED)

# todo - threads?
include_directories(${PNG_INCLUDE_DIR} ${ZLIB_INCLUDE_DIR} ${LIBXML2_INCLUDE_DIR} leveldb-mcpe/include libnbtplusplus/include)

if(MCPE_VIZ_WIN32)
  include_directories(libnbtplusplus/wbuild .)
  target_link_libraries(mcpe_viz
    ${PROJECT_SOURCE_DIR}/leveldb-mcpe/wbuild/libleveldb.a
    ${PROJECT_SOURCE_DIR}/libnbtplusplus/wbuild/libnbt++.a
    ${PNG_LIBRARIES} ${ZLIB_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${LIBXML2_LIBRARIES})
  set_target_properties(mcpe_viz PROPERTIES LINK_SEARCH_START_STATIC 1)
  set_target_properties(mcpe_viz PROPERTIES LINK_SEARCH_END_STATIC 1)
  set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
elseif(MCPE_VIZ_WIN64)
  include_directories(libnbtplusplus/wbuild64 .)
  target_link_libraries(mcpe_viz
    ${PROJECT_SOURCE_DIR}/leveldb-mcpe/wbuild64/libleveldb.a
    ${PROJECT_SOURCE_DIR}/libnbtplusplus/wbuild64/libnbt++.a
    ${PNG_LIBRARIES} ${ZLIB_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${LIBXML2_LIBRARIES})
  set_target_properties(mcpe_viz PROPERTIES LINK_SEARCH_START_STATIC 1)
  set_target_properties(mcpe_viz PROPERTIES LINK_SEARCH_END_STATIC 1)
  set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
else()
  include_directories(libnbtplusplus/build .)
  target_link_libraries(mcpe_viz
    ${PROJECT_SOURCE_DIR}/leveldb-mcpe/libleveldb.a
    ${PROJECT_SOURCE_DIR}/libnbtplusplus/build/libnbt++.a
    ${PNG_LIBRARIES} ${ZLIB_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${LIBXML2_LIBRARIES})
endif()

# copy the xml file - kind of ugly
configure_file(${PROJECT_SOURCE_DIR}/mcpe_viz.xml mcpe_viz.xml COPYONLY)
configure_file(${PROJECT_SOURCE_DIR}/mcpe_viz.cfg mcpe_viz.cfg COPYONLY)

if(MCPE_VIZ_WIN32)
  configure_file(${PROJECT_SOURCE_DIR}/README.md README.txt NEWLINE_STYLE WIN32)
  configure_file(${PROJECT_SOURCE_DIR}/LICENSE LICENSE.txt NEWLINE_STYLE WIN32)
  # todo - would be cool to auto-gather dll's - but this doesn't work yet
  #add_custom_target(remove_dll ALL COMMAND
  #  ${CMAKE_COMMAND} -E rm -f "./*.dll"
  #  DEPENDS mcpe_viz.exe
  #  )
  #add_custom_target(gather_dll ALL COMMAND
  #  ${CMAKE_COMMAND} -E ../Research/mingw/mingw-bundledlls/mingw-bundledlls --copy mcpe_viz.exe
  #  DEPENDS remove_dll
  #  )
  add_custom_target(create_zip ALL COMMAND
    ${CMAKE_COMMAND} -E tar "cfv" "../mcpe_viz.win32.zip" --format=zip
    "./mcpe_viz.exe"
    "./mcpe_viz.cfg"
    "./mcpe_viz.xml"
    "./*.dll"
    "./README.txt"
    "./LICENSE.txt"
    DEPENDS mcpe_viz.exe
    )
elseif(MCPE_VIZ_WIN64)
  # package win64 w/ required dll's
  # todo
  configure_file(${PROJECT_SOURCE_DIR}/README.md README.txt NEWLINE_STYLE WIN32)
  configure_file(${PROJECT_SOURCE_DIR}/LICENSE LICENSE.txt NEWLINE_STYLE WIN32)
  add_custom_target(create_zip ALL COMMAND
    ${CMAKE_COMMAND} -E tar "cfv" "../mcpe_viz.win64.zip" --format=zip
    "./mcpe_viz.exe"
    "./mcpe_viz.cfg"
    "./mcpe_viz.xml"
    "./*.dll"
    "./README.txt"
    "./LICENSE.txt"
    DEPENDS mcpe_viz.exe
    )
endif()
