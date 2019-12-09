cmake_minimum_required(VERSION 3.0)
project(mcpe_viz
  VERSION 0.0.48)

option(MCPE_VIZ_WIN32 "Build win32 using mingw" OFF)
option(MCPE_VIZ_WIN64 "Build win64 using mingw" OFF)
option(MCPE_VIZ_DEBUG "Build debug version" OFF)
option(MCPE_VIZ_PROFILE "Build profile version" OFF)

# todo - we used to have -std=c++0x in addition to -std=c++11 (leveldb lib compile seems to want that flag)
# old flags: -fno-builtin-memcmp -pthread

# cflags useful for all builds
add_definitions(-DDLLX=)

if(MCPE_VIZ_WIN32)

  # win32 build

  #add_definitions(-DWIN32 -D_WIN32_WINNT=0x0600 -D_WIN32_WINDOWS=0x0600 -DWINVER=0x0600)
  add_definitions(-DWIN32)
  
  if(MCPE_VIZ_DEBUG)
    message("Build: WIN32 DEBUG")
    add_definitions(-g -O0)
    set(CMAKE_BUILD_TYPE Debug)
  else()
    message("Build: WIN32")
    # todo - can -O2 work?
    # todo - after code split (2015.10.24) -O1 and -O also do not work (i.e. immediate crash)
    add_definitions(-O2 -DNDEBUG)
  endif()
  
elseif(MCPE_VIZ_WIN64)

  # win64 build

  #add_definitions(-DWIN64 -D_WIN32_WINNT=0x0600 -D_WIN32_WINDOWS=0x0600 -DWINVER=0x0600)
  add_definitions(-DWIN32 -DWIN64)

  if(MCPE_VIZ_DEBUG)
    message("Build: WIN64 DEBUG")
    add_definitions(-g -O0)
    set(CMAKE_BUILD_TYPE Debug)
  else()
    message("Build: WIN64")
    # todo - can -O2 work?
    # todo - after code split (2015.10.24) -O1 and -O also do not work (i.e. immediate crash)
    add_definitions(-O2 -DNDEBUG)
  endif()
  
else()

  # linux build
  
  if(MCPE_VIZ_DEBUG)
    
    # debug/valgrind
    message("Build: DEBUG")
    add_definitions(-g -O0)
    set(CMAKE_BUILD_TYPE Debug)
    
  elseif(MCPE_VIZ_PROFILE)
    
    # gprof profiling
    message("Build: PROFILE")
    add_definitions(-pg -O0)
    set(CMAKE_BUILD_TYPE Debug)
    set(CMAKE_EXE_LINKER_FLAGS -pg)
    
  else()
    
    # default linux build
    # note: played with "-falign-functions=16 -falign-loops=16 -flto" but no significant impact
    message("Build: REGULAR")
    add_definitions(-O2 -DNDEBUG)
    #set(CMAKE_EXE_LINKER_FLAGS -flto)
    
  endif()
endif()

add_executable(mcpe_viz mcpe_viz.cc mcpe_viz.xml.cc mcpe_viz.nbt.cc mcpe_viz.util.cc getopt.c)

find_package(PNG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)
find_package(LibXml2 REQUIRED)

include_directories(${PNG_INCLUDE_DIR} ${ZLIB_INCLUDE_DIR} ${LIBXML2_INCLUDE_DIR} ${PROJECT_SOURCE_DIR}/leveldb-mcpe/include ${PROJECT_SOURCE_DIR}/libnbtplusplus/include)

