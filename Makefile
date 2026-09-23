CC      ?= cc
CXX     ?= c++
CFLAGS  ?= -O2 -g -Wall -Wextra -std=c11
CXXFLAGS?= -O2 -g -Wall -Wextra -std=c++11
CPPFLAGS += -Isrc $(shell pkg-config --cflags sdl2)
LDLIBS  += $(shell pkg-config --libs sdl2) -lm

SRC   := $(wildcard src/*.c)
CXXSRC:= $(wildcard src/opl/*.cpp)
OBJ   := $(SRC:.c=.o) $(CXXSRC:.cpp=.o)
HDR   := $(wildcard src/*.h) $(wildcard src/opl/*.h)
BIN   := picosupaplex

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ) $(LDLIBS)

src/%.o: src/%.c $(HDR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

src/opl/%.o: src/opl/%.cpp $(HDR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(BIN)

run: $(BIN)
	./$(BIN)

.PHONY: all clean run
