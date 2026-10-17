#include "../includes/webserv.hpp"
#include <arpa/inet.h>
#include <cstdio>
#include <iostream>
#include <netdb.h>
#include <sstream>
#include <sys/poll.h>
#include <sys/socket.h>
#include <fstream>
#include <bits/stdc++.h>
#include <csignal>

namespace {
struct ReadyFd {
	int fd;
	short revents;
	unsigned long epoch;
};

volatile sig_atomic_t g_shutdown = 0;

void handle_shutdown_signal(int) {
	g_shutdown = 1;
}
}

WebServ::WebServ() : _fd_epoch_counter(0), _reuse_addr(1), _backlog(511) {}

void WebServ::loadConfig(const std::vector<Server> &servers) {
	_servers = servers;
}

bool WebServ::setup_listen_sockets() {
	std::vector<int> seen_ports;

	for (size_t i = 0; i < _servers.size(); i++) {
		int port = _servers[i].get_port();
		bool already_seen = false;
		for (size_t j = 0; j < seen_ports.size(); j++) {
			if (seen_ports[j] == port) {
				already_seen = true;
				break;
			}
		}
		if (already_seen) {
			const std::vector<std::string> &names = _servers[i].get_names();
			std::cerr << "error: port " << port << " is declared by more "
								<< "than one server block; vhost is out of scope, "
								<< "so this configuration does not work (block \""
								<< (names.empty() ? std::string("(unnamed)")
								                 : names[0])
								<< "\")" << std::endl;
			cleanup_socket();
			return false;
		}
		seen_ports.push_back(port);

		ListenSocket listen_socket;
		listen_socket.fd = -1;
		listen_socket.port = port;
		for (size_t j = 0; j < _servers.size(); j++) {
			if (_servers[j].get_port() == port)
				listen_socket.server_indexes.push_back(static_cast<int>(j));
		}

		std::string host = _servers[listen_socket.server_indexes[0]].get_host();

		struct addrinfo hints;
		struct addrinfo *addr;
		std::memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		if (host == "0.0.0.0")
			hints.ai_flags = AI_PASSIVE;

		std::ostringstream port_str;
		port_str << port;

		int gai_ret = getaddrinfo(host == "0.0.0.0" ? NULL : host.c_str(),
															port_str.str().c_str(), &hints, &addr);
		if (gai_ret != 0) {
			std::cerr << "getaddrinfo: " << gai_strerror(gai_ret) << std::endl;
			continue;
		}

		struct addrinfo *ai;
		for (ai = addr; ai != NULL; ai = ai->ai_next) {
			int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
			if (fd < 0)
				continue;

			if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &_reuse_addr,
										 sizeof(_reuse_addr)) < 0) {
				close(fd);
				continue;
			}

			if (bind(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
				listen_socket.fd = fd;
				break;
			}

			close(fd);
		}

		freeaddrinfo(addr);

		if (listen_socket.fd < 0) {
			std::cerr << "error: could not bind to port " << port
								<< " (port already in use or permission denied)" << std::endl;
			cleanup_socket();
			return false;
		}

		_listen_sockets.push_back(listen_socket);
		_listen_fds[listen_socket.fd] = static_cast<int>(_listen_sockets.size()) - 1;
	}

	if (_listen_sockets.empty()) {
		std::cerr << "no listening socket was created" << std::endl;
		return false;
	}
	return true;
}

bool WebServ::setup_listen() {
	for (size_t i = 0; i < _listen_sockets.size(); i++) {
		if (listen(_listen_sockets[i].fd, _backlog) < 0) {
			perror("listen");
			cleanup_socket();
			return false;
		}
	}
	return true;
}

bool WebServ::setup_server() {
	if (!setup_listen_sockets())
		return false;
	if (!setup_listen())
		return false;
	std::cout << "Server listening on "
						<< _listen_sockets.size() << " port(s)..." << std::endl;
	return true;
}

int WebServ::find_listen_index(int fd) const {
	std::map<int, int>::const_iterator it = _listen_fds.find(fd);
	if (it == _listen_fds.end())
		return -1;
	return it->second;
}

