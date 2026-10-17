#ifndef RESPONSE_HPP
# define RESPONSE_HPP

# include <map>
# include <string>
# include "server.hpp"

class Response
{
	private:
		int	_status_code;
		std::string	_status_msg;
		std::map<std::string, std::string>	_headers;
		std::string	_body;
		bool	_no_body;
		bool	_has_file;
		bool	_is_cgi;
		size_t	_file_size;
		std::string	_file_path;
	public:
		Response();
		Response(const Response &other);
		Response &operator=(const Response &other);
		~Response();

		void	set_status(int code);
		void	set_status_msg(const std::string &status_msg);
		void	set_header(const std::string &name, const std::string &value);
		void	set_body(const std::string &body);
		void	set_no_body(bool no_body);

		void	set_is_cgi(bool is_cgi);

		void	set_file(const std::string &path, size_t file_size);

		int	get_status() const;
		const std::string	&get_status_msg() const;
		bool	get_no_body() const;
		bool	get_is_cgi() const;
		bool	has_file() const;
		size_t	get_file_size() const;
		const std::string	&get_file_path() const;

		static bool	status_has_body(int code);

		std::string	build() const;
		void	clear();

		static Response	error(int code, const Server &server);
};

#endif
