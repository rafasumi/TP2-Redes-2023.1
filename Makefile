CC = gcc
CCFLAGS = -Wall

COMMON=common.c
OBJ=$(patsubst %.c, %.o, $(COMMON))
USER=user.c
SERVER=server.c

build: $(OBJ) server user

server: $(OBJ) $(SERVER)
	$(CC) $(CCFLAGS) -lpthread $(SERVER) $(OBJ) -o server

user: $(OBJ) $(USER)
	$(CC) $(CCFLAGS) $(USER) $(OBJ) -o user

$(OBJ): $(COMMON)
	$(CC) $(CCFLAGS) -c $(COMMON)

clean:
	@rm -f user server $(OBJ)
