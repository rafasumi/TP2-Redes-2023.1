#include "common.h"
#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/* ------------------------- Variáveis globais ------------------------- */
// Array com os sockets com conexões ativas. O valor armazenado é o file
// descriptor do socket caso ele esteja ativo e -1 caso contrário.
int active_sockets[MAX_CLIENTS];
// Contagem de usuários ativos.
unsigned int user_count = 0;

// Struct que é usado para a passagem de argumentos às threads que fazem o
// processamento de cada cliente.
typedef struct server_thread_args {
  // Socket do cliente processado pela thread.
  int client_sock;

  // Trava mutex a ser usada pelas threads.
  pthread_mutex_t* mutex;
} server_thread_args;

// Função auxiliar para obter um novo ID para um cliente. Basicamente, pega a
// primeira posição do array "active_sockets" que é igual a -1. Essa função
// assume que há alguma posição vazia no array. Essa função precisa ser
// executada em exclusão mútua para evitar condições de corrida, já que as
// variáveis globais "active_sockets" e "user_count" são atualizados aqui.
int get_id(int socket) {
  int id = 0;
  while (active_sockets[id] != -1) {
    id++;
  }

  active_sockets[id] = socket;
  user_count++;

  return id;
}

// Função auxiliar para obter uma representação em string da lista de usuários
// ativos no momento. Como essa função precisa percorrer o array
// "active_sockets", ela precisa ser executada em exclusão mútua para evitar
// possíveis condições de corrida.
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

