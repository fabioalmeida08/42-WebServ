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
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdlib>
#include <poll.h>
#include <fcntl.h>
#include <vector>

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



    //NOTE: metodos privados que seram usados em conjunto com o poll
    std::vector<struct pollfd> _poll_fds;
    void handle_new_connection();
    void handle_client_read(int client_fd, int index);
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
    bool setup_socket();
    bool setup_bind();
    bool setup_listen();
    bool run();
    void cleanup_addrinfo();
    void cleanup_socket();
    void cleanup_server();


};

#endif
