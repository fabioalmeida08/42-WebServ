/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   response.hpp                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lbento <lbento@student.42.fr>              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/19 18:52:15 by ranhaia-          #+#    #+#             */
/*   Updated: 2026/09/26 23:13:48 by lbento           ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef RESPONSE_HPP
# define RESPONSE_HPP

# include <map>
# include <string>
# include "server.hpp"

class Response
{
	private:
		int			_status_code;
		std::string	_status_msg;
		std::string	_body;
		std::map<std::string, std::string>	_headers;
	public:
		Response();
		~Response();
		Response(const Response &other);
		Response &operator=(const Response &other);
		void	set_status(int code);
		void	set_header(const std::string &name, const std::string &value);
		void	set_body(const std::string &body);

		int	get_status() const;
		const std::string	&get_status_msg() const;

		std::string	build() const;
		void	clear();

		static Response	error(int code, const Server &server);
};

#endif