// Função usada para enviar mensagem pública a todos os usuários ativos no
// momento. O usuário de ID "skip_id" é ignorado, o que pode ser útil, por
// exemplo, para enviar uma versão alterada da mensagem para ele.
void broadcast(msg_t* msg, int skip_id) {
  char buffer[BUFFER_SIZE];
  memset(buffer, 0, BUFFER_SIZE);
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

// Envia uma mensagem do tipo ERROR no socket "socket", para o destinatário de
// ID "id_receiver" e com a mensagem de código "error_code".
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

// Envia uma mensagem do tipo OK no socket "socket", para o destinatário de ID
// "id_receiver".
void ok_msg(int socket, int id_receiver, int ok_code) {
  msg_t msg = {.id_msg = OK, .id_sender = NULL_ID, .id_receiver = id_receiver};

  memset(msg.message, 0, BUFFER_SIZE);
  switch (ok_code) {
  case 1:
    strcpy(msg.message, "Removed Successfully");
    break;
  case 2:
    strcpy(msg.message, "OK");
    break;
  }

  char buffer[BUFFER_SIZE];
  memset(buffer, 0, BUFFER_SIZE);
  encode(&msg, buffer);

  if (send_msg(socket, buffer) != 0) {
    log_exit("send");
  }
}

// Função a ser executada pelas threads que realizam o processamento de cada
// cliente.
void* client_thread(void* args) {
  server_thread_args* cdata = (server_thread_args*)args;

  msg_t msg;
  char buffer[BUFFER_SIZE];

  // Recebe mensagens continuamente e realiza o processamento adequado
  while (1) {
    memset(buffer, 0, BUFFER_SIZE);
    if (recv_msg(cdata->client_sock, buffer) <= 0) {
      log_exit("recv");
    }

    if (decode(&msg, buffer) == 0) {
      parse_error();
    }

    if (msg.id_msg == REQ_ADD) {
      pthread_mutex_lock(cdata->mutex);

      if (user_count == 15) {
        pthread_mutex_unlock(cdata->mutex);
        // id_receiver precisa ser nulo nesse caso, pois o usuário não possui um
        // ID
        error_msg(cdata->client_sock, NULL_ID, 1);

        // Como o limite de usuários já foi excedido, o loop é finalizado e a
        // thread para de executar
        break;
      }

      // Define um identificador para o usuário
      int new_id = get_id(cdata->client_sock);
      printf("User %d added\n", new_id);

      // Envia a mensagem informando que o novo usuário entrou no grupo por
      // broadcast para todos os usuários
      msg_t ret_msg;
      memset(ret_msg.message, 0, BUFFER_SIZE);

      ret_msg.id_msg = MSG;
      ret_msg.id_sender = new_id;
      ret_msg.id_receiver = NULL_ID;
      sprintf(ret_msg.message, "User %d joined the group!", new_id);
      broadcast(&ret_msg, NULL_ID);

      // Aloca uma string que representa a lista de integrantes do grupo para o
      // conteúdo da mensagem
      memset(ret_msg.message, 0, strlen(ret_msg.message));
      get_user_list(ret_msg.message);

      pthread_mutex_unlock(cdata->mutex);

      // Envia a mensagem com a lista dos atuais integrantes do grupo para o
      // novo usuário
      ret_msg.id_msg = RES_LIST;
      ret_msg.id_sender = NULL_ID;
      ret_msg.id_receiver = NULL_ID;

      memset(buffer, 0, BUFFER_SIZE);
      encode(&ret_msg, buffer);

      if (send_msg(cdata->client_sock, buffer) != 0) {
        log_exit("send");
      }
    } else if (msg.id_msg == REQ_REM) {
      // As operações precisam ser feitas em exclusão mútua devido à atualização
      // das variáveis "active_sockets" e "user_count"
      pthread_mutex_lock(cdata->mutex);

      // Verifica se o usuário que solicitou o fechamento da conexão está na
      // lista de conexões ativas
      if (active_sockets[msg.id_sender] == -1) {
        error_msg(cdata->client_sock, msg.id_sender, 2);
      } else {
        printf("User %d removed\n", msg.id_sender);

        // Envia mensagem de confirmação para o usuário
        ok_msg(cdata->client_sock, msg.id_sender, 1);
        active_sockets[msg.id_sender] = -1;
        user_count--;

        broadcast(&msg, NULL_ID);
      }

      pthread_mutex_unlock(cdata->mutex);

      break;
    } else if (msg.id_msg == MSG) {
      if (msg.id_receiver == NULL_ID) { // Mensagem pública
        char time_str[7];
        set_time_str(time_str);
        // Imprime a mensagem recebida, com o timestamp
        printf("%s %d: %s\n", time_str, msg.id_sender, msg.message);

        // Faz o broadcast da mensagem
        pthread_mutex_lock(cdata->mutex);
        broadcast(&msg, msg.id_sender);
        pthread_mutex_unlock(cdata->mutex);

        // Altera a mensagem para ser enviada para o remetente
        char temp[BUFFER_SIZE] = "-> all ";
        strcat(temp, msg.message);
        strcpy(msg.message, temp);

        memset(buffer, 0, strlen(buffer));
        encode(&msg, buffer);

        // Envia a mensagem alterada para o usuário remetente
        if (send_msg(cdata->client_sock, buffer) != 0) {
          log_exit("recv");
        }
      } else { // Mensagem privada
        // Todo o tratamento da mensagem privada é feio em exclusão mútua para
        // garantir que o destinatário não possa ser marcado como inativo por
        // outra thread enquanto o tratamento é feito aqui
        pthread_mutex_lock(cdata->mutex);

        // Verifica se o ID do destinatário existe
        if (msg.id_receiver >= MAX_CLIENTS || msg.id_receiver < 0 ||
            active_sockets[msg.id_receiver] == -1) {
          printf("User %d not found\n", msg.id_receiver);
          error_msg(cdata->client_sock, msg.id_sender, 3);
        } else {
          memset(buffer, 0, strlen(buffer));
          encode(&msg, buffer);

          // Envia a mensagem para o destinatário
          if (send_msg(active_sockets[msg.id_receiver], buffer) != 0) {
            log_exit("recv");
          }

          // Envia a mensagem de confirmação para o remetente
          ok_msg(cdata->client_sock, msg.id_sender, 2);
        }

        pthread_mutex_unlock(cdata->mutex);
      }
    } else {
      // Caso para tratar uma mensagem malformada que tenha um ID inválido
      eprintf("Unknown message ID.");
      exit(EXIT_FAILURE);
    }
  }

  close(cdata->client_sock);
  free(cdata);
  pthread_exit(NULL);
}

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

  // A thread principal do programa continuamente aguarda por novas conexões
  while (1) {
    struct sockaddr_storage client_storage;
    struct sockaddr* client_addr = (struct sockaddr*)(&client_storage);
    socklen_t client_addrlen = sizeof(client_storage);

    int client_sock = accept(server_sock, client_addr, &client_addrlen);
    if (client_sock == -1) {
      log_exit("accept");
    }

    // Quando uma nova conexão é aceita, é criada uma nova thread para realizar
    // o processamento das mensagens associadas ao cliente dessa conexão
    server_thread_args* cdata = (server_thread_args*)malloc(sizeof(server_thread_args));
    cdata->client_sock = client_sock;
    cdata->mutex = &mutex;

    pthread_t thread_id;
    pthread_create(&thread_id, NULL, client_thread, (void*)cdata);
  }

  pthread_mutex_destroy(&mutex);
  close(server_sock);

  exit(EXIT_SUCCESS);
}
