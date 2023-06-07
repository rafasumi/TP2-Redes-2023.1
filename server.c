#include "common.h"
#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// Variáveis globais
int active_sockets[MAX_CLIENTS];
unsigned int user_count = 0;

typedef struct thread_args {
  int client_sock;
  pthread_mutex_t* mutex;
} thread_args;

// Retirado das aulas do professor Ítalo.
void usage(const char* bin) {
  eprintf("Usage: %s <v4|v6> <server port>\n", bin);
  eprintf("Example: %s v4 51511\n", bin);
  exit(EXIT_FAILURE);
}

// Inicializa o objeto sockaddr_storage com base no protocolo informado como
// argumento. Retorna 0 quando há sucesso e -1 caso contrário. Retirado das
// aulas do professor Ítalo.
int sockaddr_init(const char* protocol, const char* port_str,
                  struct sockaddr_storage* storage) {
  uint16_t port = (uint16_t)atoi(port_str); // unsigned short
  if (port == 0) {
    return -1;
  }
  port = htons(port); // host to network short

  memset(storage, 0, sizeof(*storage));

  if (strcmp(protocol, "v4") == 0) {
    struct sockaddr_in* addr4 = (struct sockaddr_in*)storage;
    addr4->sin_family = AF_INET;
    addr4->sin_addr.s_addr = INADDR_ANY;
    addr4->sin_port = port;
  } else if (strcmp(protocol, "v6") == 0) {
    struct sockaddr_in6* addr6 = (struct sockaddr_in6*)storage;
    addr6->sin6_family = AF_INET6;
    addr6->sin6_addr = in6addr_any;
    addr6->sin6_port = port;
  } else {
    return -1;
  }

  return 0;
}

int get_id(int socket) {
  int id = 0;
  while (active_sockets[id] != -1) {
    id++;
  }

  active_sockets[id] = socket;
  user_count++;

  return id;
}

void get_user_list(char* buffer) {
  char temp[5];
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (active_sockets[i] != -1) {
      sprintf(temp, "%d,", i);
      strcat(buffer, temp);
    }
  }

  // Remove a última vírgula
  buffer[strlen(buffer) - 1] = '\0';
}

void broadcast(msg_t* msg, int skip_id) {
  char buffer[BUFFER_SIZE];
  encode(msg, buffer);

  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (active_sockets[i] == -1 || i == skip_id) {
      continue;
    }

    if (send_msg(active_sockets[i], buffer) != 0) {
      log_exit("send");
    }
  }
}

void error_msg(int socket, int id_receiver, int error_code) {
  msg_t msg = {.id_msg = ERROR, .id_sender = NULL_ID, .id_receiver = id_receiver};

  memset(msg.message, 0, BUFFER_SIZE);
  switch (error_code) {
  case 1:
    strcpy(msg.message, "User limit exceeded");
    break;
  case 2:
    strcpy(msg.message, "User not found");
    break;
  case 3:
    strcpy(msg.message, "Receiver not found");
    break;
  }

  char buffer[BUFFER_SIZE];
  memset(buffer, 0, BUFFER_SIZE);
  encode(&msg, buffer);

  if (send_msg(socket, buffer) != 0) {
    log_exit("send");
  }
}

void ok_msg(int socket, int id_receiver) {
  msg_t msg = {.id_msg = OK, .id_sender = NULL_ID, .id_receiver = id_receiver};
  memset(msg.message, 0, BUFFER_SIZE);
  strcpy(msg.message, "Removed Successfully");

  char buffer[BUFFER_SIZE];
  memset(buffer, 0, BUFFER_SIZE);
  encode(&msg, buffer);

  if (send_msg(socket, buffer) != 0) {
    log_exit("send");
  }
}

