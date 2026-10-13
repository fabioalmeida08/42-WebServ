/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   cgi.cpp                                            :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: fabio <fabio@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/10/13 00:00:00 by fabio             #+#    #+#             */
/*   Updated: 2026/10/13 00:00:00 by fabio            ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../includes/cgi.hpp"
#include "../includes/http_utils.hpp"
#include "../includes/http_status.hpp"

#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

// Teto da saida do CGI: se estourar, mata o processo e responde 500 em vez
// de deixar o script encher a memoria do servidor.
static const size_t	MAX_CGI_OUTPUT = 10 * 1024 * 1024;
static const int		CGI_TIMEOUT = 30;

// O subject exige C++98 e so permite um conjunto de funções; setenv nao
// esta na lista, entao montamos o array char** na mao.
static void	add_env(std::vector<std::string> &env, const std::string &key,
									const std::string &value)
{
	env.push_back(key + "=" + value);
}

static bool	set_blocking_off(int fd)
{
	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
		return false;
	// Nao deixa outro script CGI herdar o pipe de um CGI irmao
	if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
		return false;
	return true;
}

static std::string	dirname_of(const std::string &path)
{
	size_t	slash = path.rfind('/');

	if (slash == std::string::npos)
		return std::string(".");
	if (slash == 0)
		return std::string("/");
	return path.substr(0, slash);
}

// O caminho do config e relativo ao cwd do servidor, mas o CGI faz chdir
// pro diretorio do script; sem absoluto o execve procura no lugar errado.
static std::string	absolute_path(const std::string &path)
{
	char	resolved[PATH_MAX];

	if (realpath(path.c_str(), resolved) == NULL)
		return path;
	return std::string(resolved);
}

Cgi::Cgi() : _pid(-1), _in_fd(-1), _out_fd(-1), _eof(false), _reaped(false),
						_exit_status(0)
{
}

Cgi::~Cgi()
{
	_close_in();
	_close_out();
	if (_pid > 0 && !_reaped)
	{
		kill(_pid, SIGKILL);
		waitpid(_pid, NULL, 0);
	}
}

void	Cgi::_close_in()
{
	if (_in_fd >= 0)
	{
		close(_in_fd);
		_in_fd = -1;
	}
}

void	Cgi::_close_out()
{
	if (_out_fd >= 0)
	{
		close(_out_fd);
		_out_fd = -1;
	}
}

int		Cgi::in_fd() const { return _in_fd; }
int		Cgi::out_fd() const { return _out_fd; }
pid_t	Cgi::pid() const { return _pid; }
const std::string	&Cgi::script_path() const { return _script_path; }
bool	Cgi::finished() const { return _eof && _reaped; }
bool	Cgi::is_running() const { return _pid > 0 && !_reaped; }

bool	Cgi::is_cgi_path(const std::string &path, const Location &loc)
{
	const std::string	&ext = loc.get_cgi_ext();

	if (ext.empty() || path.size() < ext.size())
		return false;
	return path.compare(path.size() - ext.size(), ext.size(), ext) == 0;
}

// Caminho do script dentro da root: /cgi-bin/hello.py com root www vira
// www/cgi-bin/hello.py
std::string	Cgi::script_path_of(const std::string &path, const Location &loc)
{
	return loc.get_root() + path;
}

// Monta o ambiente do CGI. O subject faz questão disso: "Have a careful
// look at the environment variables involved in the web server-CGI
// communication. The full request and arguments provided by the client
// must be available to the CGI."
void	Cgi::_build_env(const Request &req, const Server &srv,
									const Location &loc, const std::string &remote_addr,
									std::vector<std::string> &env) const
{
	std::ostringstream	port;

	port << srv.get_port();
	add_env(env, "GATEWAY_INTERFACE", "CGI/1.1");
	add_env(env, "SERVER_SOFTWARE", "webserv/1.0");
	add_env(env, "SERVER_PROTOCOL", req.get_version());
	add_env(env, "SERVER_NAME", srv.get_names().empty() ? "localhost"
	                                                    : srv.get_names()[0]);
	add_env(env, "SERVER_PORT", port.str());
	add_env(env, "REQUEST_METHOD", req.get_method());
	add_env(env, "QUERY_STRING", req.get_query());
	add_env(env, "SCRIPT_NAME", req.get_path());
	add_env(env, "SCRIPT_FILENAME", script_path_of(req.get_path(), loc));
	add_env(env, "DOCUMENT_ROOT", loc.get_root());
	add_env(env, "PATH_INFO", "");
	add_env(env, "PATH_TRANSLATED", "");
	add_env(env, "REMOTE_ADDR", remote_addr);
	add_env(env, "REMOTE_HOST", remote_addr);
	if (req.has_header("content-type"))
		add_env(env, "CONTENT_TYPE", req.get_header("content-type"));
	if (req.has_header("content-length"))
		add_env(env, "CONTENT_LENGTH", req.get_header("content-length"));
}

bool	Cgi::start(const Request &req, const Server &srv, const Location &loc,
								const std::string &script_path,
								const std::string &remote_addr, std::string &err)
{
	// Absoluto: o filho faz chdir e o execve recebe o caminho inteiro
	const std::string	abs_script = absolute_path(script_path);
	struct stat			info;

	if (stat(abs_script.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
	{
		err = "script nao encontrado: " + script_path;
		return false;
	}
	if (access(abs_script.c_str(), X_OK) != 0)
	{
		err = "script sem permissao de execucao: " + script_path;
		return false;
	}

	int	in_pipe[2];
	int	out_pipe[2];

	if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0)
	{
		err = "pipe() falhou";
		return false;
	}

	std::vector<std::string>	env;
	_build_env(req, srv, loc, remote_addr, env);
	for (size_t i = 0; i < env.size(); i++)
	{
		if (env[i].compare(0, 16, "SCRIPT_FILENAME=") == 0)
			env[i] = "SCRIPT_FILENAME=" + abs_script;
	}

	_pid = fork();
	if (_pid < 0)
	{
		err = "fork() falhou";
		close(in_pipe[0]); close(in_pipe[1]);
		close(out_pipe[0]); close(out_pipe[1]);
		return false;
	}
	if (_pid == 0)
	{
		// O filho so precisa do outro lado dos pipes
		dup2(in_pipe[0], STDIN_FILENO);
		dup2(out_pipe[1], STDOUT_FILENO);
		close(in_pipe[0]); close(in_pipe[1]);
		close(out_pipe[0]); close(out_pipe[1]);

		// CGI deve rodar no diretorio do script pra access(relativo) funcionar
		chdir(dirname_of(abs_script).c_str());

		std::vector<char *>	envp;
		for (size_t i = 0; i < env.size(); i++)
			envp.push_back(const_cast<char *>(env[i].c_str()));
		envp.push_back(NULL);

		char	*const	argv[] = { const_cast<char *>(abs_script.c_str()), NULL };

		execve(abs_script.c_str(), argv, &envp[0]);
		std::cerr << "execve falhou: " << std::strerror(errno) << std::endl;
		_exit(127);
	}

	close(in_pipe[0]);
	close(out_pipe[1]);
	_in_fd = in_pipe[1];
	_out_fd = out_pipe[0];
	set_blocking_off(_in_fd);
	set_blocking_off(_out_fd);
	_script_path = script_path;
	return true;
}

// Escreve o body no stdin do CGI. Um write em pipe cheio volta -1 e o
// subject proibe checar errno, entao guarda a posição e espera o poll
// liberar o pipe de novo.
bool	Cgi::feed_body(const std::string &body, size_t &sent)
{
	if (_in_fd < 0)
		return true;
	while (sent < body.size())
	{
		ssize_t	written = write(_in_fd, body.data() + sent, body.size() - sent);

		if (written <= 0)
			return false;
		sent += static_cast<size_t>(written);
	}
	_close_in();
	return true;
}

void	Cgi::close_stdin()
{
	_close_in();
}

void	Cgi::collect(const char *data, size_t size)
{
	_out_buffer.append(data, size);
}

bool	Cgi::buffer_full() const
{
	return _out_buffer.size() > MAX_CGI_OUTPUT;
}

void	Cgi::mark_eof()
{
	_eof = true;
	_close_out();
}

void	Cgi::try_reap()
{
	if (_reaped || _pid <= 0)
		return;
	if (waitpid(_pid, &_exit_status, WNOHANG) == _pid)
		_reaped = true;
}

// Depois do EOF do stdout o filho ja deve estar morto; espera um pouco e
// mata se insistir, pra nao deixar zumbi nem travar a resposta.
void	Cgi::finish()
{
	if (_reaped || _pid <= 0)
		return;
	for (int i = 0; i < 20; i++)
	{
		if (waitpid(_pid, &_exit_status, WNOHANG) == _pid)
		{
			_reaped = true;
			return;
		}
		usleep(10000);
	}
	kill(_pid, SIGKILL);
	waitpid(_pid, &_exit_status, 0);
	_reaped = true;
}

void	Cgi::terminate()
{
	if (_pid > 0 && !_reaped)
	{
		kill(_pid, SIGTERM);
		for (int i = 0; i < 20 && !_reaped; i++)
		{
			if (waitpid(_pid, &_exit_status, WNOHANG) == _pid)
			{
				_reaped = true;
				break;
			}
			usleep(10000);
		}
		if (!_reaped)
		{
			kill(_pid, SIGKILL);
			waitpid(_pid, &_exit_status, 0);
			_reaped = true;
		}
	}
	_close_in();
	_close_out();
}

int	Cgi::_cgi_status() const
{
	if (WIFEXITED(_exit_status))
		return WEXITSTATUS(_exit_status);
	return -1;
}

// A saida do CGI e "header: valor\r\n...\r\n\r\ncorpo". O Status e opcional:
// sem ele o CGI diz 200. O fim do corpo e o EOF (o subject lembra disso).
Response	Cgi::_parse_output(const Server &srv) const
{
	Response	res;
	size_t		sep = _out_buffer.find("\r\n\r\n");
	size_t		sep_len = 4;

	if (sep == std::string::npos)
	{
		sep = _out_buffer.find("\n\n");
		sep_len = 2;
	}
	if (sep == std::string::npos)
		return Response::error(502, srv);

	std::string	head = _out_buffer.substr(0, sep);
	std::string	body = _out_buffer.substr(sep + sep_len);
	int			status = 200;

	std::istringstream	lines(head);
	std::string			line;
	while (std::getline(lines, line))
	{
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);
		size_t	colon = line.find(':');
		if (colon == std::string::npos)
			continue;
		std::string	name = HttpUtils::to_lower(HttpUtils::trim(line.substr(0, colon)));
		std::string	value = HttpUtils::trim(line.substr(colon + 1));

		if (name == "status")
		{
			std::istringstream	nums(value);
			int					code;

			nums >> code;
			if (code >= 100 && code <= 599)
			{
				status = code;
				size_t	space = value.find(' ');
				if (space != std::string::npos && HttpUtils::trim(value.substr(space + 1)).size())
					res.set_status_msg(HttpUtils::trim(value.substr(space + 1)));
			}
		}
		else if (name.size())
			res.set_header(name, value);
	}
	res.set_status(status);
	if (Response::status_has_body(status))
		res.set_body(body);
	return res;
}

Response	Cgi::build_response(const Server &srv)
{
	if (_cgi_status() != 0)
	{
		std::cerr << "cgi " << _script_path << ": exit " << _cgi_status()
							<< std::endl;
		return Response::error(502, srv);
	}
	return _parse_output(srv);
}