bool WebServ::run() {
	if (!setup_server())
		return false;

	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, handle_shutdown_signal);
	signal(SIGTERM, handle_shutdown_signal);

	for (size_t i = 0; i < _listen_sockets.size(); i++) {
		if (!set_non_blocking(_listen_sockets[i].fd))
			return false;
		add_poll_fd(_listen_sockets[i].fd, POLLIN);
	}

	while (g_shutdown == 0) {
		int ready = poll(_poll_fds.data(), _poll_fds.size(), POLL_TICK_MS);

		if (ready < 0) {
			if (g_shutdown == 0)
				perror("poll");
			break;
		}

		check_timeouts();

		std::vector<ReadyFd> ready_fds;
		ready_fds.reserve(_poll_fds.size());
		for (size_t k = 0; k < _poll_fds.size(); k++) {
			if (_poll_fds[k].revents == 0)
				continue;
			ReadyFd item;
			item.fd = _poll_fds[k].fd;
			item.revents = _poll_fds[k].revents;
			item.epoch = _fd_epoch[item.fd];
			ready_fds.push_back(item);
		}

		for (size_t k = 0; k < ready_fds.size(); k++) {
			int fd = ready_fds[k].fd;
			short revents = ready_fds[k].revents;

			if (_fd_epoch[fd] != ready_fds[k].epoch)
				continue;
			int index = poll_index(fd);
			if (index < 0)
				continue;

			if (find_listen_index(fd) >= 0) {
				handle_new_connection(find_listen_index(fd));
				continue;
			}
			std::map<int, int>::iterator cgi_it = _cgi_fds.find(fd);
			if (cgi_it != _cgi_fds.end()) {
				handle_cgi_event(cgi_it->second, fd, revents);
				continue;
			}
			if (_clients.find(fd) == _clients.end())
				continue;
			handle_client_events(fd, index, revents);
		}
	}
	std::vector<int> sobrando;
	for (std::map<int, Client>::iterator it = _clients.begin();
			 it != _clients.end(); ++it)
		sobrando.push_back(it->first);
	for (size_t i = 0; i < sobrando.size(); i++)
		close_client(sobrando[i]);
	cleanup_socket();
	return true;
}

int WebServ::poll_index(int fd) const {
	for (size_t k = 0; k < _poll_fds.size(); k++) {
		if (_poll_fds[k].fd == fd)
			return static_cast<int>(k);
	}
	return -1;
}

void WebServ::handle_new_connection(int listen_index) {
	for (int accepted = 0; accepted < 64; accepted++) {
		struct sockaddr_storage client_addr;
		socklen_t client_addr_size = sizeof(client_addr);

		int client_fd =
				accept(_listen_sockets[listen_index].fd,
				       (struct sockaddr *)&client_addr, &client_addr_size);
		if (client_fd < 0)
			break;

		if (!set_non_blocking(client_fd)) {
			close(client_fd);
			continue;
		}

		_client_listen[client_fd] = listen_index;
		_clients[client_fd].parser.set_max_body_size(
				get_client_server(client_fd).get_client_max_body());
		{
			char addr[INET_ADDRSTRLEN];
			struct sockaddr_in *sin =
					reinterpret_cast<struct sockaddr_in *>(&client_addr);
			if (inet_ntop(AF_INET, &sin->sin_addr, addr, sizeof(addr)) != NULL)
				_clients[client_fd].remote_addr = addr;
		}
		add_poll_fd(client_fd, POLLIN);

		std::cout << "Client connected, fd " << client_fd << " on port "
							<< _listen_sockets[listen_index].port << std::endl;
	}
}

void WebServ::handle_client_events(int client_fd, int index, short revents) {
	if (revents & POLLNVAL) {
		close_client(client_fd);
		return;
	}
	if ((revents & POLLIN) && !handle_client_read(client_fd, index))
		return;
	if (_clients.find(client_fd) == _clients.end())
		return;

	ClientState state = _clients[client_fd].state;
	if ((revents & POLLOUT)
			&& (state == WRITING_RESPONSE || state == SENDING_FILE)) {
		int out_index = poll_index(client_fd);
		if (out_index >= 0)
			handle_client_write(client_fd, out_index);
	}
	if (_clients.find(client_fd) == _clients.end())
		return;
	if (revents & (POLLHUP | POLLERR))
		close_client(client_fd);
}

void WebServ::apply_body_limit(Client &client, int client_fd) {
	const std::string &path = client.parser.get_request().get_path();

	if (path.empty())
		return;
	const Server &server = get_client_server(client_fd);
	const Location *loc = Location::match(path, server.get_locations());

	if (loc != NULL && loc->get_max_body_size() > 0)
		client.parser.set_max_body_size(loc->get_max_body_size());
	else
		client.parser.set_max_body_size(server.get_client_max_body());
}

