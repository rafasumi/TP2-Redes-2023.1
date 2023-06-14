#include "common.h"
#include <arpa/inet.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct thread_args {
  int socket;
  int my_id;
  int* user_list;
  int* confirmed;
  pthread_cond_t* confirmation_arrived;
  pthread_mutex_t* mutex;
} thread_args;

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

void list_users(const int* user_list, int my_id) {
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
  memset(buffer, 0, BUFFER_SIZE);
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

void* handle_input(void* args) {
  thread_args* input_args = (thread_args*)args;

  while (1) {
    char input[BUFFER_SIZE];
    fgets(input, BUFFER_SIZE, stdin);

    // Remove \n do input lido pelo fgets
    input[strcspn(input, "\n")] = '\0';

    char* ptr;
    if (strcmp(input, "close connection") == 0) {
      msg_t msg = {.id_msg = REQ_REM, .id_sender = input_args->my_id, .id_receiver = NULL_ID};
      memset(msg.message, 0, BUFFER_SIZE);
      strcpy(msg.message, "REQ_REM");

      char buffer[BUFFER_SIZE];
      memset(buffer, 0, BUFFER_SIZE);
      encode(&msg, buffer);

      if (send_msg(input_args->socket, buffer) != 0) {
        log_exit("send");
      }

      break;
    } else if (strcmp(input, "list users") == 0) {
      pthread_mutex_lock(input_args->mutex);
      list_users(input_args->user_list, input_args->my_id);
      pthread_mutex_unlock(input_args->mutex);
    }
    // A função strstr encontra a primeira ocorrência de um padrão em uma
    // string. Caso o padrão "send to " for encontrado, então o resto da string
    // provavelmente é um comando para mensagem privada.
    else if ((ptr = strstr(input, "send to ")) != NULL) {
      ptr += strlen("send to ");

      // Se não houver nada após "send to ", trata como um comando inválido
      if (*ptr == '\0')
        continue;

      // Obtém o resto dos parâmetros da string de entrada
      char id_receiver[BUFFER_SIZE];
      memset(id_receiver, 0, BUFFER_SIZE);
      char message[BUFFER_SIZE];
      memset(message, 0, BUFFER_SIZE);
      sscanf(ptr, "%s %[^\n]", id_receiver, message);

      // Trata o caso em que não há nada após o ID do destinatário
      if (message[0] == '\0')
        continue;

      // Não envia mensagem para o servidor caso ID não seja um número válido ou
      // se for igual a -1 (NULL_ID). Isso é feito para evitar ambiguidades no
      // servidor.
      if (!is_number(id_receiver, strlen(id_receiver)) || strcmp(id_receiver, "-1") == 0) {
        printf("Receiver not found\n");
        continue;
      }

      msg_t msg = {
          .id_msg = MSG, .id_sender = input_args->my_id, .id_receiver = atoi(id_receiver)};
      memset(msg.message, 0, BUFFER_SIZE);
      strcpy(msg.message, message);

      char buffer[BUFFER_SIZE];
      memset(buffer, 0, BUFFER_SIZE);
      encode(&msg, buffer);

      if (send_msg(input_args->socket, buffer) != 0) {
        log_exit("send");
      }

      pthread_mutex_lock(input_args->mutex);
      *input_args->confirmed = 0;
      pthread_cond_wait(input_args->confirmation_arrived, input_args->mutex);
      if (*input_args->confirmed) {
        char time_str[7];
        set_time_str(time_str);
        printf("P %s -> %d: %s\n", time_str, msg.id_receiver, msg.message);
      }
      pthread_mutex_unlock(input_args->mutex);
    }
    // Nesse caso, se o padrão "send all " for encontrado, então o resto da
    // string provavelmente é um comando para mensagem de broadcast.
    else if ((ptr = strstr(input, "send all ")) != NULL) {
      ptr += strlen("send all ");

      // Se não houver nada após "send all ", trata como um comando inválido
      if (*ptr == '\0')
        continue;

      msg_t msg = {.id_msg = MSG, .id_sender = input_args->my_id, .id_receiver = NULL_ID};
      memset(msg.message, 0, BUFFER_SIZE);
      strcpy(msg.message, ptr);

      char buffer[BUFFER_SIZE];
      memset(buffer, 0, BUFFER_SIZE);
      encode(&msg, buffer);

      if (send_msg(input_args->socket, buffer) != 0) {
        log_exit("send");
      }
    } else {
      // Se o input passado não cai em nenhum dos casos anteriores, então é um
      // comando desconhecido.
      continue;
    }
  }

  pthread_exit(NULL);
}

void* handle_recv(void* args) {
  thread_args* recv_args = (thread_args*)args;

  char buffer[BUFFER_SIZE];
  msg_t msg;
  while (1) {
    memset(buffer, 0, BUFFER_SIZE);
    if (recv_msg(recv_args->socket, buffer) <= 0) {
      log_exit("recv");
    }

    if (decode(&msg, buffer) == 0) {
      parse_error();
    }

    if (msg.id_msg == REQ_REM) {
      printf("User %d left the group!\n", msg.id_sender);

      pthread_mutex_lock(recv_args->mutex);
      recv_args->user_list[msg.id_sender] = 0;
      pthread_mutex_unlock(recv_args->mutex);
    } else if (msg.id_msg == MSG) {
      // Esse acesso à lista de usuário não precisa de exclusão mútua, já que
      // a lista só pode ser alterada pela thread atual.
      if (recv_args->user_list[msg.id_sender] == 1) {
        char time_str[7];
        set_time_str(time_str);

        if (msg.id_receiver != NULL_ID) {
          printf("P ");
        }

        printf("%s", time_str);

        if (msg.id_sender != recv_args->my_id)
          printf(" %d:", msg.id_sender);

        printf(" %s\n", msg.message);
      } else {
        // É a primeira mensagem recebida do usuário, portanto não deve ser
        // impressa com o timestamp.
        printf("%s\n", msg.message);

        pthread_mutex_lock(recv_args->mutex);
        recv_args->user_list[msg.id_sender] = 1;
        pthread_mutex_unlock(recv_args->mutex);
      }
    } else if (msg.id_msg == OK) {
      if (strcmp(msg.message, "Removed Successfully") == 0) {
        printf("%s\n", msg.message);
        break;
      } else {
        pthread_mutex_lock(recv_args->mutex);
        *recv_args->confirmed = 1;
        pthread_cond_signal(recv_args->confirmation_arrived);
        pthread_mutex_unlock(recv_args->mutex);
      }
    } else if (msg.id_msg == ERROR) {
      printf("%s\n", msg.message);

      if (strcmp(msg.message, "Receiver not found") == 0) {
        pthread_mutex_lock(recv_args->mutex);
        pthread_cond_signal(recv_args->confirmation_arrived);
        pthread_mutex_unlock(recv_args->mutex);
      }
    }
  }

  pthread_exit(NULL);
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
  } else if (msg.id_msg == MSG) {
    my_id = msg.id_sender;
  }

  res_list(sock, user_list);

  pthread_mutex_t mutex;
  pthread_mutex_init(&mutex, NULL);
  pthread_cond_t confirmation_arrived;
  pthread_cond_init(&confirmation_arrived, NULL);

  int confirmed;

  pthread_t input_thread;
  thread_args input_args = {.socket = sock,
                            .my_id = my_id,
                            .user_list = user_list,
                            .confirmed = &confirmed,
                            .confirmation_arrived = &confirmation_arrived,
                            .mutex = &mutex};

  pthread_t recv_thread;
  thread_args recv_args = input_args;

  pthread_create(&input_thread, NULL, handle_input, &input_args);
  pthread_create(&recv_thread, NULL, handle_recv, &recv_args);

  pthread_join(input_thread, NULL);
  pthread_join(recv_thread, NULL);

  pthread_mutex_destroy(&mutex);
  close(sock);

  exit(EXIT_SUCCESS);
}
