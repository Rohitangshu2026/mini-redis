CXX = g++
CXXFLAGS = -Wall -Wextra -O2 -g -Iinclude

SERVER_SRC = src/server/server.cpp
CLIENT_SRC = src/client/client.cpp

all: server client

server:
	$(CXX) $(CXXFLAGS) $(SERVER_SRC) -o server

client:
	$(CXX) $(CXXFLAGS) $(CLIENT_SRC) -o client

clean:
	rm -f server client
