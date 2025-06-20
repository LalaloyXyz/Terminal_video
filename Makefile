# Makefile

CXX = g++
CXXFLAGS = -std=c++17 `pkg-config --cflags opencv4`
LDFLAGS = `pkg-config --libs opencv4`
SRC = main.cpp
OUT = main

all: $(OUT)

$(OUT): $(SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f $(OUT)
