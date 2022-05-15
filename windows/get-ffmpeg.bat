@echo off
set PKG32=ffmpeg-n5.0-latest-win32-gpl-shared-5.0
set PKG64=ffmpeg-n5.0-latest-win64-gpl-shared-5.0
set URL32=https://github.com/sudo-nautilus/FFmpeg-Builds-Win32/releases/download/latest
set URL64=https://github.com/BtbN/FFmpeg-Builds/releases/download/latest
set OUTDIR=..\ffmpeg\
set PATH=%PATH%;"C:\Program Files\7-Zip"

call :checkprog curl

call :download %PKG32% %URL32% 
call :download %PKG64% %URL64% 

call :extract %PKG32% win32
call :extract %PKG64% x64

echo Done
goto :eof

:download
echo Downloading %1.zip
curl -L -o %1.zip %2/%1.zip
if errorlevel 1 (
 echo Failed to download %1.zip
 exit 1
)
exit /B

:extract
mkdir %OUTDIR%%2\dll
mkdir %OUTDIR%%2\lib
7z x -y %1.zip
move /Y %1\bin\*.dll %OUTDIR%%2\dll
rmdir /S /Q %1\lib\pkgconfig
move /Y %1\lib\*.* %OUTDIR%%2\lib
rmdir /S /Q %OUTDIR%include
move /Y %1\include %OUTDIR%
rmdir /S /Q %1
exit /B

:checkprog
where /Q %1
if errorlevel 1 (
 echo A required program is missing: %1.exe
 exit 1
)
exit /B
