#include "../includes/server.hpp"

Server::Server()
    : _port(8080), _host("0.0.0.0"), _client_max_body(1024 * 1024) {}

int Server::get_port() const { return _port; }

const std::string &Server::get_host() const { return _host; }

const std::vector<std::string> &Server::get_names() const { return _names; }

const std::map<int, std::string> &Server::get_error_pages() const {
  return _error_pages;
}

size_t Server::get_client_max_body() const { return _client_max_body; }

const std::vector<Location> &Server::get_locations() const {
  return _locations;
}

void Server::set_port(int port) { _port = port; }

void Server::set_host(const std::string &host) { _host = host; }

void Server::add_name(const std::string &name) { _names.push_back(name); }

void Server::set_error_page(int code, const std::string &path) {
  _error_pages[code] = path;
}

void Server::set_client_max_body(size_t max_body) {
  _client_max_body = max_body;
}

void Server::add_location(const Location &location) {
  _locations.push_back(location);
}
