call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\Common7\Tools\VsDevCmd.bat"

mkdir zlib
mkdir zlib\Win32
mkdir zlib\x64
mkdir zlib\include
cd zlib-1.2.11

mkdir build32
cd build32
cmake -A Win32 -DCMAKE_INSTALL_PREFIX=install ..
move ..\zconf.h.included ..\zconf.h
msbuild INSTALL.vcxproj -m -t:Rebuild -p:Configuration=Release -p:Platform=Win32
copy install\include\* ..\..\zlib\include
copy install\bin\* ..\..\zlib\Win32
copy install\lib\* ..\..\zlib\Win32
cd ..

mkdir build64
cd build64
cmake -A x64 -DCMAKE_INSTALL_PREFIX=install ..
move ..\zconf.h.included ..\zconf.h
msbuild INSTALL.vcxproj -m -t:Rebuild -p:Configuration=Release -p:Platform=x64
copy install\bin\* ..\..\zlib\x64
copy install\lib\* ..\..\zlib\x64
cd ..

cd ..

mkdir libpng
mkdir libpng\Win32
mkdir libpng\x64
mkdir libpng\include
cd lpng1637

mkdir build32
cd build32
cmake -A Win32 -DZLIB_LIBRARY=..\..\zlib\Win32\zlib.lib -DZLIB_INCLUDE_DIR=..\..\zlib\include -DCMAKE_INSTALL_PREFIX=install ..
msbuild INSTALL.vcxproj -m -t:Rebuild -p:Configuration=Release -p:Platform=Win32
copy install\include\* ..\..\libpng\include
copy install\bin\* ..\..\libpng\Win32
copy install\lib\* ..\..\libpng\Win32
cd ..

mkdir build64
cd build64
cmake -A x64 -DZLIB_LIBRARY=..\..\zlib\x64\zlib.lib -DZLIB_INCLUDE_DIR=..\..\zlib\include -DCMAKE_INSTALL_PREFIX=install ..
msbuild INSTALL.vcxproj -m -t:Rebuild -p:Configuration=Release -p:Platform=x64
copy install\bin\* ..\..\libpng\x64
copy install\lib\* ..\..\libpng\x64
cd ..

cd ..

mkdir libxml2
mkdir libxml2\Win32
mkdir libxml2\x64
mkdir libxml2\include
cd libxml2-2.9.10
copy include\libxml\xmlversion.h xmlversion.h.orig

cd win32
cscript configure.js compiler=msvc prefix=install iconv=no

setlocal
call "c:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars32.bat"
nmake -f Makefile install
move install\include\libxml2\libxml ..\..\libxml2\include
copy install\bin\libxml2.dll ..\..\libxml2\Win32
copy install\lib\libxml2.lib ..\..\libxml2\Win32
rmdir /s /q install
nmake -f Makefile clean
endlocal

setlocal
call "c:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"
nmake -f Makefile install
copy install\bin\libxml2.dll ..\..\libxml2\x64
copy install\lib\libxml2.lib ..\..\libxml2\x64
rmdir /s /q install
nmake -f Makefile distclean
endlocal

cd ..\..

cd libnbtplusplus

mkdir build32
cd build32
cmake -A Win32 -DZLIB_LIBRARY=..\..\zlib\Win32\zlib.lib -DZLIB_INCLUDE_DIR=..\..\zlib\include -DCMAKE_INSTALL_PREFIX=install -DNBT_BUILD_TESTS=OFF ..
msbuild libnbt++.sln -m -t:Rebuild -p:Configuration=Release -p:Platform=Win32
cd ..

mkdir build64
cd build64
cmake -A x64 -DZLIB_LIBRARY=..\..\zlib\x64\zlib.lib -DZLIB_INCLUDE_DIR=..\..\zlib\include -DCMAKE_INSTALL_PREFIX=install -DNBT_BUILD_TESTS=OFF ..
msbuild libnbt++.sln -m -t:Rebuild -p:Configuration=Release -p:Platform=x64
copy nbt_export.h ..\include
cd ..

cd ..

cd leveldb-mcpe

msbuild leveldb.sln -m -t:Rebuild -p:Configuration=Release -p:Platform=x86
mkdir Win32
move Release Win32

