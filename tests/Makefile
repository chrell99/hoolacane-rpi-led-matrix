# Compiler and Flags
CXX = g++
CXXFLAGS = -Wall -O3 -g -Wextra -Wno-unused-parameter -I../include
LDFLAGS = -L../lib -lrgbmatrix -lrt -lm -lpthread -lfftw3 -lasound

# Source Files
SOURCES = minimal-example.cc strobe-test.cc sound-test.cc fft-test.cc
EXECUTABLES = $(SOURCES:.cc=)

# Default Target: Build All Executables
all: $(EXECUTABLES)

# Compile Each Source File into an Executable
%: %.cc
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

# Clean Build Files
clean:
	rm -f $(EXECUTABLES)
