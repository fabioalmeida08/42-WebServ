/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   response.hpp                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ranhaia- <ranhaia-@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/19 18:52:15 by ranhaia-          #+#    #+#             */
/*   Updated: 2026/08/19 19:55:11 by ranhaia-         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef RESPONSE_HPP
# define RESPONSE_HPP

#include <iostream>
#include <map>
#include <string>
#include <sstream>

class	Response
{
private:
	int			_status_code;
	std::string	_status_msg;
	std::string	_body;
	std::map<std::string, std::string> _headers;

	void	_set_status_msg();

public:
	Response();
	~Response();

	void    set_status(int code);
	void    set_header(const std::string &key, const std::string &value);
	void    set_body(const std::string &body);

	std::string build();

	void    clear();
};

#endif