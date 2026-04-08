@echo off
echo Setting up Visual Studio environment...

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

cd /d E:\final\star_align

echo Building with LibRaw support...
cl.exe /nologo /std:c++17 /EHsc /I libraw\include ^
    /c test_star_align.cpp /Fo:build\test_star_align.obj /O2 /arch:AVX2

if %ERRORLEVEL% NEQ 0 (
    echo Build failed
    pause
    exit /b 1
)

echo Linking with LibRaw...
link.exe /nologo /out:build\test_star_align.exe build\test_star_align.obj ^
    build\libraw\Release\raw.lib /subsystem:console /entry:mainCRTStartup

if %ERRORLEVEL% NEQ 0 (
    echo Link failed
    pause
    exit /b 1
)

echo Build successful!
echo Output: build\test_star_align.exe
pause