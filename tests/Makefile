# Compiler and Flags
CXX = g++
CXXFLAGS = -Wall -O3 -g -Wextra -Wno-unused-parameter -I../include
LDFLAGS = -L../lib -lrgbmatrix -lrt -lm -lpthread

# Source Files
SOURCES = minimal-example.cc
EXECUTABLES = $(SOURCES:.cc=)

# Default Target: Build All Executables
all: $(EXECUTABLES)

# Compile Each Source File into an Executable
%: %.cc
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

# Clean Build Files
clean:
	rm -f $(EXECUTABLES)
