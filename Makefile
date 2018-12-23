CMAKEFLAGS=

all: cmakebuild

cmakebuild:
	if test ! -s build/Makefile; then  mkdir -p build ; cd build ; cmake .. $(CMAKEFLAGS); fi
	cd build && $(MAKE) $(MFLAGS)
	cd gui && qmake-qt4

win32:
	if test ! -s wbuild/Makefile; then  mkdir -p wbuild ; cd wbuild ;  mingw32-cmake .. -DMCPE_VIZ_WIN32=ON $(CMAKEFLAGS); fi
	cd wbuild && $(MAKE) $(MFLAGS)

win32-debug:
	if test ! -s wbuild/Makefile; then  mkdir -p wbuild ; cd wbuild ;  mingw32-cmake .. -DMCPE_VIZ_WIN32=ON -DMCPE_VIZ_DEBUG=ON $(CMAKEFLAGS); fi
	cd wbuild && $(MAKE) $(MFLAGS)

win64:
	if test ! -s wbuild64/Makefile; then  mkdir -p wbuild64 ; cd wbuild64 ;  mingw64-cmake .. -DMCPE_VIZ_WIN64=ON $(CMAKEFLAGS); fi
	cd wbuild64 && $(MAKE) $(MFLAGS)

win64-debug:
	if test ! -s wbuild64/Makefile; then  mkdir -p wbuild64 ; cd wbuild64 ;  mingw64-cmake .. -DMCPE_VIZ_WIN64=ON -DMCPE_VIZ_DEBUG=ON $(CMAKEFLAGS); fi
	cd wbuild64 && $(MAKE) $(MFLAGS)
