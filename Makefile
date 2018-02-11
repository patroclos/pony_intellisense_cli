flags = -std=c++11 -fexceptions -ggdb -O0
ldflags = -std=c++11 -lponyc -lponyrt -lponyrt-pic -pthread -ldl -latomic -lLLVM-4.0 -lm -lblake2 -L. -Llib/pony
ponysrc = $(HOME)/ponyc/src

FILES = $(shell find src -name '*.cpp')

all:
	g++ $(flags) -Ilib -I$(ponysrc) -I$(ponysrc)/common -I$(ponysrc)/libponyc -I$(ponysrc)/libponyrt -o main $(FILES) $(ldflags)

lex: src/textlevel.cpp
	g++ $(flags) -Ilib -I$(ponysrc) -I$(ponysrc)/common -I$(ponysrc)/libponyc -I$(ponysrc)/libponyrt -o lex src/textlevel.cpp src/pos.cpp $(ldflags)
