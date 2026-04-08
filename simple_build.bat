@echo off
echo Setting up Visual Studio environment...

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

cd /d E:\final\star_align

echo Building star_align.cpp...
cl.exe /nologo /std:c++17 /EHsc /I dss /I libraw\include ^
    /c star_align.cpp /Fo:build\star_align.obj /O2 /arch:AVX2

if %ERRORLEVEL% NEQ 0 (
    echo Build failed
    pause
    exit /b 1
)

echo Linking...
link.exe /nologo /out:build\star_align.exe build\star_align.obj ^
    /subsystem:console /entry:mainCRTStartup /LIBPATH:libraw\lib\static

if %ERRORLEVEL% NEQ 0 (
    echo Link failed
    pause
    exit /b 1
)

echo Build successful!
echo Output: build\star_align.exe
pause