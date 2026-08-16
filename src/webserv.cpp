#include <cstdio>
#include <iostream>
#include <netdb.h>
#include "../includes/webserv.hpp"
#include <sys/poll.h>
#include <sys/socket.h>

WebServ::WebServ()
    : _port("8080"), _local_host("0.0.0.0"), _server_fd(-1), _opt(1),
      _backlog(128), _server_info(NULL), _getai_status(-1) {
}

// NOTE: metodos privados de inicializacao do server;
bool WebServ::setup_socket() {
  //NOTE: limpa o struct do lixo de memoria, preenchendo ela com 0's
  std::memset(&_hints, 0, sizeof(_hints));
  _hints.ai_family = AF_INET; //familia de ip que vai ser usado AF_INET = IPV4
  _hints.ai_socktype = SOCK_STREAM; // tipo do socket,  SOCK_STREAM = protocolo TCP
  _hints.ai_flags = AI_PASSIVE; // IP CURINGA "0.0.0.0" 
                                // que faz o kernel aceitar conexões em qualquer interface de rede disponivel

  // NOTE: preencher a struct server_info com as infos importantes configuradas no hints
  if ((_getai_status =
           getaddrinfo(NULL, _port.c_str(), &_hints, &_server_info)) != 0) {
    std::cerr << "getaddinfo : " << gai_strerror(_getai_status) << std::endl;
    return false;
  }

  // NOTE: criacao do socket usando a struct que é preenchida por geataddrinfo;
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
  //NOTE: bind serve para ligar o ip a uma porta(IP:PORT), aonde o socket vai escutar
  //após o bind o servidor possui esse endereco
  if (bind(_server_fd, _server_info->ai_addr, _server_info->ai_addrlen) < 0) {
    perror("bind");
    cleanup_server();
    return false;
  }
  cleanup_addrinfo();
  return true;
}

bool WebServ::setup_listen() {
  //NOTE: listen serve para escutar no IP:PORTA e cada requisição é colocado numa fila.
  //coloca o socket em modo de escuta, novas conexões TCP ficam na fila de espera até serem aceitas pelo
  //accept
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

  if (!set_non_blocking(_server_fd))
    return false;


  add_poll_fd(_server_fd, POLLIN);
  //NOTE: loop principal do programa por enquanto atende a um cliente por vez;
  while (1) {
    int ready = poll(_poll_fds.data(), _poll_fds.size(), -1);

    if (ready < 0)
    {
      perror("poll");
      break;
    }

    for (int i = static_cast<int>(_poll_fds.size()) -1; i >= 0; i--)
    {
      struct pollfd &pfd = _poll_fds[i];

      if (pfd.revents == 0)
        continue;
      if (pfd.fd == _server_fd)
      {
        handle_new_connection();
      } else if (pfd.revents & (POLLHUP | POLLERR))
      {
        close(pfd.fd);
        _poll_fds.erase(_poll_fds.begin() + i);
      } else if (pfd.revents & POLLIN)
      {
        handle_client_read(pfd.fd, i);
      }
    }
  }
  return true;
}

void WebServ::handle_new_connection() {
  struct sockaddr_storage client_addr;
  socklen_t client_addr_size = sizeof(client_addr);

  int client_fd = accept(_server_fd, (struct sockaddr *)&client_addr, &client_addr_size);
  if (client_fd < 0) {
    perror("accept");
    return; // não é fatal, só não aceitou essa conexão específica
  }

  if (!set_non_blocking(client_fd)) {
    close(client_fd);
    return;
  }

  add_poll_fd(client_fd, POLLIN);

  std::cout << "Cliente conectado, fd " << client_fd << std::endl;
}

void WebServ::handle_client_read(int client_fd, int index) {
  char buffer[4096];
  ssize_t bytes_rcv = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

  if (bytes_rcv <= 0) {
    // 0 = cliente fechou a conexão; -1 = erro — tratamos os dois igual,
    // sem checar errno, como o subject exige
    close(client_fd);
    _poll_fds.erase(_poll_fds.begin() + index);
    return;
  }

  buffer[bytes_rcv] = '\0';
  std::cout << "Requisição recebida:\n" << buffer << std::endl;

  const char *body = "Ui DIDI HIHIHIH";
  char response[1024];
  snprintf(response, sizeof(response),
           "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\n\r\n%s",
           strlen(body), body);

  send(client_fd, response, strlen(response), 0);

  close(client_fd);
  _poll_fds.erase(_poll_fds.begin() + index);
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

bool WebServ::set_non_blocking(int fd) {
  if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
    perror("fcntl");
    return false;
  }
  return true;
}

void WebServ::add_poll_fd(int fd, short events) {
  struct pollfd pollfd;
  pollfd.fd = fd;
  pollfd.events = events;
  pollfd.revents = 0;
  _poll_fds.push_back(pollfd);
}

WebServ::~WebServ() { cleanup_server(); }
