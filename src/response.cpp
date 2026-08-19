/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   response.cpp                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ranhaia- <ranhaia-@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/19 18:52:08 by ranhaia-          #+#    #+#             */
/*   Updated: 2026/08/19 19:55:06 by ranhaia-         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../includes/response.hpp"

Response::Response()
{

}

Response::~Response()
{

}


void    Response::set_status(int code)
{
	this->_status_code = code;
	this->_set_status_msg();
}

void    Response::set_header(const std::string &key, const std::string &value)
{
	this->_headers[key] = value;
}

void    Response::set_body(const std::string &body)
{
	this->_body = body;
}

void	Response::_set_status_msg()
{
	switch(this->_status_code)
	{
		case 200:
			this->_status_msg = "OK";
			break ;
		case 202:
			this->_status_msg = "Accepted";
			break ;
		case 400:
			this->_status_msg = "Bad Request";
			break ;
		case 401:
			this->_status_msg = "Unauthorized";
			break ;
		case 403:
			this->_status_msg = "Forbidden";
			break ;
		case 404:
			this->_status_msg = "Not Found";
			break ;
		case 405:
			this->_status_msg = "Method Not Allowed";
			break ;
		case 408:
			this->_status_msg = "Request Timeout";
			break ;
		case 500:
			this->_status_msg = "Internal Server Error";
			break ;
		case 501:
			this->_status_msg = "Not Implemented";
			break ;
		case 502:
			this->_status_msg = "Bad Gateway";
			break ;
		case 503:
			this->_status_msg = "Service Unavailable";
			break ;
		case 504:
			this->_status_msg = "Gateway Timeout";
			break ;
		case 505:
			this->_status_msg = "HTTP Version Not Supported";
			break ;
	}
}


std::string Response::build()
{
    std::string response;
    std::stringstream ss;
    
    ss << this->_status_code;
    response.append("HTTP/1.1 " + ss.str() + " " + this->_status_msg + "\r\n");

    std::map<std::string, std::string>::iterator it;
    for (it = this->_headers.begin(); it != this->_headers.end(); ++it) {
        response.append(it->first + ": " + it->second + "\r\n");
    }

    std::stringstream ss_size;
    ss_size << this->_body.size();
    response.append("Content-Length: " + ss_size.str() + "\r\n");
    response.append("\r\n");
    response.append(this->_body);

    return response;
}


void    Response::clear()
{

}

