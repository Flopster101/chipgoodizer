# Builds the CLI binary on its own: three source files, no build system needed.
# The libraries and the test suite still go through CMake.

CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
CPPFLAGS ?= -Iinclude

# Appended last so a host's -ffast-math cannot change the engine's arithmetic.
FPFLAGS := -fno-fast-math

SOURCES := src/dsp/Chipgoodizer.cpp src/io/WavFile.cpp src/cli/main.cpp
HEADERS := $(wildcard include/chipgoodizer/*.h include/chipgoodizer/*.hpp include/chipgoodizer/*/*.hpp)

chipgoodizer: $(SOURCES) $(HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(FPFLAGS) $(SOURCES) -o $@

clean:
	rm -f chipgoodizer

.PHONY: clean