bool WebServ::handle_client_read(int client_fd, int index) {
	char buffer[4096];
	ssize_t bytes_rcv = recv(client_fd, buffer, sizeof(buffer), 0);

	if (bytes_rcv == 0) {
		close_client(client_fd);
		return false;
	}
	if (bytes_rcv < 0) {
		return true;
	}

	Client &client = _clients[client_fd];
	client.last_activity = std::time(NULL);
	try {
		apply_body_limit(client, client_fd);
		client.parser.feed(buffer, static_cast<size_t>(bytes_rcv));
		apply_body_limit(client, client_fd);
		if (!client.parser.is_done())
			return true;

		const Server &server = get_client_server(client_fd);
		Response response;
		if (client.parser.has_error()) {
			response = Response::error(client.parser.get_error_code(), server);
			std::cout << "fd " << client_fd << ": invalid request -> "
								<< response.get_status() << std::endl;
		} else {
			Request &request = client.parser.get_request();
			Router router;
			response = router.handle_request(request, server, &client.cgi);
			if (request.get_method() == "HEAD")
				response.set_no_body(true);
			std::cout << "fd " << client_fd << ": " << request.get_method() << " "
								<< request.get_uri() << " -> ";
			if (response.get_is_cgi())
				std::cout << "CGI started";
			else
				std::cout << response.get_status();
			std::cout << std::endl;
			if (response.get_is_cgi()) {
				client.cgi_body_sent = 0;
				client.state = CGI_RUNNING;
				client.last_activity = std::time(NULL);
				if (client.cgi.in_fd() >= 0) {
					add_poll_fd(client.cgi.in_fd(), POLLOUT);
					_cgi_fds[client.cgi.in_fd()] = client_fd;
				}
				if (client.cgi.out_fd() >= 0) {
					add_poll_fd(client.cgi.out_fd(), POLLIN);
					_cgi_fds[client.cgi.out_fd()] = client_fd;
				}
				return true;
			}
		}
		response.set_header("Connection", "close");
		client.response = response.build();
		if (response.has_file() && !response.get_no_body()) {
			int fd = open(response.get_file_path().c_str(), O_RDONLY);
			if (fd < 0) {
				client.response = Response::error(500, server).build();
			} else if (!set_non_blocking(fd)) {
				close(fd);
				client.response = Response::error(500, server).build();
			} else {
				client.file_fd = fd;
				client.file_size = response.get_file_size();
				client.file_sent = 0;
			}
		}
	} catch (const std::exception &e) {
		std::cerr << "fd " << client_fd << ": " << e.what() << std::endl;
		close_client(client_fd);
		return false;
	}
	client.state = WRITING_RESPONSE;
	_poll_fds[index].events = POLLOUT;
	return true;
}

bool WebServ::handle_client_write(int client_fd, int index) {
	Client &client = _clients[client_fd];

	if (client.sent < client.response.size()) {
		ssize_t bytes_sent =
				send(client_fd, client.response.data() + client.sent,
						 client.response.size() - client.sent, 0);

		if (bytes_sent <= 0) {
			_poll_fds[index].events = POLLOUT;
			return true;
		}
		client.last_activity = std::time(NULL);
		client.sent += static_cast<size_t>(bytes_sent);
		_poll_fds[index].events = POLLOUT;
		return true;
	}
	if (client.file_fd >= 0) {
		client.state = SENDING_FILE;
		return send_file_block(client_fd, client, index);
	}
	close_client(client_fd);
	return false;
}

