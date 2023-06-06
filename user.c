#include "common.h"
#include <arpa/inet.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

// Retirado das aulas do professor Ítalo.
void usage(const char* bin) {
  eprintf("Usage: %s <server IP address> <server port>\n", bin);
  eprintf("Example IPv4: %s 127.0.0.1 51511\n", bin);
  eprintf("Example IPv6: %s ::1 51511\n", bin);
  exit(EXIT_FAILURE);
}

// Faz o parse do endereço passado como argumento e inicializa um struct do tipo
// sockaddr_storage de acordo com o protocolo adequado. Retorna 0 quando há
// sucesso e -1 caso contrário. Retirado das aulas do professor Ítalo.
int parse_address(const char* addr_str, const char* port_str,
                  struct sockaddr_storage* storage) {
  if (addr_str == NULL || port_str == NULL) {
    return -1;
  }

  uint16_t port = (uint16_t)atoi(port_str); // unsigned short
  if (port == 0) {
    return -1;
  }
  port = htons(port); // host to network short

  struct in_addr inaddr4;                       // 32-bit IPv4 address
  if (inet_pton(AF_INET, addr_str, &inaddr4)) { // presentation to network
    struct sockaddr_in* addr4 = (struct sockaddr_in*)storage;
    addr4->sin_family = AF_INET;
    addr4->sin_port = port;
    addr4->sin_addr = inaddr4;
    return 0;
  }

  struct in6_addr inaddr6;                       // 128-bit IPv6 address
  if (inet_pton(AF_INET6, addr_str, &inaddr6)) { // presentation to network
    struct sockaddr_in6* addr6 = (struct sockaddr_in6*)storage;
    addr6->sin6_family = AF_INET6;
    addr6->sin6_port = port;
    memcpy(&(addr6->sin6_addr), &inaddr6, sizeof(inaddr6));
    return 0;
  }

  return -1;
}

void set_user_list(int* user_list, char* message) {
  char delim[] = ",";
  char* token = strtok(message, delim);
  while (token != NULL) {
    int id = atoi(token);
    user_list[id] = 1;
    token = strtok(NULL, delim);
  }
}

void list_users(int* user_list, int my_id) {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (user_list[i] != 0 && i != my_id) {
      printf("%d ", i);
    }
  }
  printf("\n");
}

void req_add(int socket, msg_t* msg) {
  memset(msg->message, 0, BUFFER_SIZE);
  msg->id_msg = REQ_ADD;
  msg->id_sender = NULL_ID;
  msg->id_receiver = NULL_ID;
  strcpy(msg->message, "REQ_ADD");

  char buffer[BUFFER_SIZE];
  encode(msg, buffer);

  if (send_msg(socket, buffer) != 0) {
    log_exit("send");
  }

  memset(buffer, 0, BUFFER_SIZE);
  if (recv_msg(socket, buffer) <= 0) {
    log_exit("recv");
  }

  if (decode(msg, buffer) == 0) {
    parse_error();
  }
}

void res_list(int socket, int* user_list) {
  char buffer[BUFFER_SIZE];
  memset(buffer, 0, BUFFER_SIZE);
  if (recv_msg(socket, buffer) <= 0) {
    log_exit("recv");
  }

  msg_t msg;
  if (decode(&msg, buffer) == 0) {
    parse_error();
  }

  set_user_list(user_list, msg.message);
}

