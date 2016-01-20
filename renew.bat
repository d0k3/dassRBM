@echo off
cd source
make
del *.o
move /Y dassRBM.exe ..
cd ..
upx --best --lzma dassRBM.exe
copy /Y dassRBM.exe ..\Programme
7z a dassRBM.7z *.exe *_all.bat *.txt sample source