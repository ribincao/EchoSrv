CC = g++
CFLAGS = -std=c++11
Server:
	$(CC) $(CFLAGS) -o Srv Srv.cpp
 
Client:
	$(CC) $(CFLAGS) -o Cli Cli.cpp
 
clean:
	rm -f *.o Srv Cli