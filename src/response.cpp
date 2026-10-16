/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   response.cpp                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lbento <lbento@student.42.fr>              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/19 18:52:08 by ranhaia-          #+#    #+#             */
/*   Updated: 2026/10/16 00:09:33 by lbento           ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../includes/response.hpp"
#include "../includes/HttpStatus.hpp"
#include "../includes/HttpUtils.hpp"
#include "../includes/mime.hpp"
#include <fstream>
#include <sstream>
#include <sys/stat.h>

// Respostas que nao levam corpo (1xx, 204, 304, 3xx)
bool	Response::status_has_body(int code)
{
	if (code < 200 || code == 204 || code == 304)
		return false;
	if (code >= 300 && code < 400)
		return false;
	return true;
}

static bool	read_file(const std::string &path, std::string &content)
{
	struct stat	info;

	if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
		return false;
	std::ifstream	file(path.c_str(), std::ios::in | std::ios::binary);
	if (!file.is_open())
		return false;
	std::ostringstream	data;
	data << file.rdbuf();
	content = data.str();
	return true;
}

static std::string	default_error_page(int code, const std::string &reason)
{
	std::ostringstream	html;

	html << "<!DOCTYPE html>\n"
		<< "<html>\n"
		<< "<head><title>" << code << " " << reason << "</title></head>\n"
		<< "<body>\n"
		<< "<center><h1>" << code << " " << reason << "</h1></center>\n"
		<< "<hr><center>webserv</center>\n"
		<< "</body>\n"
		<< "</html>\n";
	return html.str();
}

Response::Response() : _status_code(200), _status_msg(HttpStatus::reason(200)), _no_body(false), _has_file(false), _is_cgi(false), _file_size(0)
{

}

Response::Response(const Response &other) : _status_code(other._status_code), _status_msg(other._status_msg), _headers(other._headers), _body(other._body), _no_body(other._no_body), _has_file(other._has_file), _is_cgi(other._is_cgi), _file_size(other._file_size), _file_path(other._file_path)
{

}

Response	&Response::operator=(const Response &other)
{
	if (this != &other)
	{
		_status_code = other._status_code;
		_status_msg = other._status_msg;
		_headers = other._headers;
		_body = other._body;
		_no_body = other._no_body;
		_has_file = other._has_file;
		_is_cgi = other._is_cgi;
		_file_size = other._file_size;
		_file_path = other._file_path;
	}
	return *this;
}

Response::~Response()
{
}

void	Response::set_status(int code)
{
	_status_code = code;
	_status_msg = HttpStatus::reason(code);
}

void	Response::set_status_msg(const std::string &status_msg)
{
	_status_msg = status_msg;
}

void	Response::set_header(const std::string &name, const std::string &value)
{
	_headers[name] = value;
}

void	Response::set_body(const std::string &body)
{
	_body = body;
}

void	Response::set_no_body(bool no_body)
{
	_no_body = no_body;
}

void	Response::set_is_cgi(bool is_cgi)
{
	_is_cgi = is_cgi;
}

bool	Response::get_is_cgi() const
{
	return _is_cgi;
}

void	Response::set_file(const std::string &path, size_t file_size)
{
	_has_file = true;
	_file_size = file_size;
	_file_path = path;
}

bool	Response::get_no_body() const
{
	return _no_body;
}

bool	Response::has_file() const
{
	return _has_file;
}

size_t	Response::get_file_size() const
{
	return _file_size;
}

const std::string	&Response::get_file_path() const
{
	return _file_path;
}

int	Response::get_status() const
{
	return _status_code;
}

const std::string	&Response::get_status_msg() const
{
	return _status_msg;
}

void	Response::clear()
{
	_status_code = 200;
	_status_msg = HttpStatus::reason(200);
	_headers.clear();
	_body.clear();
	_no_body = false;
	_has_file = false;
	_is_cgi = false;
	_file_size = 0;
	_file_path.clear();
}

std::string	Response::build() const
{
	std::ostringstream	head;
	bool				has_body = status_has_body(_status_code) && !_no_body;
	bool				has_date = false;
	bool				has_server = false;

	head << "HTTP/1.1 " << _status_code << " " << _status_msg << "\r\n";
	for (std::map<std::string, std::string>::const_iterator it = _headers.begin();
		it != _headers.end(); ++it)
	{
		std::string	name = HttpUtils::to_lower(it->first);

		if (name == "content-length")
			continue;
		if (name == "date")
			has_date = true;
		if (name == "server")
			has_server = true;
		head << it->first << ": " << it->second << "\r\n";
	}
	if (!has_server)
		head << "Server: webserv/1.0\r\n";
	if (!has_date)
		head << "Date: " << HttpUtils::http_date() << "\r\n";
	// HEAD manda o mesmo Content-Length do GET, só sem o corpo
	if (status_has_body(_status_code))
	{
		if (_has_file)
			head << "Content-Length: " << _file_size << "\r\n";
		else
			head << "Content-Length: " << _body.size() << "\r\n";
	}
	head << "\r\n";

	std::string	message = head.str();
	if (has_body && !_has_file)
		message += _body;
	return message;
}

Response	Response::error(int code, const Server &server)
{
	Response	response;
	std::string	page_path = server.resolve_error_page(code);
	std::string	page;

	response.set_status(code);
	if (!page_path.empty() && read_file(page_path, page))
	{
		response.set_header("Content-Type", Mime::get_type(page_path));
		response.set_body(page);
	}
	else
	{
		response.set_header("Content-Type", "text/html");
		response.set_body(default_error_page(code, response.get_status_msg()));
	}
	return response;
}
