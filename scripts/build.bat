@echo off
rem so you don't have to remember to launch a developer command prompt
rem every time. just double-click this or run it from any cmd window.
rem
rem if you're not on this exact dev box, change the two paths below.
rem if you don't have qt at C:\Qt\6.8.3, change the qt path below.
rem if you don't have visual studio 2022/2026, change the vs path below.
rem if you have neither, this is the wrong project for you, sorry.

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 (
    echo vcvars64.bat failed. is visual studio actually installed?
    exit /b 1
)

cd /d "%~dp0\.."

if not exist build\CMakeCache.txt (
    cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
    if errorlevel 1 (
        echo cmake configure failed. read the error above. it's probably qt.
        exit /b 1
    )
)

cmake --build build %*