# setup include dirs and lib dirs for each build type
# set win32/win64 builds to use static libs when possible
if(MCPE_VIZ_WIN32)
  target_link_libraries(mcpe_viz
    ${PROJECT_SOURCE_DIR}/leveldb-mcpe/Win32/Release/leveldb.lib
    ${PROJECT_SOURCE_DIR}/libnbtplusplus/build32/Release/nbt++.lib
    ${PNG_LIBRARIES} ${ZLIB_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${LIBXML2_LIBRARIES})
  # 2016.01.30 - disabled
  #set_target_properties(mcpe_viz PROPERTIES LINK_SEARCH_START_STATIC 1)
  #set_target_properties(mcpe_viz PROPERTIES LINK_SEARCH_END_STATIC 1)
  #set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
elseif(MCPE_VIZ_WIN64)
  target_link_libraries(mcpe_viz
    ${PROJECT_SOURCE_DIR}/leveldb-mcpe/x64/Release/leveldb.lib
    ${PROJECT_SOURCE_DIR}/libnbtplusplus/build64/Release/nbt++.lib
    ${PNG_LIBRARIES} ${ZLIB_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${LIBXML2_LIBRARIES})
  # 2016.01.30 - disabled
  #set_target_properties(mcpe_viz PROPERTIES LINK_SEARCH_START_STATIC 1)
  #set_target_properties(mcpe_viz PROPERTIES LINK_SEARCH_END_STATIC 1)
  #set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
else()
  include_directories(libnbtplusplus/build .)
  target_link_libraries(mcpe_viz
    ${PROJECT_SOURCE_DIR}/leveldb-mcpe/out-static/libleveldb.a
    ${PROJECT_SOURCE_DIR}/libnbtplusplus/build/libnbt++.a
    ${PNG_LIBRARIES} ${ZLIB_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${LIBXML2_LIBRARIES})
endif()

# copy misc files - kind of ugly
configure_file(${PROJECT_SOURCE_DIR}/mcpe_viz.xml mcpe_viz.xml COPYONLY)
configure_file(${PROJECT_SOURCE_DIR}/mcpe_viz.cfg mcpe_viz.cfg COPYONLY)
configure_file(${PROJECT_SOURCE_DIR}/mcpe_viz.js mcpe_viz.js COPYONLY)
configure_file(${PROJECT_SOURCE_DIR}/mcpe_viz.css mcpe_viz.css COPYONLY)
configure_file(${PROJECT_SOURCE_DIR}/mcpe_viz.html.template mcpe_viz.html.template COPYONLY)

configure_file(${PROJECT_SOURCE_DIR}/js/bootstrap.min.css js/bootstrap.min.css COPYONLY)
configure_file(${PROJECT_SOURCE_DIR}/js/bootstrap-tour.min.css js/bootstrap-tour.min.css COPYONLY)
configure_file(${PROJECT_SOURCE_DIR}/js/ol.css js/ol.css COPYONLY)
configure_file(${PROJECT_SOURCE_DIR}/js/bootstrap.min.js js/bootstrap.min.js COPYONLY)
configure_file(${PROJECT_SOURCE_DIR}/js/bootstrap-tour.min.js js/bootstrap-tour.min.js COPYONLY)
configure_file(${PROJECT_SOURCE_DIR}/js/jquery-1.11.2.min.js js/jquery-1.11.2.min.js COPYONLY)
configure_file(${PROJECT_SOURCE_DIR}/js/ol.js js/ol.js COPYONLY)

# zip win32 and win64 builds
if(MCPE_VIZ_WIN32)

  # copy the gui exe and dll's
  # todo - this appears to not work?
  #file(GLOB GUI_EXE "${PROJECT_SOURCE_DIR}/gui/release/*.exe")
  #file(GLOB GUI_DLL "${PROJECT_SOURCE_DIR}/gui/release/*.dll")
  #file(COPY ${GUI_EXE} DESTINATION "${PROJECT_SOURCE_DIR}/wbuild/")
  #file(COPY ${GUI_DLL} DESTINATION "${PROJECT_SOURCE_DIR}/wbuild/")
  
  # todo - would be cool to auto-gather dll's - but this doesn't work yet
  #add_custom_target(remove_dll ALL COMMAND
  #  ${CMAKE_COMMAND} -E rm -f "./*.dll"
  #  DEPENDS mcpe_viz.exe
  #  )
  #add_custom_target(gather_dll ALL COMMAND
  #  ${CMAKE_COMMAND} -E ../Research/mingw/mingw-bundledlls/mingw-bundledlls --copy mcpe_viz.exe
  #  DEPENDS remove_dll
  #  )
  configure_file(${PROJECT_SOURCE_DIR}/README.md README.txt NEWLINE_STYLE WIN32)
  configure_file(${PROJECT_SOURCE_DIR}/LICENSE LICENSE.txt NEWLINE_STYLE WIN32)
  add_custom_target(create_zip
    COMMAND
    ${CMAKE_COMMAND} -E tar "cfv" "../mcpe_viz.win32.zip" --format=zip
    "mcpe_viz.*"
    "mcpe_viz_helper.exe"
    "*.dll"
    "README.txt"
    "LICENSE.txt"
    "js"
    "images"
    DEPENDS mcpe_viz.exe
    )
elseif(MCPE_VIZ_WIN64)

  # copy the gui exe and dll's
  # todo - this appears to not work?
  #file(GLOB GUI_EXE "${PROJECT_SOURCE_DIR}/gui/release/*.exe")
  #file(GLOB GUI_DLL "${PROJECT_SOURCE_DIR}/gui/release/*.dll")
  #file(COPY ${GUI_EXE} DESTINATION "${PROJECT_SOURCE_DIR}/wbuild64/")
  #file(COPY ${GUI_DLL} DESTINATION "${PROJECT_SOURCE_DIR}/wbuild64/")
  
  # package win64 w/ required dll's
  configure_file(${PROJECT_SOURCE_DIR}/README.md README.txt NEWLINE_STYLE WIN32)
  configure_file(${PROJECT_SOURCE_DIR}/LICENSE LICENSE.txt NEWLINE_STYLE WIN32)
  add_custom_target(create_zip
    COMMAND
    ${CMAKE_COMMAND} -E tar "cfv" "../mcpe_viz.win64.zip" --format=zip
    "mcpe_viz.*"
    "mcpe_viz_helper.exe"
    "*.dll"
    "README.txt"
    "LICENSE.txt"
    "js"
    "images"
    DEPENDS mcpe_viz.exe
    )
endif()
