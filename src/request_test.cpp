/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   request_test.cpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ranhaia- <ranhaia-@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/19 18:24:01 by ranhaia-          #+#    #+#             */
/*   Updated: 2026/09/14 19:54:04 by ranhaia-         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../includes/request_test.hpp"

Request::Request()
{
	std::cout << "parser request chamada" << "\n";
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
	this->method = buffer;
}

void		Request::set_uri(std::string buffer)
{
	this->uri = buffer;
}

std::string	Request::get_buffer()
{
	return (this->buffer);
}

std::string Request::get_body()
{
    std::string raw_buffer = this->buffer;
    
    // Procura onde terminam os cabeçalhos (a linha em branco)
    size_t pos = raw_buffer.find("\r\n\r\n");
    
    // Se não encontrar, significa que não tem corpo
    if (pos == std::string::npos) {
        return "";
    }
    
    // Retorna apenas o que vem DEPOIS do \r\n\r\n (que tem 4 caracteres)
    return raw_buffer.substr(pos + 4);
}
