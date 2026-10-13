/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   cgi.hpp                                            :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: fabio <fabio@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/10/13 00:00:00 by fabio             #+#    #+#             */
/*   Updated: 2026/10/13 00:00:00 by fabio            ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CGI_HPP
# define CGI_HPP

# include <string>
# include <vector>
# include <sys/types.h>

# include "request.hpp"
# include "server.hpp"
# include "location.hpp"
# include "response.hpp"

// Executa um script CGI: fork + execve com o ambiente HTTP montado, lendo
// o corpo do request pelo pipe de stdin e a saida pelo de stdout.
//
// Os dois pipes ficam registrados no mesmo poll() do resto do servidor:
// o subject exige que todo socket/pipe passe pelo poll, e pipe nao tem a
// isenção que os arquivos de disco tem.
class Cgi
{
	public:
		// Quanto tempo o script pode levar antes de virar 504. e menor
		// que o timeout do request: um CGI travado nao pode prender o
		// cliente pra sempre (subject: "never hang indefinitely")
		static const int TIMEOUT = 30;

	private:
		pid_t			_pid;
		int				_in_fd;
		int				_out_fd;
		std::string		_out_buffer;
		bool			_eof;
		bool			_reaped;
		int				_exit_status;
		std::string		_script_path;

		void	_close_in();
		void	_close_out();
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

		// Escreve o corpo do request no stdin do CGI. Devolve true quando
		// todo o corpo ja foi escrito e o pipe foi fechado (o CGI precisa
		// do EOF pra saber que acabou o body).
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
