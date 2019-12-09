cd zlib-1.2.11
rmdir /s /q build32
rmdir /s /q build64
cd ..
rmdir /s /q zlib

cd lpng1637
rmdir /s /q build32
rmdir /s /q build64
cd ..
rmdir /s /q libpng

cd libxml2-2.9.10
del config.h
del win32\rcVersion.h
move xmlversion.h.orig include\libxml\xmlversion.h
cd ..
rmdir /s /q libxml2

cd libnbtplusplus
rmdir /s /q build32
rmdir /s /q build64
del include\nbt_export.h
cd ..

cd leveldb-mcpe
rmdir /s /q Win32
rmdir /s /q x64
cd ..

rmdir /s /q build32
rmdir /s /q build64

del mybuildlog.txt
