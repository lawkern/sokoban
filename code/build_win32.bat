@ECHO off

SET DEVELOPMENT_BUILD=1

SET WARNING_FLAGS=-WX -W4 -wd4201 -wd4204 -wd4702
IF %DEVELOPMENT_BUILD%==1 (
   SET WARNING_FLAGS=%WARNING_FLAGS% -wd4100 -wd4101 -wd4189
)

SET COMPILER_FLAGS=-nologo -Z7 -Oi -Od -FC -MT -diagnostics:column %WARNING_FLAGS% -DDEVELOPMENT_BUILD=%DEVELOPMENT_BUILD%
SET LINKER_FLAGS=-opt:ref -incremental:no user32.lib gdi32.lib winmm.lib ole32.lib mmdevapi.lib

IF NOT EXIST ..\build mkdir ..\build
PUSHD ..\build

REM NOTE(law): Compile any resources needed by the executable
rc -nologo -i ..\data -fo sokoban.res ..\data\sokoban.rc

REM NOTE(law): Compile the actual executable
cl ..\code\platform_win32.c sokoban.res %COMPILER_FLAGS% -Fesokoban /link %LINKER_FLAGS%

POPD