bool WebServ::send_file_block(int client_fd, Client &client, int index) {
	static const size_t FILE_CHUNK = 64 * 1024;

	if (client.file_sent >= client.file_size) {
		close_client(client_fd);
		return false;
	}
	if (client.file_buffer_pos >= client.file_buffer.size()) {
		size_t remaining = client.file_size - client.file_sent;
		size_t wanted = (remaining < FILE_CHUNK) ? remaining : FILE_CHUNK;

		client.file_buffer.resize(wanted);
		ssize_t bytes_read = read(client.file_fd, &client.file_buffer[0], wanted);

		if (bytes_read <= 0) {
			close_client(client_fd);
			return false;
		}
		client.file_buffer.resize(static_cast<size_t>(bytes_read));
		client.file_buffer_pos = 0;
		_poll_fds[index].events = POLLOUT;
		return true;
	}
	ssize_t bytes_sent = send(client_fd,
														&client.file_buffer[client.file_buffer_pos],
														client.file_buffer.size() - client.file_buffer_pos, 0);
	if (bytes_sent <= 0) {
		_poll_fds[index].events = POLLOUT;
		return true;
	}
	client.last_activity = std::time(NULL);
	client.file_buffer_pos += static_cast<size_t>(bytes_sent);
	client.file_sent += static_cast<size_t>(bytes_sent);
	_poll_fds[index].events = POLLOUT;
	return true;
}

void WebServ::close_file(Client &client) {
	if (client.file_fd >= 0) {
		close(client.file_fd);
		client.file_fd = -1;
	}
}

void WebServ::check_timeouts() {
	time_t now = std::time(NULL);

	std::vector<int> fds;
	fds.reserve(_poll_fds.size());
	for (size_t k = 0; k < _poll_fds.size(); k++) {
		if (_clients.find(_poll_fds[k].fd) != _clients.end())
			fds.push_back(_poll_fds[k].fd);
	}

	for (size_t k = 0; k < fds.size(); k++) {
		int client_fd = fds[k];
		std::map<int, Client>::iterator it = _clients.find(client_fd);
		if (it == _clients.end())
			continue;

		Client &client = it->second;
		int limit = (client.state == CGI_RUNNING) ? Cgi::TIMEOUT
		                                          : REQUEST_TIMEOUT;
		if (now - client.last_activity < limit)
			continue;

		if (client.state == CGI_RUNNING) {
			std::cout << "fd " << client_fd << ": no CGI response after "
								<< Cgi::TIMEOUT << "s -> 504" << std::endl;
			finish_cgi(client_fd, client, 504);
			continue;
		}
		if (client.state != READING) {
			std::cout << "fd " << client_fd << ": no send progress after "
								<< REQUEST_TIMEOUT << "s -> closing" << std::endl;
			close_client(client_fd);
			continue;
		}

		Response timeout = Response::error(408, get_client_server(client_fd));

		std::cout << "fd " << client_fd << ": timeout after "
							<< REQUEST_TIMEOUT << "s -> 408" << std::endl;
		timeout.set_header("Connection", "close");
		client.response = timeout.build();
		client.sent = 0;
		client.last_activity = now;
		client.state = WRITING_RESPONSE;
		int index = poll_index(client_fd);
		if (index >= 0)
			_poll_fds[index].events = POLLOUT;
	}
}

void WebServ::handle_cgi_event(int client_fd, int cgi_fd, short revents) {
	std::map<int, Client>::iterator it = _clients.find(client_fd);
	if (it == _clients.end()) {
		remove_poll_fd(cgi_fd);
		_cgi_fds.erase(cgi_fd);
		return;
	}
	Client &client = it->second;

	if (cgi_fd != client.cgi.in_fd() && cgi_fd != client.cgi.out_fd()) {
		remove_poll_fd(cgi_fd);
		_cgi_fds.erase(cgi_fd);
		return;
	}

	if (cgi_fd == client.cgi.in_fd()) {
		if (revents & (POLLHUP | POLLERR | POLLNVAL)) {
			client.cgi.close_stdin();
		} else {
			client.cgi.feed_body(client.parser.get_request().get_body(),
													 client.cgi_body_sent);
		}
		if (client.cgi.in_fd() < 0) {
			remove_poll_fd(cgi_fd);
			_cgi_fds.erase(cgi_fd);
		}
		client.last_activity = std::time(NULL);
		return;
	}
	if (revents & POLLIN) {
		char buffer[4096];
		ssize_t bytes_rcv = read(cgi_fd, buffer, sizeof(buffer));

		if (bytes_rcv > 0) {
			client.cgi.collect(buffer, static_cast<size_t>(bytes_rcv));
			client.last_activity = std::time(NULL);
			if (client.cgi.buffer_full()) {
				std::cerr << "fd " << client_fd << ": CGI output exceeded the "
									<< "allowed limit -> 500" << std::endl;
				finish_cgi(client_fd, client, 500);
			}
			return;
		}
		if (bytes_rcv == 0) {
			client.cgi.mark_eof();
			client.cgi.finish();
			finish_cgi(client_fd, client);
			return;
		}
		return;
	}
	if (revents & (POLLHUP | POLLERR | POLLNVAL)) {
		client.cgi.mark_eof();
		client.cgi.finish();
		finish_cgi(client_fd, client);
	}
}

