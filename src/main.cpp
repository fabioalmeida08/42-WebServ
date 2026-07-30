#include "../includes/webserv.hpp"

int main (int argc, char *argv[]) {
  (void)argv;
  (void)argc;

  WebServ server;

  server.run();
  return 0;
}
