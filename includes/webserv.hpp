#ifndef WEBSERV_HPP
#define WEBSERV_HPP


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
#include "../includes/RequestParser.hpp"
#include "../includes/response.hpp"
#include "../includes/router.hpp"

class WebServ {
	private:
		struct ListenSocket {
			int fd;
			int port;
			std::vector<int> server_indexes;
		};

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
		std::string file_buffer;
		size_t file_buffer_pos;
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
	std::map<int, int> _cgi_fds;
	std::map<int, unsigned long> _fd_epoch;
	unsigned long _fd_epoch_counter;

	int _reuse_addr;
	int _backlog;

	std::vector<struct pollfd> _poll_fds;

	static const int REQUEST_TIMEOUT = 60;
	static const int POLL_TICK_MS = 1000;

	void handle_new_connection(int listen_index);
	void handle_client_events(int client_fd, int index, short revents);
	bool handle_client_read(int client_fd, int index);
	void apply_body_limit(Client &client, int client_fd);
	bool handle_client_write(int client_fd, int index);
	bool send_file_block(int client_fd, Client &client, int index);
	void close_file(Client &client);
	void start_cgi(int client_fd, int index, Client &client);
	void handle_cgi_event(int client_fd, int cgi_fd, short revents);
	void finish_cgi(int client_fd, Client &client, int forced_status = 0);
	void remove_poll_fd(int fd);
	void check_timeouts();
	void close_client(int client_fd);
	int poll_index(int fd) const;
	const Server &get_client_server(int client_fd);
	int find_listen_index(int fd) const;
	bool set_non_blocking(int fd);
	void add_poll_fd(int fd, short events);

		WebServ(const WebServ &other);
		WebServ &operator=(const WebServ &other);
	public:
		WebServ();
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
