#include "../includes/webserv.hpp"
#include <cstdio>
#include <iostream>
#include <netdb.h>
#include <sstream>
#include <sys/poll.h>
#include <sys/socket.h>

WebServ::WebServ()
    : _port("8080"), _server_fd(-1), _reuse_addr(1),
      _backlog(128), _addr(NULL), _gai_ret(-1) {}

void WebServ::loadConfig(const std::vector<Server> &servers) {
  _servers = servers;
  if (!_servers.empty()) {
    std::ostringstream oss;
    oss << _servers[0].get_port();
    _port = oss.str();
  }
}

bool WebServ::setup_socket_bind() {
  std::memset(&_hints, 0, sizeof(_hints));
  _hints.ai_family = AF_INET;
  _hints.ai_socktype = SOCK_STREAM;
  _hints.ai_flags = AI_PASSIVE;

  if ((_gai_ret =
           getaddrinfo(NULL, _port.c_str(), &_hints, &_addr)) != 0) {
    std::cerr << "getaddrinfo: " << gai_strerror(_gai_ret) << std::endl;
    return false;
  }

  struct addrinfo *ai;
  for (ai = _addr; ai != NULL; ai = ai->ai_next) {
    _server_fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (_server_fd < 0)
      continue;

    if (setsockopt(_server_fd, SOL_SOCKET, SO_REUSEADDR, &_reuse_addr, sizeof(_reuse_addr)) <
        0) {
      close(_server_fd);
      _server_fd = -1;
      continue;
    }

    if (bind(_server_fd, ai->ai_addr, ai->ai_addrlen) == 0)
      break;

    close(_server_fd);
    _server_fd = -1;
  }

  cleanup_addrinfo();

  if (_server_fd < 0) {
    std::cerr << "Falha ao bind em nenhum endereço" << std::endl;
    return false;
  }
  return true;
}

bool WebServ::setup_listen() {
  // NOTE: listen serve para escutar no IP:PORTA e cada requisição é colocado
  // numa fila. coloca o socket em modo de escuta, novas conexões TCP ficam na
  // fila de espera até serem aceitas pelo accept
  if (listen(_server_fd, _backlog) < 0) {
    perror("listen");
    cleanup_socket();
    return false;
  }
  return true;
}

bool WebServ::setup_server() {
  if (!setup_socket_bind())
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
  while (1) {
    int ready = poll(_poll_fds.data(), _poll_fds.size(), -1);

    if (ready < 0) {
      perror("poll");
      break;
    }

    for (int i = static_cast<int>(_poll_fds.size()) - 1; i >= 0; i--) {
      struct pollfd &pfd = _poll_fds[i];

      if (pfd.revents == 0)
        continue;
      if (pfd.fd == _server_fd) {
        handle_new_connection();
      } else if (pfd.revents & (POLLHUP | POLLERR)) {
        close(pfd.fd);
        _poll_fds.erase(_poll_fds.begin() + i);
      } else if (pfd.revents & POLLIN)
        handle_client_read(pfd.fd, i);
    }
  }
  return true;
}

void WebServ::handle_new_connection() {
  struct sockaddr_storage client_addr;
  socklen_t client_addr_size = sizeof(client_addr);

  int client_fd =
      accept(_server_fd, (struct sockaddr *)&client_addr, &client_addr_size);
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
           "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "
           "%zu\r\n\r\n%s",
           strlen(body), body);

  send(client_fd, response, strlen(response), 0);

  close(client_fd);
  _poll_fds.erase(_poll_fds.begin() + index);
}

void WebServ::cleanup_addrinfo() {
  if (_addr) {
    freeaddrinfo(_addr);
    _addr = NULL;
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
