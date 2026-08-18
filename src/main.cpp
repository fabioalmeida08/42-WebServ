#include <iostream>
#include <stdexcept>

#include "../includes/parser.hpp"
#include "../includes/webserv.hpp"

int main(int argc, char *argv[]) {
  try {
    std::vector<Server> servers;
    if (argc == 2)
      servers = Parser(argv[1]).get_servers();
    else
      servers = Parser("default.conf").get_servers();

    WebServ server;
    server.loadConfig(servers);
    server.run();
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
  return 0;
}
