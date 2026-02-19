all: server client

server: server.o utils.o sqlite3.o
	g++ -o server server.o utils.o sqlite3.o -lpthread -ldl

client: client.o
	g++ -o client client.o

sqlite3.o: sqlite3.c sqlite3.h
	gcc -c sqlite3.c -o sqlite3.o

server.o: server.cpp utils.hpp
	g++ -c server.cpp -o server.o

utils.o: utils.cpp utils.hpp
	g++ -c utils.cpp -o utils.o

client.o: client.cpp
	g++ -c client.cpp -o client.o

clean:
	rm -f server.o utils.o sqlite3.o client.o server client

run: server
	./server

.PHONY: all clean run
