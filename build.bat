@echo off
cd /d "%~dp0"
if not exist build mkdir build
rc /nologo /fo build\DungeonSpawnerForcer1.16.5.res src\DungeonSpawnerForcer1.16.5.rc
cl /nologo /O2 /std:c11 /W3 /wd4244 /wd4305 /wd4101 /D_CRT_SECURE_NO_WARNINGS /Fobuild\ /Fe:DungeonSpawnerForcer1.16.5.exe src\*.c src\cubiomes\*.c build\DungeonSpawnerForcer1.16.5.res