void WebServ::finish_cgi(int client_fd, Client &client, int forced_status) {
	const Server &server = get_client_server(client_fd);

	int in_fd = client.cgi.in_fd();
	int out_fd = client.cgi.out_fd();
	remove_poll_fd(in_fd);
	remove_poll_fd(out_fd);
	_cgi_fds.erase(in_fd);
	_cgi_fds.erase(out_fd);

	Response response;
	if (forced_status != 0) {
		client.cgi.terminate();
		response = Response::error(forced_status, server);
	} else {
		response = client.cgi.build_response(server);
		client.cgi.terminate();
	}
	if (client.parser.get_request().get_method() == "HEAD")
		response.set_no_body(true);
	response.set_header("Connection", "close");
	std::cout << "fd " << client_fd << ": CGI " << client.cgi.script_path()
							<< " -> " << response.get_status() << std::endl;
	client.response = response.build();
	std::string tmp = client.cgi.take_tmp_path();
	int fd = -1;

	if (response.has_file() && !response.get_no_body() && !tmp.empty())
		fd = open(tmp.c_str(), O_RDONLY);
	if (!tmp.empty())
		unlink(tmp.c_str());
	if (response.has_file() && !response.get_no_body()) {
		if (fd < 0 || !set_non_blocking(fd)) {
			if (fd >= 0)
				close(fd);
			client.response = Response::error(500, server).build();
		} else {
			client.file_fd = fd;
			client.file_size = response.get_file_size();
			client.file_sent = 0;
			client.file_buffer.clear();
			client.file_buffer_pos = 0;
		}
	}
	client.sent = 0;
	client.last_activity = std::time(NULL);
	client.state = WRITING_RESPONSE;

	int index = -1;
	for (size_t i = 0; i < _poll_fds.size(); i++) {
		if (_poll_fds[i].fd == client_fd) {
			index = static_cast<int>(i);
			break;
		}
	}
	if (index < 0) {
		close_client(client_fd);
		return;
	}
	_poll_fds[index].events = POLLOUT;
}

void WebServ::remove_poll_fd(int fd) {
	if (fd < 0)
		return;
	for (size_t i = 0; i < _poll_fds.size(); i++) {
		if (_poll_fds[i].fd == fd) {
			_poll_fds.erase(_poll_fds.begin() + i);
			_fd_epoch[fd] = ++_fd_epoch_counter;
			return;
		}
	}
}

void WebServ::close_client(int client_fd) {
	std::map<int, Client>::iterator it = _clients.find(client_fd);
	if (it == _clients.end())
		return;
	Client &client = it->second;

	remove_poll_fd(client.cgi.in_fd());
	remove_poll_fd(client.cgi.out_fd());
	_cgi_fds.erase(client.cgi.in_fd());
	_cgi_fds.erase(client.cgi.out_fd());
	client.cgi.terminate();

	close_file(client);
	_clients.erase(it);
	_client_listen.erase(client_fd);
	close(client_fd);
	remove_poll_fd(client_fd);
}

const Server &WebServ::get_client_server(int client_fd) {
	int listen_index = _client_listen[client_fd];
	return _servers[_listen_sockets[listen_index].server_indexes[0]];
}

void WebServ::cleanup_server() { cleanup_socket(); }

void WebServ::cleanup_socket() {
	for (size_t i = 0; i < _listen_sockets.size(); i++) {
		if (_listen_sockets[i].fd >= 0) {
			close(_listen_sockets[i].fd);
			_listen_sockets[i].fd = -1;
		}
	}
}

bool WebServ::set_non_blocking(int fd) {
	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
		perror("fcntl");
		return false;
	}
	if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
		perror("fcntl");
		return false;
	}
	return true;
}

void WebServ::add_poll_fd(int fd, short events) {
	struct pollfd pollfd;
	pollfd.fd = fd;
	pollfd.events = events;
	pollfd.revents = 0;
	_poll_fds.push_back(pollfd);
	_fd_epoch[fd] = ++_fd_epoch_counter;
}

WebServ::~WebServ() { cleanup_server(); }
