/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   request.cpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lbento <lbento@student.42.fr>              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/18 12:27:41 by lbento            #+#    #+#             */
/*   Updated: 2026/09/26 23:30:34 by lbento           ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../includes/request.hpp"
#include "../includes/http_utils.hpp"

Request::Request()
{

}

Request::Request(const Request &other) : _method(other._method), _uri(other._uri), _path(other._path), _query(other._query), _version(other._version), _headers(other._headers), _body(other._body)
{

}

Request	&Request::operator=(const Request &other)
{
	if (this != &other)
	{
		_method = other._method;
		_uri = other._uri;
		_path = other._path;
		_query = other._query;
		_version = other._version;
		_headers = other._headers;
		_body = other._body;
	}
	return *this;
}

Request::~Request()
{
}

const std::string	&Request::get_method() const
{
	return _method;
}

const std::string	&Request::get_uri() const
{
	return _uri;
}

const std::string	&Request::get_path() const
{
	return _path;
}

const std::string	&Request::get_query() const
{
	return _query;
}

const std::string	&Request::get_version() const
{
	return _version;
}

const std::string	&Request::get_body() const
{
	return _body;
}

const std::map<std::string, std::string>	&Request::get_headers() const
{
	return _headers;
}

std::string	Request::get_header(const std::string &name) const
{
	std::map<std::string, std::string>::const_iterator	it;

	it = _headers.find(HttpUtils::to_lower(name));
	if (it == _headers.end())
		return "";
	return it->second;
}

bool	Request::has_header(const std::string &name) const
{
	return _headers.find(HttpUtils::to_lower(name)) != _headers.end();
}

void	Request::set_method(const std::string &method)
{
	_method = method;
}

void	Request::set_uri(const std::string &uri)
{
	_uri = uri;
}

void	Request::set_path(const std::string &path)
{
	_path = path;
}

void	Request::set_query(const std::string &query)
{
	_query = query;
}

void	Request::set_version(const std::string &version)
{
	_version = version;
}

void	Request::set_header(const std::string &name, const std::string &value)
{
	_headers[HttpUtils::to_lower(name)] = value;
}

void	Request::remove_header(const std::string &name)
{
	_headers.erase(HttpUtils::to_lower(name));
}

void	Request::append_body(const char *data, size_t size)
{
	_body.append(data, size);
}
