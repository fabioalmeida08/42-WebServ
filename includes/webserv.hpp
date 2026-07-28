/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   webserv.hpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: fabio </var/spool/mail/fabio>              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/07/27 12:03:51 by fabio             #+#    #+#             */
/*   Updated: 2026/07/27 12:14:21 by fabio            ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef WEBSERV_HPP
#define WEBSERV_HPP

// #define OPT 1

#include <netdb.h>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdlib>


class WebServ {
  private:
    std::string _port;
    std::string _local_host;
    int _server_fd;
    int _opt;
    int _backlog;
    struct addrinfo _hints;
    struct addrinfo *_server_info;
    int _getai_status;
    struct sockaddr_storage _client_addr;
  public:
    //TODO: aqui ele vai inciar com a config default do arquivo padrao
    WebServ();
    //TODO: caso receba um arquivo de configuracao, 
    //dentro desse método  tera a classe parser que ira validar e configurar o arquivo
    //de config
    // WebServ(std::string &config_file);
    // WebServ(const WebServ &other);
    // WebServ &operator=(const WebServ &other);
    ~WebServ();

    bool setup_server();
    bool setup_socket();
    bool setup_bind();
    bool setup_listen();
    bool run();
    //TODO: funcao de clean e close fd quando algum erro acontecer 
    //na hora do setup


};

#endif
