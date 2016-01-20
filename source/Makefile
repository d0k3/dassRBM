# Project: dassRBM
# Makefile created by _king_ 01/30/2012
# Working with GCC 4.6.0

CC      = gcc
CPP     = g++
RC      = windres -O coff
RM      = del
CFLAGS  = -I. -O3 -Wall -pedantic -funroll-loops -ffast-math -fsched-spec-load -fomit-frame-pointer
LDFLAGS = -s -static-libgcc -static-libstdc++
DEPS    = lodepng.h Makefile
OBJ     = lodepng.o dassRBM.o
RES     = 
BIN     = dassRBM

%.o: %.cpp $(DEPS)
	$(CPP) -c -o $@ $< $(CFLAGS)
	
%.res: %.rc
	$(RC) $< $@
	
$(BIN): $(OBJ) $(RES)
	$(CPP) -o $@ $^ $(LDFLAGS)

clean:
	$(RM) *.o
