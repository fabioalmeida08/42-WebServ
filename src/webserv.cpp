#include "../includes/webserv.hpp"
#include <cstdio>
#include <iostream>
#include <netdb.h>
#include <sys/socket.h>
// TEST: apenas para teste, o correto é preencher de acordo com o arquivo de
// config

WebServ::WebServ()
    : _port("8080"), _local_host("0.0.0.0"), _server_fd(-1), _opt(1),
      _backlog(128), _server_info(NULL), _getai_status(-1) {
  // TODO:
  // chamar a funcão do parser para preencher as portas aqui
  // a classe parser teria que ter uma funcao que preenche
  // as estruturas de acordo com o arquivo de config;
}
// WebServ WebServ(const WebServ &other);

// NOTE: metodos privados de inicializacao do server;
bool WebServ::setup_socket() {
  std::memset(&_hints, 0, sizeof(_hints));
  _hints.ai_family = AF_INET;
  _hints.ai_socktype = SOCK_STREAM;
  _hints.ai_flags = AI_PASSIVE;

  // NOTE: preencher a struct com as info importantes
  if ((_getai_status =
           getaddrinfo(NULL, _port.c_str(), &_hints, &_server_info)) != 0) {
    std::cerr << "getaddinfo : " << gai_strerror(_getai_status) << std::endl;
    return false;
  }

  // NOTE: criacao do socket;
  _server_fd = socket(_server_info->ai_family, _server_info->ai_socktype,
                      _server_info->ai_protocol);
  if (_server_fd < 0) {
    perror("Socket");
    cleanup_server();
    return false;
  }

  // NOTE: configurar opcoes do socket, basicamente falando que pode reutilizar
  // a porta namespace para evitar o erro chato de port already in use quando
  // reiniciar o server
  if (setsockopt(_server_fd, SOL_SOCKET, SO_REUSEADDR, &_opt, sizeof(_opt)) <
      0) {
    perror("setsockopt");
    cleanup_server();
    return false;
  }
  return true;
}

bool WebServ::setup_bind() {
  if (bind(_server_fd, _server_info->ai_addr, _server_info->ai_addrlen) < 0) {
    perror("bind");
    cleanup_server();
    return false;
  }
  cleanup_addrinfo();
  return true;
}

bool WebServ::setup_listen() {
  if (listen(_server_fd, _backlog) < 0) {
    perror("listen");
    cleanup_socket();
    return false;
  }
  return true;
}

bool WebServ::setup_server() {
  if (!setup_socket())
    return false;
  if (!setup_bind())
    return false;
  if (!setup_listen())
    return false;
  std::cout << "Server Aguardando conexões..." << std::endl;
  return true;
}

bool WebServ::run() {

  if (!setup_server())
    return false;

  while (1) {
    socklen_t _client_addr_size = sizeof(_client_addr);

    // NOTE: primeiro sleep ocorre aqui, após isso o kernel bota o
    // processo em sleep até receber uma conexao;
    int client_fd = accept(_server_fd, (struct sockaddr *)&_client_addr,
                           &_client_addr_size);

    if (client_fd < 0) {
      perror("accept");
      continue;
    }

    std::cout << "Cliente conectado" << std::endl;

    char buffer[4096];

    ssize_t bytes_rcv = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

    if (bytes_rcv < 0) {
      perror("recv");
      close(client_fd);
      continue;
    }

    buffer[bytes_rcv] = '\0';

    std::cout << "Requisição recebida:\n%" << buffer << std::endl;

    const char *body = "Ui DIDI HIHIHIH";

    char response[1024];

    snprintf(response, sizeof(response),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/plain\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "%s",
             strlen(body), body);

    send(client_fd, response, strlen(response), 0);

    close(client_fd);

    std::cout << "Cliente desconectado" << std::endl;
  }
  return true;
}

void WebServ::cleanup_addrinfo() {
  if (_server_info) {
    freeaddrinfo(_server_info);
    _server_info = NULL;
  }
}

void WebServ::cleanup_server() {
  cleanup_addrinfo();
  cleanup_socket();
}

void WebServ::cleanup_socket() {
  if (_server_fd >= 0) {
    close(_server_fd);
    _server_fd = -1;
  }
}
// TODO: forma canonica
//  WebServ::WebServ(std::string &config_file) {}
//  WebServ::WebServ(const WebServ &other) {}
//  // WebServ &WebServ::operator=(const WebServ &other) {}
//
WebServ::~WebServ() { cleanup_server(); }