int handle_input(char* input, int socket, int my_id, int* user_list) {
  fgets(input, BUFFER_SIZE, stdin);

  // Remove \n do input lido pelo fgets
  input[strcspn(input, "\n")] = '\0';

  char* ptr;
  if (strcmp(input, "close connection") == 0) {
    msg_t msg;
    msg.id_msg = REQ_REM;
    msg.id_sender = my_id;
    msg.id_receiver = NULL_ID;
    memset(msg.message, 0, BUFFER_SIZE);
    strcpy(msg.message, "REQ_REM");

    char buffer[BUFFER_SIZE];
    encode(&msg, buffer);

    if (send_msg(socket, buffer) != 0) {
      log_exit("send");
    }

    memset(buffer, 0, strlen(buffer));
    if (recv_msg(socket, buffer) <= 0) {
      log_exit("recv");
    }

    if (decode(&msg, buffer) == 0) {
      parse_error();
    }

    if (msg.id_msg == OK || msg.id_msg == ERROR) {
      printf("%s\n", msg.message);
    }

    return 1;
  } else if (strcmp(input, "list users") == 0) {
    list_users(user_list, my_id);
  }
  // A função strstr encontra a primeira ocorrência de um padrão em uma
  // string. Caso o padrão "send to " for encontrado, então o resto da string
  // provavelmente é um comando para mensagem privada.
  else if ((ptr = strstr(input, "send to ")) != NULL) {
    ptr += strlen("send to ");

    // Se não houver nada após "send to ", trata como um comando inválido
    if (*ptr == '\0')
      return 0;

    char* id_receiver;
    char* message;
    id_receiver = strtok(ptr, " ");
    message = strtok(NULL, " ");

    printf("%s %s\n", id_receiver, message);
  }
  // Nesse caso, se o padrão "send all " for encontrado, então o resto da
  // string provavelmente é um comando para mensagem de broadcast.
  else if ((ptr = strstr(input, "send all ")) != NULL) {
    ptr += strlen("send all ");

    // Se não houver nada após "send to ", trata como um comando inválido
    if (*ptr == '\0')
      return 0;

    printf("%s\n", ptr);
  }

  // Se o input passado não cai em nenhum dos casos anteriores, então é um
  // comando desconhecido.

  return 0;
}

int main(int argc, const char* argv[]) {
  if (argc < 3)
    usage(argv[0]);

  // Faz o parse do endereço recebido como parâmetro
  struct sockaddr_storage storage;
  if (parse_address(argv[1], argv[2], &storage) != 0) {
    usage(argv[0]);
  }

  // Cria um novo socket no endereço
  int sock;
  sock = socket(storage.ss_family, SOCK_STREAM, 0);
  if (sock == -1) {
    log_exit("socket");
  }

  // Abre uma nova conexão no socket criado
  struct sockaddr* addr = (struct sockaddr*)(&storage);
  if (connect(sock, addr, sizeof(storage)) != 0) {
    // A conexão pode falhar caso o servidor não esteja ouvindo
    log_exit("connect");
  }

  int my_id;
  int user_list[MAX_CLIENTS] = {0};

  msg_t msg;
  req_add(sock, &msg);

  // Trata a resposta para a requisição de conexão com o servidor
  printf("%s\n", msg.message);
  if (msg.id_msg == ERROR) {
    close(sock);
    exit(EXIT_FAILURE);
  } else {
    my_id = msg.id_sender;
  }

  res_list(sock, user_list);

  fd_set file_descriptors;
  int max_fd = (sock > STDIN_FILENO) ? sock : STDIN_FILENO;

  char buffer[BUFFER_SIZE];
  while (1) {
    FD_ZERO(&file_descriptors);
    FD_SET(STDIN_FILENO, &file_descriptors);
    FD_SET(sock, &file_descriptors);

    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    if (select(max_fd + 1, &file_descriptors, NULL, NULL, &timeout) == -1) {
      log_exit("select");
    }

    memset(buffer, 0, BUFFER_SIZE);
    if (FD_ISSET(STDIN_FILENO, &file_descriptors)) {
      if (handle_input(buffer, sock, my_id, user_list) == 1)
        break;
    } else if (FD_ISSET(sock, &file_descriptors)) {      
      if (recv_msg(sock, buffer) > 0) {
        msg_t msg;
        if (decode(&msg, buffer) == 0) {
          parse_error();
        }

        if (msg.id_msg == MSG) {
          printf("%s\n", msg.message);
          user_list[msg.id_sender] = 1;
        } else if (msg.id_msg == REQ_REM) {
          printf("User %d left the group!\n", msg.id_sender);
          user_list[msg.id_sender] = 0;
        }
      }
    }
  }

  close(sock);

  exit(EXIT_SUCCESS);
}