msbuild leveldb.sln -m -t:Rebuild -p:Configuration=Release -p:Platform=x64

cd ..

mkdir build32
cd build32
cmake -A Win32 -DMCPE_VIZ_WIN32=ON -DZLIB_LIBRARY=..\zlib\Win32\zlib.lib -DZLIB_INCLUDE_DIR=..\zlib\include -DPNG_LIBRARY=..\libpng\Win32\libpng16.lib -DPNG_PNG_INCLUDE_DIR=..\libpng\include -DLIBXML2_LIBRARY=..\libxml2\Win32\libxml2.lib -DLIBXML2_INCLUDE_DIR=..\libxml2\include ..
msbuild mcpe_viz.sln -m -t:Rebuild -p:Configuration=Release -p:Platform=Win32
copy ..\zlib\Win32\zlib.dll Release
copy ..\libpng\Win32\libpng16.dll Release
copy ..\libxml2\Win32\libxml2.dll Release
copy ..\leveldb-mcpe\Win32\Release\leveldb.dll Release
cd ..

mkdir build64
cd build64
cmake -A x64 -DMCPE_VIZ_WIN64=ON -DZLIB_LIBRARY=..\zlib\x64\zlib.lib -DZLIB_INCLUDE_DIR=..\zlib\include -DPNG_LIBRARY=..\libpng\x64\libpng16.lib -DPNG_PNG_INCLUDE_DIR=..\libpng\include -DLIBXML2_LIBRARY=..\libxml2\x64\libxml2.lib -DLIBXML2_INCLUDE_DIR=..\libxml2\include ..
msbuild mcpe_viz.sln -m -t:Rebuild -p:Configuration=Release -p:Platform=x64
copy ..\zlib\x64\zlib.dll Release
copy ..\libpng\x64\libpng16.dll Release
copy ..\libxml2\x64\libxml2.dll Release
copy ..\leveldb-mcpe\x64\Release\leveldb.dll Release
cd ..

mkdir mcpe_viz_helper.win32
copy mcpe_viz_helper_win_builds\Win32\* mcpe_viz_helper.win32
copy build32\Release\leveldb.dll mcpe_viz_helper.win32
copy build32\Release\libpng16.dll mcpe_viz_helper.win32
copy build32\Release\libxml2.dll mcpe_viz_helper.win32
copy build32\Release\mcpe_viz.exe mcpe_viz_helper.win32
copy build32\Release\zlib.dll mcpe_viz_helper.win32
xcopy /i images mcpe_viz_helper.win32\images
xcopy /i js mcpe_viz_helper.win32\js
copy mcpe_viz.cfg mcpe_viz_helper.win32
copy mcpe_viz.css mcpe_viz_helper.win32
copy mcpe_viz.js mcpe_viz_helper.win32
copy mcpe_viz.xml mcpe_viz_helper.win32
copy mcpe_viz.html.template mcpe_viz_helper.win32
copy LICENSE mcpe_viz_helper.win32\LICENSE.txt
copy README.md mcpe_viz_helper.win32\README.txt

mkdir mcpe_viz_helper.win64
copy mcpe_viz_helper_win_builds\x64\* mcpe_viz_helper.win64
copy build64\Release\leveldb.dll mcpe_viz_helper.win64
copy build64\Release\libpng16.dll mcpe_viz_helper.win64
copy build64\Release\libxml2.dll mcpe_viz_helper.win64
copy build64\Release\mcpe_viz.exe mcpe_viz_helper.win64
copy build64\Release\zlib.dll mcpe_viz_helper.win64
xcopy /i images mcpe_viz_helper.win64\images
xcopy /i js mcpe_viz_helper.win64\js
copy mcpe_viz.cfg mcpe_viz_helper.win64
copy mcpe_viz.css mcpe_viz_helper.win64
copy mcpe_viz.js mcpe_viz_helper.win64
copy mcpe_viz.xml mcpe_viz_helper.win64
copy mcpe_viz.html.template mcpe_viz_helper.win64
copy LICENSE mcpe_viz_helper.win64\LICENSE.txt
copy README.md mcpe_viz_helper.win64\README.txt
