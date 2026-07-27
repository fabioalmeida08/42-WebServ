#include "../includes/webserv.hpp"

int main (int argc, char *argv[]) {
  (void)argv;
  (void)argc;

  WebServ server;

  server.start();
  
  return 0;
}
