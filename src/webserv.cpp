/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   webserv.cpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lbento <lbento@student.42.fr>              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/09/26 22:33:09 by lbento            #+#    #+#             */
/*   Updated: 2026/10/07 05:17:53 by lbento           ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

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
// Copia do que o poll marcou, antes de tratar qualquer evento. Guarda a
// versao da entrada na hora da copia: os handlers fecham fds dentro do
// loop e o accept() pode reutilizar o mesmo numero no mesmo tick.
struct ReadyFd {
	int fd;
	short revents;
	unsigned long epoch;
};
}  // namespace

// 511 e o tippico backlog de producao (nginx usa o mesmo). Com 128 e
// centenas de conexoes simultaneas o kernel devolve RST pra quem entra
// depois que a fila enche.
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
			// Vhost esta fora do escopo (o subject so pede um server por
			// porta); o primeiro bloco responde por todas, entao este
			// aqui nunca vai ser alcançavel — deixa explicito no startup
			const std::vector<std::string> &names = _servers[i].get_names();
			std::cerr << "warning: porta " << port << " ja esta em uso por "
								<< "outro bloco server; o server_name \""
								<< (names.empty() ? std::string("(sem nome)")
								                 : names[0])
								<< "\" fica inalcançavel (vhost fora de escopo)"
								<< std::endl;
			continue;
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
			std::cerr << "warning: nao foi possivel bind na porta " << port
								<< " — grupo ignorado" << std::endl;
			continue;
		}

		_listen_sockets.push_back(listen_socket);
		_listen_fds[listen_socket.fd] = static_cast<int>(_listen_sockets.size()) - 1;
	}

	if (_listen_sockets.empty()) {
		std::cerr << "nenhum socket de escuta foi criado" << std::endl;
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
	std::cout << "Server Aguardando conexões em "
						<< _listen_sockets.size() << " porta(s)..." << std::endl;
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

	for (size_t i = 0; i < _listen_sockets.size(); i++) {
		if (!set_non_blocking(_listen_sockets[i].fd))
			return false;
		add_poll_fd(_listen_sockets[i].fd, POLLIN);
	}

	while (1) {
		// O poll precisa acordar mais vezes do que o menor prazo: se ele
		// esperasse REQUEST_TIMEOUT (60s) o check_timeouts so rodaria a
		// cada 60s e o timeout de CGI (30s) dispararia na metade da hora.
		// 1s de tick mantem todos os prazos com precisao de 1 segundo.
		int ready = poll(_poll_fds.data(), _poll_fds.size(), POLL_TICK_MS);

		if (ready < 0) {
			perror("poll");
			break;
		}

		check_timeouts();

		// Copia os fds prontos ANTES de tratar qualquer um: os handlers
		// apagam entradas do _poll_fds (finish_cgi tira os dois pipes do
		// CGI, close_client tira pipes + socket) e com a iteracao por
		// indice o i passava a apontar pra outra entrada — ou fora da
		// faixa, o que deixava um cliente em READING escutando so POLLOUT,
		// nunca recebia POLLIN e morria de 408 sem resposta.
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

			// O fd pode ter sido fechado por um handler anterior — ou
			// fechado e reutilizado, que e pior: sem conferir a versao o
			// evento velho seria entregue ao cliente novo
			if (_fd_epoch[fd] != ready_fds[k].epoch)
				continue;
			int index = poll_index(fd);
			if (index < 0)
				continue;

			if (find_listen_index(fd) >= 0) {
				handle_new_connection(find_listen_index(fd));
				continue;
			}
			// Pipe do CGI: o dono e um cliente, mas o fd nao e socket
			std::map<int, int>::iterator cgi_it = _cgi_fds.find(fd);
			if (cgi_it != _cgi_fds.end()) {
				handle_cgi_event(cgi_it->second, fd, revents);
				continue;
			}
			if (_clients.find(fd) == _clients.end())
				continue;
			// POLLIN e POLLOUT podem chegar juntos; o indice e recalcu-
			// lado na hora porque os handlers mexem no vetor
			handle_client_events(fd, index, revents);
		}
	}
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
	// Drena a fila de aceitação inteira: aceitando um por volta, 200
	// conexões ao mesmo tempo estouram a accept queue e o kernel responde
	// RST pro cliente que ja completou o handshake. O limite de 64 por
	// evento mantém o loop finito sem precisar checar errno.
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

		std::cout << "Cliente conectado, fd " << client_fd << " na porta "
							<< _listen_sockets[listen_index].port << std::endl;
	}
}

// Dispara o que o poll marcou para um cliente.
//
// Ordem importa: drena o POLLIN antes de olhar HUP/ERR, porque quando o
// peer fecha a conexao o request ja pode ter chegado junto — fechar sem
// ler manda RST e o cliente perde a resposta.
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
	// So escreve se tem resposta pendente: um POLLOUT esporadico num
	// cliente que ainda esta lendo o request nao pode derrubar ele
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

bool WebServ::handle_client_read(int client_fd, int index) {
	char buffer[4096];
	ssize_t bytes_rcv = recv(client_fd, buffer, sizeof(buffer), 0);

	if (bytes_rcv == 0) {
		// EOF: o peer fechou de verdade
		close_client(client_fd);
		return false;
	}
	if (bytes_rcv < 0) {
		// -1 sem POLLHUP: nao da pra checar errno depois de recv, entao
		// nao da pra distinguir EAGAIN de erro real. Espera o proximo
		// POLLIN; erro de verdade reaparece como POLLERR/POLLHUP.
		return true;
	}

	Client &client = _clients[client_fd];
	client.last_activity = std::time(NULL);
	try {
		client.parser.feed(buffer, static_cast<size_t>(bytes_rcv));
		if (!client.parser.is_done())
			return true;

		const Server &server = get_client_server(client_fd);
		Response response;
		if (client.parser.has_error()) {
			response = Response::error(client.parser.get_error_code(), server);
			std::cout << "fd " << client_fd << ": requisição inválida -> "
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
				std::cout << "CGI iniciado";
			else
				std::cout << response.get_status();
			std::cout << std::endl;
			if (response.get_is_cgi()) {
				client.cgi_body = request.get_body();
				client.cgi_body_sent = 0;
				client.state = CGI_RUNNING;
				client.last_activity = std::time(NULL);
				// stdin do CGI: manda o corpo e depois da EOF. stdout
				// sempre: a resposta vem por lá.
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
		// Corpo em disco: abre o arquivo e manda em blocos, sem carregar
		// tudo na memoria (subject: nao pode estourar RAM)
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

	// Primeiro os headers (e o corpo que esteja em memoria)
	if (client.sent < client.response.size()) {
		ssize_t bytes_sent =
				send(client_fd, client.response.data() + client.sent,
						 client.response.size() - client.sent, 0);

		if (bytes_sent <= 0) {
			// Socket cheio: espera o proximo POLLOUT em vez de derrubar
			_poll_fds[index].events = POLLOUT;
			return true;
		}
		client.last_activity = std::time(NULL);
		client.sent += static_cast<size_t>(bytes_sent);
		if (client.sent < client.response.size()) {
			_poll_fds[index].events = POLLOUT;
			return true;
		}
	}
	if (client.file_fd >= 0) {
		client.state = SENDING_FILE;
		return send_file_block(client_fd, client, index);
	}
	close_client(client_fd);
	return false;
}

// Manda o arquivo em disco em blocos de FILE_CHUNK bytes. O arquivo em
// disco nao passa pelo poll (isento pelo subject), mas o socket continua
// sendo escrito só quando o poll libera POLLOUT.
//
// Um send() nao-bloqueante pode voltar parcial ou -1 quando o buffer do
// socket enche. O subject proibe checar errno depois de read/write, entao
// nao da pra distinguir EAGAIN de um erro de verdade: a saida e guardar o
// que sobrou e esperar o proximo POLLOUT. Erro real aparece como
// POLLHUP/POLLERR no poll ou estoura o timeout.
bool WebServ::send_file_block(int client_fd, Client &client, int index) {
	static const size_t FILE_CHUNK = 64 * 1024;

	while (client.file_sent < client.file_size) {
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
		}
		ssize_t bytes_sent = send(client_fd,
															&client.file_buffer[client.file_buffer_pos],
															client.file_buffer.size() - client.file_buffer_pos, 0);
		if (bytes_sent <= 0) {
			// Buffer do socket cheio: espera o proximo POLLOUT
			_poll_fds[index].events = POLLOUT;
			return true;
		}
		client.last_activity = std::time(NULL);
		client.file_buffer_pos += static_cast<size_t>(bytes_sent);
		client.file_sent += static_cast<size_t>(bytes_sent);
	}
	close_client(client_fd);
	return false;
}

void WebServ::close_file(Client &client) {
	if (client.file_fd >= 0) {
		close(client.file_fd);
		client.file_fd = -1;
	}
}

// Fecha clientes que nunca terminaram de mandar o request, e tambem os que
// pararam de fazer progresso no envio (um peer que conecta e nunca le).
void WebServ::check_timeouts() {
	time_t now = std::time(NULL);

	// Mesmo motivo do loop principal: os handlers apagam mais de uma
	// entrada por chamada, entao trabalha com uma copia dos fds
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
		// Cada estado tem o seu proprio prazo: o CGI morre antes (30s)
		// porque um script travado nao pode prender o cliente pra sempre
		int limit = (client.state == CGI_RUNNING) ? Cgi::TIMEOUT
		                                          : REQUEST_TIMEOUT;
		if (now - client.last_activity < limit)
			continue;

		if (client.state == CGI_RUNNING) {
			std::cout << "fd " << client_fd << ": CGI sem resposta após "
								<< Cgi::TIMEOUT << "s -> 504" << std::endl;
			// Mata o script e responde 504 no lugar da saida dele
			finish_cgi(client_fd, client, 504);
			continue;
		}
		if (client.state != READING) {
			std::cout << "fd " << client_fd << ": sem progresso no envio após "
								<< REQUEST_TIMEOUT << "s -> fechando" << std::endl;
			close_client(client_fd);
			continue;
		}

		Response timeout = Response::error(408, get_client_server(client_fd));

		std::cout << "fd " << client_fd << ": timeout após "
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

// Um pipe do CGI ficou pronto. O fd nao e socket, mas passa pelo mesmo
// poll() — o subject exige isso para todo I/O que pode esperar.
void WebServ::handle_cgi_event(int client_fd, int cgi_fd, short revents) {
	std::map<int, Client>::iterator it = _clients.find(client_fd);
	if (it == _clients.end()) {
		remove_poll_fd(cgi_fd);
		_cgi_fds.erase(cgi_fd);
		return;
	}
	Client &client = it->second;

	// Guarda contra fd reutilizado: se este fd nao e mais o pipe deste
	// cliente, ele foi fechado e o numero foi dado pra outro objeto (um
	// arquivo de disco, por exemplo). Limpa e sai sem tocar no fd.
	if (cgi_fd != client.cgi.in_fd() && cgi_fd != client.cgi.out_fd()) {
		remove_poll_fd(cgi_fd);
		_cgi_fds.erase(cgi_fd);
		return;
	}

	if (cgi_fd == client.cgi.in_fd()) {
		// O lado da escrita: manda o corpo e fecha o pipe (o CGI precisa
		// do EOF pra saber que o body terminou)
		if (revents & (POLLHUP | POLLERR | POLLNVAL)) {
			// O script fechou o stdin sem ler; nao mata ele por isso
			client.cgi.close_stdin();
		} else {
			client.cgi.feed_body(client.cgi_body, client.cgi_body_sent);
		}
		if (client.cgi.in_fd() < 0) {
			remove_poll_fd(cgi_fd);
			_cgi_fds.erase(cgi_fd);
		}
		client.last_activity = std::time(NULL);
		return;
	}
	// POLLIN vem antes do POLLHUP: com saida rápida os dois chegam juntos
	// e tratar o HUP primeiro mandava a resposta com o buffer vazio
	if (revents & POLLIN) {
		char buffer[4096];
		ssize_t bytes_rcv = read(cgi_fd, buffer, sizeof(buffer));

		if (bytes_rcv > 0) {
			client.cgi.collect(buffer, static_cast<size_t>(bytes_rcv));
			client.last_activity = std::time(NULL);
			if (client.cgi.buffer_full()) {
				std::cerr << "fd " << client_fd << ": saida do CGI passou de "
									<< "10MB -> 500" << std::endl;
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
		// -1 sem POLLHUP: nao da pra checar errno, espera o proximo evento
		return;
	}
	if (revents & (POLLHUP | POLLERR | POLLNVAL)) {
		// EOF sem dado pendente: o script morreu sem fechar limpo
		client.cgi.mark_eof();
		client.cgi.finish();
		finish_cgi(client_fd, client);
	}
}

// CGI terminou: monta a resposta a partir da saida do script e volta pro
// caminho normal de envio ao cliente.
//
// O last_activity precisa ser renovado aqui: sem isso o poll seguinte
// retorna na hora (POLLOUT), o check_timeouts roda de novo com o mesmo
// timestamp velho e derruba a conexao antes de enviar a resposta.
void WebServ::finish_cgi(int client_fd, Client &client, int forced_status) {
	const Server &server = get_client_server(client_fd);

	// Captura antes do terminate(), que fecha os dois lados
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
			// Invalida a versao: um snapshot ja tirado deste tick nao
			// pode ser entregue ao objeto novo que pegar este numero
			_fd_epoch[fd] = ++_fd_epoch_counter;
			return;
		}
	}
}

// O indice do fd no vetor nao pode ser confiado: remove_poll_fd pode ter
// removido entradas antes e deslocado tudo. Por isso procura de novo.
void WebServ::close_client(int client_fd) {
	std::map<int, Client>::iterator it = _clients.find(client_fd);
	if (it == _clients.end())
		return;
	Client &client = it->second;

	// Pipes do CGI primeiro: eles ficam em posicoes diferentes no vetor
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
	// Sem CLOEXEC o script do CGI ia herdar os sockets de todos os
	// clientes conectados e segurar eles abertos ate morrer
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
	// Toda entrada nova ganha uma versao propria. O snapshot do loop
	// principal guarda essa versao e descarta o evento se o numero de fd
	// foi reutilizado por outro objeto no meio do tick.
	_fd_epoch[fd] = ++_fd_epoch_counter;
}

WebServ::~WebServ() { cleanup_server(); }