#include "../includes/webserv.hpp"
#include <cstdio>
#include <iostream>
#include <netdb.h>
#include <sstream>
#include <sys/poll.h>
#include <sys/socket.h>

WebServ::WebServ() : _reuse_addr(1), _backlog(128) {}

void WebServ::loadConfig(const std::vector<Server> &servers) {
  _servers = servers;
}

bool WebServ::setup_listen_sockets() {
  std::vector<int> seen_ports;

  for (size_t i = 0; i < _servers.size(); i++) {
    int port = _servers[i].get_port();
    bool already_seen = false;
    for (size_t j = 0; j < seen_ports.size(); j++) {
      if (seen_ports[j] == port) {
        already_seen = true;
        break;
      }
    }
    if (already_seen)
      continue;
    seen_ports.push_back(port);

    ListenSocket listen_socket;
    listen_socket.fd = -1;
    listen_socket.port = port;
    for (size_t j = 0; j < _servers.size(); j++) {
      if (_servers[j].get_port() == port)
        listen_socket.server_indexes.push_back(static_cast<int>(j));
    }

    std::string host = _servers[listen_socket.server_indexes[0]].get_host();

    struct addrinfo hints;
    struct addrinfo *addr;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (host == "0.0.0.0")
      hints.ai_flags = AI_PASSIVE;

    std::ostringstream port_str;
    port_str << port;

    int gai_ret = getaddrinfo(host == "0.0.0.0" ? NULL : host.c_str(),
                              port_str.str().c_str(), &hints, &addr);
    if (gai_ret != 0) {
      std::cerr << "getaddrinfo: " << gai_strerror(gai_ret) << std::endl;
      continue;
    }

    struct addrinfo *ai;
    for (ai = addr; ai != NULL; ai = ai->ai_next) {
      int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
      if (fd < 0)
        continue;

      if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &_reuse_addr,
                     sizeof(_reuse_addr)) < 0) {
        close(fd);
        continue;
      }

      if (bind(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
        listen_socket.fd = fd;
        break;
      }

      close(fd);
    }

    freeaddrinfo(addr);

    if (listen_socket.fd < 0) {
      std::cerr << "warning: nao foi possivel bind na porta " << port
                << " — grupo ignorado" << std::endl;
      continue;
    }

    _listen_sockets.push_back(listen_socket);
  }

  if (_listen_sockets.empty()) {
    std::cerr << "nenhum socket de escuta foi criado" << std::endl;
    return false;
  }
  return true;
}

bool WebServ::setup_listen() {
  for (size_t i = 0; i < _listen_sockets.size(); i++) {
    if (listen(_listen_sockets[i].fd, _backlog) < 0) {
      perror("listen");
      cleanup_socket();
      return false;
    }
  }
  return true;
}

bool WebServ::setup_server() {
  if (!setup_listen_sockets())
    return false;
  if (!setup_listen())
    return false;
  std::cout << "Server Aguardando conexões em "
            << _listen_sockets.size() << " porta(s)..." << std::endl;
  return true;
}

int WebServ::find_listen_index(int fd) const {
  for (size_t i = 0; i < _listen_sockets.size(); i++) {
    if (_listen_sockets[i].fd == fd)
      return static_cast<int>(i);
  }
  return -1;
}

bool WebServ::run() {
  if (!setup_server())
    return false;

  for (size_t i = 0; i < _listen_sockets.size(); i++) {
    if (!set_non_blocking(_listen_sockets[i].fd))
      return false;
    add_poll_fd(_listen_sockets[i].fd, POLLIN);
  }

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

      int listen_index = find_listen_index(pfd.fd);
      if (listen_index >= 0) {
        handle_new_connection(listen_index);
      } else if (pfd.revents & (POLLHUP | POLLERR)) {
        _client_listen.erase(pfd.fd);
        close(pfd.fd);
        _poll_fds.erase(_poll_fds.begin() + i);
      } else if (pfd.revents & POLLIN)
        handle_client_read(pfd.fd, i);
    }
  }
  return true;
}

void WebServ::handle_new_connection(int listen_index) {
  struct sockaddr_storage client_addr;
  socklen_t client_addr_size = sizeof(client_addr);

  int client_fd =
      accept(_listen_sockets[listen_index].fd, (struct sockaddr *)&client_addr,
             &client_addr_size);
  if (client_fd < 0) {
    perror("accept");
    return; // não é fatal, só não aceitou essa conexão específica
  }

  if (!set_non_blocking(client_fd)) {
    close(client_fd);
    return;
  }

  _client_listen[client_fd] = listen_index;
  add_poll_fd(client_fd, POLLIN);

  std::cout << "Cliente conectado, fd " << client_fd << " na porta "
            << _listen_sockets[listen_index].port << std::endl;
}

void WebServ::handle_client_read(int client_fd, int index) {
  char buffer[4096];
  ssize_t bytes_rcv = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

  if (bytes_rcv <= 0) {
    // 0 = cliente fechou a conexão; -1 = erro — tratamos os dois igual,
    // sem checar errno, como o subject exige
    _client_listen.erase(client_fd);
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

  _client_listen.erase(client_fd);
  close(client_fd);
  _poll_fds.erase(_poll_fds.begin() + index);
}

void WebServ::cleanup_server() { cleanup_socket(); }

void WebServ::cleanup_socket() {
  for (size_t i = 0; i < _listen_sockets.size(); i++) {
    if (_listen_sockets[i].fd >= 0) {
      close(_listen_sockets[i].fd);
      _listen_sockets[i].fd = -1;
    }
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