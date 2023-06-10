#ifndef COMMON_H
#define COMMON_H

#include <unistd.h>

#define eprintf(...) fprintf(stderr, __VA_ARGS__)

#define BUFFER_SIZE 2048

#define MAX_CLIENTS 15
#define NULL_ID -1

#define SEPARATOR '\x1D' // Group separator

#define REQ_ADD 1
#define REQ_REM 2
#define RES_LIST 4
#define MSG 6
#define ERROR 7
#define OK 8

typedef struct msg_t {
  unsigned int id_msg;
  unsigned int id_sender;
  unsigned int id_receiver;
  char message[BUFFER_SIZE];
} msg_t;

int is_number(const char* str, size_t len);

int encode(const msg_t* msg, char* outBuf);
int decode(msg_t* msg, char* inBuf);

int send_msg(int socket, const char* buffer);
size_t recv_msg(int socket, char* buffer);

// DEBUG USAGE ONLY
void print_msg(const msg_t* msg);

void log_exit(const char* msg);
void parse_error();

#endif