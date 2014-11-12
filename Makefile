CXXFLAGS = -Wall --std=c++11

test: main.cpp
	$(CXX) $(CXXFLAGS) -lflycapture $< -o $@

.PHONY: all

all: test
