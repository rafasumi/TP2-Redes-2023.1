#include "common.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

int encode(const msg_t* msg, char* outBuf) {
  return sprintf(outBuf, "%d%c%d%c%d%c%s", msg->id_msg, SEPARATOR, msg->id_receiver, SEPARATOR,
                 msg->id_sender, SEPARATOR, msg->message);
}

int is_number(const char* str, size_t len) {
  for (int i = 0; i < len; i++) {
    if (!isdigit(str[i]))
      return 0;
  }

  return 1;
}

int decode(msg_t* msg, char* inBuf) {
  char* token;
  char delim[1] = {SEPARATOR};

  token = strtok(inBuf, delim);
  if (token == NULL)
    return 0;

  if (!is_number(token, strlen(token)))
    return 0;
  msg->id_msg = atoi(token);

  token = strtok(NULL, delim);
  if (token == NULL)
    return 0;

  if (!is_number(token, strlen(token)))
    return 0;
  msg->id_receiver = atoi(token);

  token = strtok(NULL, delim);
  if (token == NULL)
    return 0;

  if (!is_number(token, strlen(token)))
    return 0;
  msg->id_sender = atoi(token);

  token = strtok(NULL, delim);
  if (token == NULL)
    return 0;

  memset(msg->message, 0, BUFFER_SIZE);
  strcpy(msg->message, token);

  return 1;
}

int send_msg(int socket, char* buffer) {
  size_t buffer_len = strlen(buffer);
  uint16_t msg_size = htons(buffer_len);

  if (send(socket, &msg_size, sizeof(uint16_t), 0) != sizeof(uint16_t)) {
    return -1;
  }

  if (send(socket, buffer, buffer_len, 0) != buffer_len) {
    return -1;
  }

  return 0;
}

size_t recv_msg(int socket, char* buffer) {
  size_t header_size = sizeof(uint16_t);
  char header_buffer[sizeof(uint16_t)];

  char* ptr = header_buffer;
  size_t count;
  while (header_size > 0) {
    count = recv(socket, ptr, header_size, 0);
    if (count <= 0) {
      return count;
    }

    ptr += count;
    header_size -= count;
  }

  uint16_t msg_size;
  memcpy(&msg_size, header_buffer, sizeof(uint16_t));
  msg_size = ntohs(msg_size);

  ptr = buffer;
  while (msg_size > 0) {
    count = recv(socket, ptr, msg_size, 0);
    if (count <= 0) {
      return count;
    }

    ptr += count;
    msg_size -= count;
  }

  return 1;
}

void print_msg(const msg_t* msg) {
  printf("ID: %d\nSENDER: %d\nRECEIVER: %d\nMESSAGE: %s\n", msg->id_msg, msg->id_sender,
         msg->id_receiver, msg->message);
}

// Retirado das aulas do professor Ítalo.
void log_exit(const char* msg) {
  perror(msg);
  exit(EXIT_FAILURE);
}

void parse_error() {
  eprintf("Error while parsing incoming message.");
  exit(EXIT_FAILURE);
}
