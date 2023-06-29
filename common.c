#include "common.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>

int is_number(const char* str, size_t len) {
  if (!isdigit(str[0]) && (str[0] != '-' || len == 1)) {
    return 0;
  }

  for (int i = 1; i < len; i++) {
    if (!isdigit(str[i]))
      return 0;
  }

  return 1;
}

void set_time_str(char* time_str) {
  time_t now = time(NULL);
  struct tm* local_time = localtime(&now);
  strftime(time_str, sizeof(time_str) + 1, "[%H:%M]", local_time);
}

int encode(const msg_t* msg, char* outBuf) {
  return sprintf(outBuf, "%d%c%d%c%d%c%s", msg->id_msg, SEPARATOR, msg->id_receiver, SEPARATOR,
                 msg->id_sender, SEPARATOR, msg->message);
}

int decode(msg_t* msg, char* inBuf) {
  char* token;
  char delim[1] = {SEPARATOR};

  // ID da mensagem
  token = strtok(inBuf, delim);
  if (token == NULL)
    return 0;

  // Certifica que o ID da mensagem é um número inteiro válido antes de fazer a
  // conversão.
  if (!is_number(token, strlen(token)))
    return 0;
  msg->id_msg = atoi(token);

  // ID do remetente
  token = strtok(NULL, delim);
  if (token == NULL)
    return 0;

  // Certifica que o ID do remetente é um número inteiro válido antes de fazer a
  // conversão.
  if (!is_number(token, strlen(token)))
    return 0;
  msg->id_receiver = atoi(token);

  // ID do destinatário
  token = strtok(NULL, delim);
  if (token == NULL)
    return 0;

  // Certifica que o ID do destinatário é um número inteiro válido antes de
  // fazer a conversão.
  if (!is_number(token, strlen(token)))
    return 0;
  msg->id_sender = atoi(token);

  // Mensagem
  token = strtok(NULL, delim);
  if (token == NULL)
    return 0;

  memset(msg->message, 0, BUFFER_SIZE);
  strcpy(msg->message, token);

  return 1;
}

int send_msg(int socket, const char* buffer) {
  size_t buffer_len = strlen(buffer);
  // Faz a conversão para a representação de rede
  uint16_t msg_size = htons(buffer_len);

  // Envia um inteiro positivo de 16 bits para informar o tamanho da mensagem
  // que será enviada
  if (send(socket, &msg_size, sizeof(uint16_t), 0) != sizeof(uint16_t)) {
    // Assume que o envio foi mal-sucedido caso o número de bytes enviados não
    // seja igual ao que se desejava enviar
    return -1;
  }

  // Envia a mensagem de fato
  if (send(socket, buffer, buffer_len, 0) != buffer_len) {
    // Assume que o envio foi mal-sucedido caso o número de bytes enviados não
    // seja igual ao que se desejava enviar
    return -1;
  }

  return 0;
}

size_t recv_msg(int socket, char* buffer) {
  size_t header_size = sizeof(uint16_t);
  char header_buffer[sizeof(uint16_t)];

  // Primeiro, recebe o "cabeçalho" que e informa o tamanho do conteúdo da
  // mensagem e tem exatamente 16 bits
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
  // Faz a conversão para a representação da máquina
  msg_size = ntohs(msg_size);

  // Após determinar o tamanho da mensagem, recebe o conteúdo da mensagem
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

void log_exit(const char* msg) {
  perror(msg);
  exit(EXIT_FAILURE);
}

void parse_error() {
  eprintf("Error while parsing incoming message.\n");
  exit(EXIT_FAILURE);
}
