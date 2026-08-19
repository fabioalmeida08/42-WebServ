/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   request_test.cpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ranhaia- <ranhaia-@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/19 18:24:01 by ranhaia-          #+#    #+#             */
/*   Updated: 2026/08/19 18:32:49 by ranhaia-         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../includes/request_test.hpp"

Request::Request()
{
	std::cout << "request chamada" << "\n";
}

Request::~Request()
{

}	

std::string	Request::get_method()
{
	return (this->method);
}

std::string	Request::get_uri()
{
	return (this->uri);
}

void		Request::set_buffer(char *buffer)
{
	this->buffer = buffer;
}

void		Request::set_method(std::string buffer)
{
	(void)buffer;
	this->method = "GET";
}

void		Request::set_uri(std::string buffer)
{
	(void)buffer;
	this->uri = "/";
}

std::string	Request::get_buffer()
{
	return (this->buffer);
}
