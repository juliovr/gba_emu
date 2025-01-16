@echo off

set common_compiler_flags=-Od -fp:fast -nologo -MT -Oi -Gm- -GR- -EHa- -W4 -WX -wd4201 -wd4100 -wd4189 -wd4505 -wd4456 -wd4459 -wd4311 -wd4312 -wd4302 -wd4706 -wd4127 -FC -Z7
set common_compiler_flags=-D_DEBUG -D_CRT_SECURE_NO_WARNINGS %common_compiler_flags%

IF NOT EXIST bin mkdir bin
pushd bin

cl %common_compiler_flags% ..\src\main.c /link -incremental:no -opt:ref

popd
