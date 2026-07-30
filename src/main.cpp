#include "../includes/webserv.hpp"

int main (int argc, char *argv[]) {
  (void)argv;
  (void)argc;
  //TODO: ter o metodo do parse que preenche a classe
  //com as config de um arquivo default, ou do arquivo 
  //do argv[1];

  WebServ server;

  server.run();
  return 0;
}
