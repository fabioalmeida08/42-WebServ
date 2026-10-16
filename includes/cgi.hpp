#ifndef CGI_HPP
# define CGI_HPP

# include <string>
# include <vector>
# include <sys/types.h>

# include "request.hpp"
# include "server.hpp"
# include "location.hpp"
# include "response.hpp"

class Cgi
{
	public:
		static const int TIMEOUT = 30;

	private:
		pid_t			_pid;
		int				_in_fd;
		int				_out_fd;
		std::string		_head;
		std::string		_tmp_path;
		int				_tmp_fd;
		bool			_header_done;
		size_t			_body_size;
		bool			_eof;
		bool			_reaped;
		int				_exit_status;
		std::string		_script_path;

		void	_close_in();
		void	_close_out();
		bool	_open_tmp();
		void	_spill(const char *data, size_t size);
		void	_build_env(const Request &req, const Server &srv,
		                    const Location &loc, const std::string &remote_addr,
		                    std::vector<std::string> &env) const;
		Response	_parse_output(const Server &srv) const;
		int		_cgi_status() const;

	public:
		Cgi();
		~Cgi();

		bool	start(const Request &req, const Server &srv,
		              const Location &loc, const std::string &script_path,
		              const std::string &remote_addr, std::string &err);

		int		in_fd() const;
		int		out_fd() const;
		pid_t	pid() const;
		const std::string	&script_path() const;
		std::string	take_tmp_path();

		bool	feed_body(const std::string &body, size_t &sent);
		void	close_stdin();
		void	collect(const char *data, size_t size);
		bool	buffer_full() const;
		void	mark_eof();
		void	try_reap();
		void	finish();
		bool	finished() const;
		bool	is_running() const;

		Response	build_response(const Server &srv);
		void	terminate();

		static bool	is_cgi_path(const std::string &path, const Location &loc);
		static std::string	script_path_of(const std::string &path,
		                                   const Location &loc);
};

#endif
