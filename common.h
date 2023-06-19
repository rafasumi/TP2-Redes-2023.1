#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>

// Macro usada para imprimir no stderr.
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

// Estrutura de dados usada para representar uma mensagem.
typedef struct msg_t {
  // ID da mensagem
  unsigned int id_msg;

  // ID do remetente
  int id_sender;

  // ID do destinatário
  int id_receiver;

  // Contéudo da mensagem
  char message[BUFFER_SIZE];
} msg_t;

// Função auxiliar usada para verificar se uma string representa um número
// inteiro válido.
int is_number(const char* str, size_t len);

// Função auxiliar usada para gerar uma string de timestamp no formato "[HH:MM]".
void set_time_str(char* time_str);

// Faz a codificação de uma mensagem no formato de estrutura de dados para uma
// string. A codificação é feita usando um separador pré-defindo para separar
// os diferentes atributos da estrutura de dados. Retorna o tamanho da string
// resultante.
int encode(const msg_t* msg, char* outBuf);

// Faz a decodificação de uma mensagem em formato de string para o formato de
// estrutura de dados. Retorna 1 caso a decodificação tenha sido bem sucedida e
// 0 caso contrário.
int decode(msg_t* msg, char* inBuf);

// Função auxiliar usada para enviar uma mensagem em um socket. Retorna -1 caso
// o envio tenha sido bem sucedido e 0 caso contrário.
int send_msg(int socket, const char* buffer);

// Função auxiliar usada para receber uma mensagem em um socket. Retorna 1 caso
// o envio tenha sido bem sucedido.
size_t recv_msg(int socket, char* buffer);

// Retirado das aulas do professor Ítalo.
void log_exit(const char* msg);

// Função usada para reportar um erro de parsing e depois finalizar a execução
// do programa.
void parse_error();

#endif
