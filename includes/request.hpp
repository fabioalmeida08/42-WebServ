/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   request.hpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lbento <lbento@student.42.fr>              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/18 12:21:59 by lbento            #+#    #+#             */
/*   Updated: 2026/09/26 23:03:16 by lbento           ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef REQUEST_HPP
# define REQUEST_HPP

# include <map>
# include <string>

class Request
{
	private:
		std::string	_method;
		std::string	_uri;
		std::string	_path;
		std::string	_query;
		std::string	_version;
		std::map<std::string, std::string>	_headers;
		std::string	_body;
	public:
		Request();
		Request(const Request &other);
		Request &operator=(const Request &other);
		~Request();
	
		const std::string	&get_method() const;
		const std::string	&get_uri() const;
		const std::string	&get_path() const;
		const std::string	&get_query() const;
		const std::string	&get_version() const;
		const	std::string	&get_body() const;

		const	std::map<std::string, std::string>	&get_headers() const;
		std::string	get_header(const std::string &name) const;
		bool	has_header(const std::string &name) const;

		void	set_method(const std::string &method);
		void	set_uri(const std::string &uri);
		void	set_path(const std::string &path);
		void	set_query(const std::string &query);
		void	set_version(const std::string &version);
		void	set_header(const std::string &name, const std::string &value);
		void	remove_header(const std::string &name);
		void	append_body(const char *data, size_t size);
};

#endif