void* client_thread(void* args) {
  thread_args* cdata = (thread_args*)args;

  msg_t msg;
  char buffer[BUFFER_SIZE];
  while (1) {
    memset(buffer, 0, BUFFER_SIZE);
    if (recv_msg(cdata->client_sock, buffer) <= 0) {
      break;
    }

    if (decode(&msg, buffer) == 0) {
      parse_error();
    }

    if (msg.id_msg == REQ_ADD) {
      msg_t ret_msg;
      memset(ret_msg.message, 0, BUFFER_SIZE);

      pthread_mutex_lock(cdata->mutex); // LOCK

      if (user_count == 15) {
        // id_receiver precisa ser nulo nesse caso, pois o usuário não possui um ID
        error_msg(cdata->client_sock, NULL_ID, 1);
        break;
      }

      int new_id = get_id(cdata->client_sock);
      printf("User %d added\n", new_id);

      ret_msg.id_msg = MSG;
      ret_msg.id_sender = new_id;
      ret_msg.id_receiver = NULL_ID;
      sprintf(ret_msg.message, "User %d joined the group", new_id);
      broadcast(&ret_msg, NULL_ID);

      memset(ret_msg.message, 0, strlen(ret_msg.message));
      get_user_list(ret_msg.message);

      pthread_mutex_unlock(cdata->mutex); // UNLOCK

      ret_msg.id_msg = RES_LIST;
      ret_msg.id_sender = NULL_ID;
      ret_msg.id_receiver = NULL_ID;

      memset(buffer, 0, BUFFER_SIZE);
      encode(&ret_msg, buffer);

      if (send_msg(cdata->client_sock, buffer) != 0) {
        log_exit("send");
      }
    } else if (msg.id_msg == REQ_REM) {
      pthread_mutex_lock(cdata->mutex); // LOCK

      if (active_sockets[msg.id_sender] == -1) {
        error_msg(cdata->client_sock, msg.id_sender, 2);
      } else {
        ok_msg(cdata->client_sock, msg.id_sender);
        active_sockets[msg.id_sender] = -1;
        user_count--;

        printf("User %d removed\n", msg.id_sender);

        broadcast(&msg, NULL_ID);
      }

      pthread_mutex_unlock(cdata->mutex); // UNLOCK

      break;
    } else {
      eprintf("Unknown message ID.");
      exit(EXIT_FAILURE);
    }
  }

  close(cdata->client_sock);
  free(cdata);
  pthread_exit(NULL);
}

int main(int argc, const char* argv[]) {
  if (argc < 3)
    usage(argv[0]);

  // Inicializa o objeto sockaddr_storage para dar bind em todos os endereços
  // IP associados à interface
  struct sockaddr_storage storage;
  if (sockaddr_init(argv[1], argv[2], &storage) != 0) {
    usage(argv[0]);
  }

  int server_sock;
  server_sock = socket(storage.ss_family, SOCK_STREAM, 0);
  if (server_sock == -1) {
    log_exit("socket");
  }

  // Opção que permite reaproveitar um socket em uso
  int enable = 1;
  if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) != 0) {
    log_exit("setsockopt");
  }

  struct sockaddr* addr = (struct sockaddr*)(&storage);
  if (bind(server_sock, addr, sizeof(storage)) != 0) {
    log_exit("bind");
  }

  if (listen(server_sock, MAX_CLIENTS) != 0) {
    log_exit("listen");
  }

  pthread_mutex_t mutex;
  pthread_mutex_init(&mutex, NULL);
  memset(active_sockets, -1, sizeof(active_sockets));
  while (1) {
    struct sockaddr_storage client_storage;
    struct sockaddr* client_addr = (struct sockaddr*)(&client_storage);
    socklen_t client_addrlen = sizeof(client_storage);

    int client_sock = accept(server_sock, client_addr, &client_addrlen);
    if (client_sock == -1) {
      log_exit("accept");
    }

    thread_args* cdata = (thread_args*)malloc(sizeof(thread_args));
    cdata->client_sock = client_sock;
    cdata->mutex = &mutex;

    pthread_t thread_id;
    pthread_create(&thread_id, NULL, client_thread, (void*)cdata);
  }

  pthread_mutex_destroy(&mutex);
  close(server_sock);

  exit(EXIT_SUCCESS);
}
