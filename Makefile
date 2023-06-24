cc = gcc

server : server.c
	$(cc) server.c -o server

server_debug : server.c
	$(cc) -g server.c -o server_debug

.PHONY: clean
clean:
	rm -f server server_debug
