@echo off
set /P command=Enter command: 
make -f config.makefile %command%
pause