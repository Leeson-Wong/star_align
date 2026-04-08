@echo off
echo Building basic test...

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

cd /d E:\final\star_align

echo Compiling basic_test.cpp...
cl.exe /nologo /std:c++17 /EHsc /I dss ^
    /c basic_test.cpp /Fo:build\basic_test.obj /O2 /arch:AVX2

if %ERRORLEVEL% NEQ 0 (
    echo Compilation failed
    pause
    exit /b 1
)

echo Linking...
link.exe /nologo /out:build\basic_test.exe build\basic_test.obj ^
    /subsystem:console /entry:mainCRTStartup

if %ERRORLEVEL% NEQ 0 (
    echo Link failed
    pause
    exit /b 1
)

echo Build successful!
echo.
echo Running test:
echo -------------
build\basic_test.exe
echo.
pause