
BIN			= mountwrapper
CXXFLAGS 	= -O2 -g -static -std=c++17 -Wall -Werror
LDFLAGS		= -static

all: $(BIN)

clean:
	rm -f $(BIN)
