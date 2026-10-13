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
#include <cctype>
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

// Teto do cabecalho do CGI: sem separador de fim de cabecalho a resposta
// nao da pra interpretar, e um script que mandasse 100MB de "headers" que
// nesse lixo a memoria.
static const size_t	MAX_CGI_HEADER = 64 * 1024;
// Teto do corpo. O corpo vai pra um arquivo temporario (e nao pra RAM),
// mas o teto existe pra um script malicioso nao encher o disco.
static const size_t	MAX_CGI_OUTPUT = 200 * 1024 * 1024;

const int	Cgi::TIMEOUT;

// O subject exige C++98 e so permite um conjunto de funções; setenv nao
// esta na lista, entao montamos o array char** na mao.
static void	add_env(std::vector<std::string> &env, const std::string &key,
									const std::string &value)
{
	env.push_back(key + "=" + value);
}

// Nome do header do request vira nome de variavel de ambiente: caixa alta
// e hifen vira sublinhado (X-Test-Header -> X_TEST_HEADER), como a
// especificacao do CGI manda.
static std::string	header_env_name(const std::string &name)
{
	std::string	out = "HTTP_";

	for (size_t i = 0; i < name.size(); i++)
	{
		char	c = name[i];

		if (c == '-')
			out += '_';
		else
			out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	}
	return out;
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

Cgi::Cgi() : _pid(-1), _in_fd(-1), _out_fd(-1), _tmp_fd(-1),
						_header_done(false), _body_size(0), _eof(false),
						_reaped(false), _exit_status(0)
{
}

Cgi::~Cgi()
{
	_close_in();
	_close_out();
	if (_tmp_fd >= 0)
		close(_tmp_fd);
	if (!_tmp_path.empty())
		unlink(_tmp_path.c_str());
	if (_pid > 0 && !_reaped)
	{
		kill(_pid, SIGKILL);
		waitpid(_pid, NULL, 0);
	}
}

// Arquivo temporario que recebe o corpo da resposta. mkstemp ja cria com
// 0600 e um nome que nao colide com de outro cliente. Prefere /var/tmp,
// que costuma ser disco de verdade: em /tmp tmpfs o "disco" seria a
// memoria de novo.
bool	Cgi::_open_tmp()
{
	// Prefere /var/tmp, que costuma ser disco de verdade: em /tmp tmpfs o
	// "disco" seria a memoria de novo. mkstemp ja cria com 0600 e um nome
	// que nao colide com o de outro cliente.
	const char	*dirs[] = { "/var/tmp/", "/tmp/", "." };

	for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
	{
		char	template_path[64];
		int		fd;

		std::strcpy(template_path, dirs[i]);
		std::strcat(template_path, "webserv-cgi-XXXXXX");
		fd = mkstemp(template_path);
		if (fd >= 0)
		{
			_tmp_fd = fd;
			_tmp_path = template_path;
			return true;
		}
	}
	return false;
}

void	Cgi::_spill(const char *data, size_t size)
{
	size_t	offset = 0;

	if (size == 0 || _body_size > MAX_CGI_OUTPUT)
		return;
	if (_tmp_fd < 0 && !_open_tmp())
	{
		_body_size = MAX_CGI_OUTPUT + 1;	// sem disco -> buffer_full() -> 500
		return;
	}
	// Este fd nao passa pelo poll (arquivo de disco e isento), entao o
	// loop aqui nao quebra a regra de uma I/O por cliente por poll
	while (offset < size)
	{
		if (_body_size + (size - offset) > MAX_CGI_OUTPUT)
		{
			// Passou do teto: nao grava o resto, mas empurra _body_size
			// pra cima do limite de proposito, pra buffer_full() acordar
			// e a resposta virar 500 em vez de um 200 truncado
			_body_size = MAX_CGI_OUTPUT + 1;
			return;
		}
		ssize_t	written = write(_tmp_fd, data + offset, size - offset);

		if (written <= 0)
			return;
		offset += static_cast<size_t>(written);
		_body_size += static_cast<size_t>(written);
	}
}

std::string	Cgi::take_tmp_path()
{
	std::string	path = _tmp_path;

	_tmp_path.clear();
	return path;
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

// Caminho do script dentro da root: /directory/youpi.bla com location
// /directory e root YoupiBanane vira YoupiBanane/youpi.bla (o prefixo da
// location sai da URL, como o subject manda)
std::string	Cgi::script_path_of(const std::string &path, const Location &loc)
{
	return Location::resolve_path(path, loc);
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
	// REQUEST_URI e obrigatorio: sem ele o cgi_tester oficial do 42 responde
	// 500 "failed to parse REQUEST_URI into a URL".
	std::string	uri = req.get_path();

	if (!req.get_query().empty())
		uri += "?" + req.get_query();
	add_env(env, "REQUEST_URI", uri);
	// PATH_INFO precisa ser o caminho do request (igual ao REQUEST_URI sem a
	// query): com "" o cgi_tester oficial responde 500 "PATH_INFO not found",
	// e com algo que nao case ele responde 500 "PATH_INFO incorrect".
	add_env(env, "PATH_INFO", req.get_path());
	add_env(env, "PATH_TRANSLATED", "");
	add_env(env, "REMOTE_ADDR", remote_addr);
	add_env(env, "REMOTE_HOST", remote_addr);
	if (req.has_header("content-type"))
		add_env(env, "CONTENT_TYPE", req.get_header("content-type"));
	if (req.has_header("content-length"))
		add_env(env, "CONTENT_LENGTH", req.get_header("content-length"));
	// Todo o resto do request vira variavel HTTP_*, como a especificacao
	// do CGI manda e como o subject cobra: "The full request and arguments
	// provided by the client must be available to the CGI". O tester
	// oficial do 42 manda um X-SECRET-HEADER-FOR-TEST justamente pra
	// conferir que o header chega la dentro.
	const std::map<std::string, std::string>	&headers = req.get_headers();

	for (std::map<std::string, std::string>::const_iterator it = headers.begin();
			it != headers.end(); ++it)
	{
		if (it->first == "content-type" || it->first == "content-length")
			continue;
		add_env(env, header_env_name(it->first), it->second);
	}
}

bool	Cgi::start(const Request &req, const Server &srv, const Location &loc,
								const std::string &script_path,
								const std::string &remote_addr, std::string &err)
{
	// Zera a saida de uma eventual execucao anterior do mesmo objeto
	_head.clear();
	_tmp_path.clear();
	_header_done = false;
	_body_size = 0;
	if (_tmp_fd >= 0)
	{
		close(_tmp_fd);
		_tmp_fd = -1;
	}
	// Absoluto: o filho faz chdir e o execve recebe o caminho inteiro
	const std::string	abs_script = absolute_path(script_path);
	// cgi <ext> <executavel>: roda o executavel e passa o arquivo como
	// argumento (o cgi_tester oficial do 42 so funciona assim, porque o
	// conteudo do .bla nao e um script). Vazio = executa o proprio arquivo,
	// o que depende do shebang.
	const std::string	abs_exec = loc.get_cgi_exec().empty()
		? abs_script : absolute_path(loc.get_cgi_exec());
	struct stat			info;

	if (stat(abs_script.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
	{
		// Sem interpretador o script e o proprio executavel: precisa existir.
		// Com interpretador, nao — o executavel e quem decide o que fazer
		// com o arquivo. O tester oficial do 42 manda POST justamente num
		// .bla que nao existe (/directory/youpla.bla) e espera que o
		// cgi_test seja chamado do mesmo jeito.
		if (loc.get_cgi_exec().empty())
		{
			err = "script nao encontrado: " + script_path;
			return false;
		}
	}
	else if (access(abs_script.c_str(),
								 loc.get_cgi_exec().empty() ? X_OK : R_OK) != 0)
	{
		err = "script sem permissao de execucao: " + script_path;
		return false;
	}
	if (abs_exec != abs_script
		&& (stat(abs_exec.c_str(), &info) != 0 || !S_ISREG(info.st_mode)))
	{
		err = "interpretador de cgi nao encontrado: " + loc.get_cgi_exec();
		return false;
	}
	if (access(abs_exec.c_str(), X_OK) != 0)
	{
		err = "interpretador de cgi sem permissao de execucao: " + abs_exec;
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

		// Sem interpretador o execve ja recebe so o script (o kernel cuida
		// do shebang); com interpretador o arquivo vira o 1o argumento.
		std::vector<char *>	argv;

		argv.push_back(const_cast<char *>(abs_exec.c_str()));
		if (abs_exec != abs_script)
			argv.push_back(const_cast<char *>(abs_script.c_str()));
		argv.push_back(NULL);

		execve(abs_exec.c_str(), &argv[0], &envp[0]);
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
	// Uma unica escrita por poll (regua: "only one read or one write per
	// client per select()"). Um corpo grande alimenta o CGI em varios
	// eventos de POLLOUT no pipe, um bloco por vez, e so fecha o stdin
	// quando o ultimo byte tiver sido aceito.
	if (sent >= body.size())
	{
		_close_in();
		return true;
	}
	ssize_t	written = write(_in_fd, body.data() + sent, body.size() - sent);

	if (written <= 0)
		return false;			// pipe cheio: proximo POLLOUT
	sent += static_cast<size_t>(written);
	if (sent >= body.size())
		_close_in();
	return sent >= body.size();
}

void	Cgi::close_stdin()
{
	_close_in();
}

void	Cgi::collect(const char *data, size_t size)
{
	if (!_header_done)
	{
		_head.append(data, size);
		size_t	sep = _head.find("\r\n\r\n");
		size_t	sep_len = 4;

		if (sep == std::string::npos)
		{
			sep = _head.find("\n\n");
			sep_len = 2;
		}
		if (sep == std::string::npos)
			return;
		std::string	body = _head.substr(sep + sep_len);

		_head.erase(sep + sep_len);		// fica so o cabecalho
		_header_done = true;
		_spill(body.data(), body.size());
		return;
	}
	_spill(data, size);
}

bool	Cgi::buffer_full() const
{
	return _head.size() > MAX_CGI_HEADER || _body_size > MAX_CGI_OUTPUT;
}

// Marca que a saida acabou. NAO fecha o out_fd aqui: finish_cgi() le o
// fd antes de limpar o _cgi_fds/_poll_fds, e zerar antes transformava a
// limpeza em erase(-1) — a entrada antiga ficava eterna e, quando o
// numero do fd fosse reutilizado por um arquivo de disco, o poll mandava
// a gente ler 4096 bytes do meio do arquivo de outro cliente.
void	Cgi::mark_eof()
{
	_eof = true;
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
	size_t		sep = _head.find("\r\n\r\n");

	if (sep == std::string::npos)
		sep = _head.find("\n\n");
	if (sep == std::string::npos)
		return Response::error(502, srv);

	std::string	head = _head.substr(0, sep);
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
	// O corpo ja esta no disco (ou vazio); marcar como arquivo deixa o
	// WebServ mandar em blocos pelo mesmo caminho dos arquivos estaticos
	if (Response::status_has_body(status) && _body_size > 0)
		res.set_file(_tmp_path, _body_size);
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
