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
		int file_fd;
		size_t file_size;
		size_t file_sent;
		// Bloco lido do disco e ainda nao enviado por completo: sem isso
		// um send() parcial perde o resto do bloco
		std::string file_buffer;
		size_t file_buffer_pos;
		// Corpo do request que ainda falta escrever no stdin do CGI
		Cgi cgi;
		size_t cgi_body_sent;
		std::string remote_addr;
		Client() : sent(0), state(READING), last_activity(std::time(NULL)),
		           file_fd(-1), file_size(0), file_sent(0), file_buffer_pos(0),
		           cgi_body_sent(0) {}
	};

	std::vector<Server> _servers;
	std::vector<ListenSocket> _listen_sockets;
	std::map<int, int> _client_listen;
	std::map<int, int> _listen_fds;
	std::map<int, Client> _clients;
	// fd do pipe do CGI -> fd do cliente dono (o pipe nao e socket, mas
	// tambem precisa passar pelo poll)
	std::map<int, int> _cgi_fds;
	// Versao da ultima entrada criada para cada fd. O snapshot de fds
	// prontos guarda a versao; se um handler fechar o fd e o accept()
	// reutilizar o mesmo numero no mesmo tick, a versao nao bate e o
	// evento velho e descartado em vez de ser entregue ao cliente novo.
	std::map<int, unsigned long> _fd_epoch;
	unsigned long _fd_epoch_counter;

	int _reuse_addr;
	int _backlog;

	//NOTE: metodos privados que seram usados em conjunto com o poll
	std::vector<struct pollfd> _poll_fds;

	// Um request incompleto nao pode ficar preso para sempre (subject:
	// "A request to your server should never hang indefinitely")
	static const int REQUEST_TIMEOUT = 60;
	// Frequencia com que o poll acorda pra conferir os prazos acima
	static const int POLL_TICK_MS = 1000;

	void handle_new_connection(int listen_index);
	void handle_client_events(int client_fd, int index, short revents);
	bool handle_client_read(int client_fd, int index);
	// client_max_body_size pode ser sobrescrito por location; a location
	// so fica sabida depois que a linha de requisicao revela o path
	void apply_body_limit(Client &client, int client_fd);
	bool handle_client_write(int client_fd, int index);
	bool send_file_block(int client_fd, Client &client, int index);
	void close_file(Client &client);
	void start_cgi(int client_fd, int index, Client &client);
	void handle_cgi_event(int client_fd, int cgi_fd, short revents);
	// forced_status != 0 responde com esse erro no lugar da saida do script
	// (timeout -> 504, saida gigante -> 500)
	void finish_cgi(int client_fd, Client &client, int forced_status = 0);
	void remove_poll_fd(int fd);
	void check_timeouts();
	void close_client(int client_fd);
	int poll_index(int fd) const;
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
