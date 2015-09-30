CMAKEFLAGS=

all: cmakebuild

cmakebuild: 
	if test ! -s build/Makefile; then  mkdir -p build ; cd build ; cmake .. $(CMAKEFLAGS); fi
	cd build && $(MAKE) $(MFLAGS)

win32:
	if test ! -s wbuild/Makefile; then  mkdir -p wbuild ; cd wbuild ;  mingw32-cmake .. -DMCPE_VIZ_WIN32=ON $(CMAKEFLAGS); fi
	cd wbuild && $(MAKE) $(MFLAGS)
