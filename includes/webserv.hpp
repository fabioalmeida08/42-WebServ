/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   webserv.hpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lbento <lbento@student.42.fr>              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/07/27 12:03:51 by fabio             #+#    #+#             */
/*   Updated: 2026/09/26 23:10:47 by lbento           ###   ########.fr       */
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
#include <ctime>
#include <poll.h>
#include <fcntl.h>
#include <vector>

#include "../includes/server.hpp"
#include "../includes/request_parser.hpp"
#include "../includes/response.hpp"
#include "../includes/router.hpp"

class WebServ {
	private:
		struct ListenSocket {
			int fd;
			int port;
			std::vector<int> server_indexes;
		};

		// NOTE: o cliente e uma maquina de estados; o CGI vai adicionar
		// CGI_RUNNING e o envio de arquivo grande SENDING_FILE
		enum ClientState {
			READING,
			WRITING_RESPONSE,
			SENDING_FILE,
			CGI_RUNNING,
			CLOSING
		};

	struct Client
	{
		RequestParser parser;
		std::string response;
		size_t sent;
		ClientState state;
		time_t last_activity;
		Client() : sent(0), state(READING), last_activity(std::time(NULL)) {}
	};

	std::vector<Server> _servers;
	std::vector<ListenSocket> _listen_sockets;
	std::map<int, int> _client_listen;
	std::map<int, int> _listen_fds;
	std::map<int, Client> _clients;

	int _reuse_addr;
	int _backlog;

	//NOTE: metodos privados que seram usados em conjunto com o poll
	std::vector<struct pollfd> _poll_fds;

	// Um request incompleto nao pode ficar preso para sempre (subject:
	// "A request to your server should never hang indefinitely")
	static const int REQUEST_TIMEOUT = 60;

	void handle_new_connection(int listen_index);
	void handle_client_events(int client_fd, int index, short revents);
	bool handle_client_read(int client_fd, int index);
	bool handle_client_write(int client_fd, int index);
	void check_timeouts();
	void close_client(int client_fd, int index);
	const Server &get_client_server(int client_fd);
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
