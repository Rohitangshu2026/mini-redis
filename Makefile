CXX = g++
CXXFLAGS = -Wall -Wextra -O2 -g -Iinclude

SERVER_SRC = \
	src/main.cpp \
	src/server/server.cpp \
	src/server/client_connection.cpp

CLIENT_SRC = client/client.cpp
CLIENT_BIN = client_app

.PHONY: all server client_app clean

all: server client_app

server:
	$(CXX) $(CXXFLAGS) $(SERVER_SRC) -o server

client_app:
	$(CXX) $(CXXFLAGS) $(CLIENT_SRC) -o $(CLIENT_BIN)

clean:
	rm -f server $(CLIENT_BIN)
