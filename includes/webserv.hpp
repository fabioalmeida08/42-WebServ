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

#include <map>
#include <netdb.h>
#include <string>
#include <cstring>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdlib>
#include <poll.h>
#include <fcntl.h>
#include <vector>

#include "../includes/server.hpp"

class WebServ {
  private:
    struct ListenSocket {
      int fd;
      int port;
      std::vector<int> server_indexes;
    };

    std::vector<Server> _servers;
    std::vector<ListenSocket> _listen_sockets;
    std::map<int, int> _client_listen;

    int _reuse_addr;
    int _backlog;



    //NOTE: metodos privados que seram usados em conjunto com o poll
    std::vector<struct pollfd> _poll_fds;
    void handle_new_connection(int listen_index);
    void handle_client_read(int client_fd, int index);
    int find_listen_index(int fd) const;
    bool set_non_blocking(int fd);
    void add_poll_fd(int fd, short events);

    //NOTE: declarado aqui mas não implementado porque não queremos uma
    //cópia do webserv, deve existir somente um
    WebServ(const WebServ &other);
    WebServ &operator=(const WebServ &other);
  public:
    WebServ();
    //TODO: caso receba um arquivo de configuracao, 
    //dentro desse método  tera a classe parser que ira validar e configurar o arquivo
    //de config
    // WebServ(std::string &config_file);
    ~WebServ();

    bool setup_server();
    bool setup_listen_sockets();
    bool setup_listen();
    bool run();
    void loadConfig(const std::vector<Server> &servers);
    void cleanup_socket();
    void cleanup_server();


};

#endif
