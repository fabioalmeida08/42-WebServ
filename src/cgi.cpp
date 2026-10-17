#include "../includes/cgi.hpp"
#include "../includes/HttpUtils.hpp"
#include "../includes/HttpStatus.hpp"

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

static const size_t	MAX_CGI_HEADER = 64 * 1024;
static const size_t	MAX_CGI_OUTPUT = 200 * 1024 * 1024;

const int	Cgi::TIMEOUT;

static void	add_env(std::vector<std::string> &env, const std::string &key,
									const std::string &value)
{
	env.push_back(key + "=" + value);
}

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

bool	Cgi::_open_tmp()
{
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
		_body_size = MAX_CGI_OUTPUT + 1;
		return;
	}
	while (offset < size)
	{
		if (_body_size + (size - offset) > MAX_CGI_OUTPUT)
		{
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

std::string	Cgi::script_path_of(const std::string &path, const Location &loc)
{
	return Location::resolve_path(path, loc);
}

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
	std::string	uri = req.get_path();

	if (!req.get_query().empty())
		uri += "?" + req.get_query();
	add_env(env, "REQUEST_URI", uri);
	add_env(env, "PATH_INFO", req.get_path());
	add_env(env, "PATH_TRANSLATED", "");
	add_env(env, "REMOTE_ADDR", remote_addr);
	add_env(env, "REMOTE_HOST", remote_addr);
	if (req.has_header("content-type"))
		add_env(env, "CONTENT_TYPE", req.get_header("content-type"));
	if (req.has_header("content-length"))
		add_env(env, "CONTENT_LENGTH", req.get_header("content-length"));
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
	_head.clear();
	_tmp_path.clear();
	_header_done = false;
	_body_size = 0;
	if (_tmp_fd >= 0)
	{
		close(_tmp_fd);
		_tmp_fd = -1;
	}
	const std::string	abs_script = absolute_path(script_path);
	const std::string	abs_exec = loc.get_cgi_exec().empty()
		? abs_script : absolute_path(loc.get_cgi_exec());
	struct stat			info;

	if (stat(abs_script.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
	{
		if (loc.get_cgi_exec().empty())
		{
			err = "script not found: " + script_path;
			return false;
		}
	}
	else if (access(abs_script.c_str(),
								 loc.get_cgi_exec().empty() ? X_OK : R_OK) != 0)
	{
		err = "script is not executable: " + script_path;
		return false;
	}
	if (abs_exec != abs_script
		&& (stat(abs_exec.c_str(), &info) != 0 || !S_ISREG(info.st_mode)))
	{
		err = "cgi interpreter not found: " + loc.get_cgi_exec();
		return false;
	}
	if (access(abs_exec.c_str(), X_OK) != 0)
	{
		err = "cgi interpreter is not executable: " + abs_exec;
		return false;
	}

	int	in_pipe[2];
	int	out_pipe[2];

	if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0)
	{
		err = "pipe() failed";
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
		err = "fork() failed";
		close(in_pipe[0]); close(in_pipe[1]);
		close(out_pipe[0]); close(out_pipe[1]);
		return false;
	}
	if (_pid == 0)
	{
		dup2(in_pipe[0], STDIN_FILENO);
		dup2(out_pipe[1], STDOUT_FILENO);
		close(in_pipe[0]); close(in_pipe[1]);
		close(out_pipe[0]); close(out_pipe[1]);

		chdir(dirname_of(abs_script).c_str());

		std::vector<char *>	envp;
		for (size_t i = 0; i < env.size(); i++)
			envp.push_back(const_cast<char *>(env[i].c_str()));
		envp.push_back(NULL);

		std::vector<char *>	argv;

		argv.push_back(const_cast<char *>(abs_exec.c_str()));
		if (abs_exec != abs_script)
			argv.push_back(const_cast<char *>(abs_script.c_str()));
		argv.push_back(NULL);

		execve(abs_exec.c_str(), &argv[0], &envp[0]);
		std::cerr << "execve failed: " << std::strerror(errno) << std::endl;
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

bool	Cgi::feed_body(const std::string &body, size_t &sent)
{
	if (_in_fd < 0)
		return true;
	if (sent >= body.size())
	{
		_close_in();
		return true;
	}
	ssize_t	written = write(_in_fd, body.data() + sent, body.size() - sent);

	if (written <= 0)
		return false;
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

		_head.erase(sep + sep_len);
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
