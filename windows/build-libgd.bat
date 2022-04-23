@echo off
set GD_URL=https://github.com/libgd/libgd
set GD_COMMIT=7efcf4f935d0466b6ada44bed62d729cd2cddbc4
set ZLIB_URL=https://github.com/madler/zlib
set ZLIB_COMMIT=21767c654d31d2dccdde4330529775c6c5fd5389
set LIBPNG_URL=https://github.com/glennrp/libpng
set LIBPNG_COMMIT=a37d4836519517bdce6cb9d956092321eca3e73b
set LIBJPEG_URL=https://github.com/libjpeg-turbo/libjpeg-turbo
set LIBJPEG_COMMIT=d0e7c4548a908166b6c4daba549ee86cabe3efba
set FREETYPE_URL=https://github.com/freetype/freetype
set FREETYPE_COMMIT=079a22da037835daf5be2bd9eccf7bc1eaa2e783

set PATH=%PATH%;"C:\Program Files\7-Zip"

call :checkprog curl
call :checkprog msbuild
call :checkprog cmake
call :checkprog patch
rem call :checkprog nasm
rem call :checkprog 7z

set OUTDIR=..\gd2

set ARCH=%Platform%
if "%ARCH%"=="x86" set ARCH=Win32

set DEPS_DIR=..\deps

rmdir /S /Q tmp
mkdir tmp
cd tmp

echo if (MSVC) >cmake.txt
echo  set (CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT") >>cmake.txt
echo  set (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MTd") >>cmake.txt
echo endif() >>cmake.txt

mkdir %DEPS_DIR%
mkdir %DEPS_DIR%\lib
mkdir %DEPS_DIR%\include

call :download libgd %GD_URL% %GD_COMMIT%
call :download zlib %ZLIB_URL% %ZLIB_COMMIT%
call :download libpng %LIBPNG_URL% %LIBPNG_COMMIT%
call :download libjpeg-turbo %LIBJPEG_URL% %LIBJPEG_COMMIT%
call :download freetype %FREETYPE_URL% %FREETYPE_COMMIT%

call :build_zlib
call :build_libpng
call :build_libjpeg
call :build_freetype
call :build_libgd

rmdir /S /Q %DEPS_DIR%
cd ..
rmdir /S /Q tmp
echo Done

goto :eof

:download
echo Downloading %2/archive/%3
curl -L -o %3.zip %2/archive/%3.zip
if errorlevel 1 (
 echo Failed to download %1
 exit 1
)
7z x -y %3.zip
if errorlevel 1 (
 echo Failed to extract %1
 exit 1
)
rename %1-%3 %1
del %3.zip
exit /B

:patch_error
echo Failed to patch %1
exit 1

:build_error
echo Failed to build %1
exit 1

:build_zlib
cd zlib
type ..\cmake.txt >>CMakeLists.txt
cmake -A %ARCH% -B ..\zlib-build
if errorlevel 1 call :build_error zlib
cd ..
msbuild zlib-build\zlibstatic.vcxproj /p:Configuration=Release /p:Platform=%ARCH%
if errorlevel 1 call :build_error zlib
copy /y zlib-build\Release\zlibstatic.lib %DEPS_DIR%\lib\zlib_a.lib
copy /y zlib-build\zconf.h %DEPS_DIR%\include
copy /y zlib\zlib.h %DEPS_DIR%\include
copy /y zlib\zutil.h %DEPS_DIR%\include
exit /B

:build_libpng
cd libpng
type ..\cmake.txt >>CMakeLists.txt
cmake -A %ARCH% -B ..\libpng-build ^
 -DZLIB_LIBRARY_RELEASE=..\%DEPS_DIR%\zlib_a.lib ^
 -DZLIB_LIBRARY_DEBUG=..\%DEPS_DIR%\zlib_a.lib ^
 -DZLIB_INCLUDE_DIR=..\%DEPS_DIR%\include
if errorlevel 1 call :build_error libpng
cd ..
msbuild libpng-build\png_static.vcxproj /p:Configuration=Release /p:Platform=%ARCH%
if errorlevel 1 call :build_error libpng
copy /y libpng-build\Release\libpng16_static.lib %DEPS_DIR%\lib\libpng_a.lib
mkdir %DEPS_DIR%\include\libpng16
copy /y libpng\png.h %DEPS_DIR%\include\libpng16
copy /y libpng\pngconf.h %DEPS_DIR%\include\libpng16
copy /y libpng-build\pnglibconf.h %DEPS_DIR%\include\libpng16
exit /B

:build_libjpeg
cd libjpeg-turbo
type ..\cmake.txt >>CMakeLists.txt
cmake -A %ARCH% -B ..\libjpeg-build
if errorlevel 1 call :build_error libjpeg
cd ..
msbuild libjpeg-build\jpeg-static.vcxproj /p:Configuration=Release /p:Platform=%ARCH%
if errorlevel 1 call :build_error libjpeg
copy /y libjpeg-build\Release\jpeg-static.lib %DEPS_DIR%\lib\libjpeg_a.lib
copy /y libjpeg-build\jconfig.h %DEPS_DIR%\include
copy /y libjpeg-build\jversion.h %DEPS_DIR%\include
copy /y libjpeg-turbo\jerror.h %DEPS_DIR%\include
copy /y libjpeg-turbo\jmorecfg.h %DEPS_DIR%\include
copy /y libjpeg-turbo\jpeglib.h %DEPS_DIR%\include
exit /B

:build_freetype
cd freetype
msbuild builds\windows\vc2010\freetype.vcxproj /p:Configuration="Release Static" /p:Platform=%ARCH%
if errorlevel 1 call :build_error freetype
copy /y "objs\%ARCH%\Release Static\freetype.lib" ..\%DEPS_DIR%\lib\freetype_a.lib
mkdir ..\%DEPS_DIR%\include\freetype2 
xcopy /Y /R /E include\* ..\%DEPS_DIR%\include\freetype2 
rmdir /S /Q ..\%DEPS_DIR%\include\freetype2\freetype\internal
cd ..
exit /B

:build_libgd
cd libgd
cd windows
unix2dos ..\..\..\libgd-win-makefile.patch
patch -i ..\..\..\libgd-win-makefile.patch
cd ..
if errorlevel 1 call :patch_error libgd
nmake /f windows/Makefile.vc WITH_DEVEL=..\%DEPS_DIR%
if errorlevel 1 call :build_error libgd
cd ..
mkdir ..\%OUTDIR%\%ARCH%
copy /Y gdbuild\libgd_a.lib ..\%OUTDIR%\%ARCH%
copy /Y %DEPS_DIR%\lib\*.lib ..\%OUTDIR%\%ARCH%
mkdir ..\%OUTDIR%\include
copy /Y libgd\src\*.h ..\%OUTDIR%\include
exit /B

:checkprog
where /Q %1
if errorlevel 1 (
 echo A required program is missing: %1.exe
 exit 1
)
exit /B
