
BIN			= mountwrapper
CXXFLAGS 	= -O2
#CXXFLAGS 	= -g
CXXFLAGS	+= -std=c++17 -Wall -Werror
CXXFLAGS	+= -static

LDFLAGS		= -static

all: $(BIN)

clean:
	rm -f $(BIN)
