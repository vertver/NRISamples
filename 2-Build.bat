@echo off

if not exist _Compiler (
    mkdir _Compiler
)

pushd _Compiler
del CMakeCache.txt
cmake -DCMAKE_BUILD_TYPE=Release .. -A x64
cmake --build . --config Release
popd

echo.
set /P M=Do you want to build DEBUG configuration? [y/n]
if /I "%M%" neq "y" goto END

:BUILD_DEBUG

pushd _Compiler
del CMakeCache.txt
cmake -DCMAKE_BUILD_TYPE=Debug .. -A x64
cmake --build . --config Debug
popd

:END
pause
