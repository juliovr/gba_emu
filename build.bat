@echo off

set common_compiler_flags=-Od -fp:fast -nologo -MD -Oi -Gm- -GR- -EHa- -W4 -WX -wd4201 -wd4100 -wd4189 -wd4505 -wd4456 -wd4459 -wd4311 -wd4312 -wd4302 -wd4706 -wd4127 -FC -Z7
set common_compiler_flags=-D_CRT_SECURE_NO_WARNINGS %common_compiler_flags%
set common_compiler_flags=-D_DEBUG %common_compiler_flags%
@REM set common_compiler_flags=-D_DEBUG_PRINT %common_compiler_flags%

IF NOT EXIST bin mkdir bin
pushd bin

cl %common_compiler_flags% ..\src\main.c /link -incremental:no -opt:ref ..\lib\raylib.lib user32.lib gdi32.lib winmm.lib shell32.lib

popd
