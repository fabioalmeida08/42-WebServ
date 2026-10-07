/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   request_parser.cpp                                 :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lbento <lbento@student.42.fr>              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/18 12:28:11 by lbento            #+#    #+#             */
/*   Updated: 2026/09/26 23:29:36 by lbento           ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../includes/request_parser.hpp"
#include "../includes/http_utils.hpp"
#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <vector>

static const size_t	MAX_LINE_SIZE = 8192;
static const size_t	MAX_HEADERS_SIZE = 32768;
static const size_t	DEFAULT_MAX_BODY = 1024 * 1024;

static bool	is_token(const std::string &str)
{
	static const std::string	symbols = "!#$%&'*+-.^_`|~";

	if (str.empty())
		return false;
	for (size_t i = 0; i < str.size(); i++)
	{
		if (!std::isalnum(static_cast<unsigned char>(str[i]))
			&& symbols.find(str[i]) == std::string::npos)
			return false;
	}
	return true;
}

static bool	is_control(unsigned char c)
{
	return c < 0x20 || c == 0x7F;
}

static int	digit_value(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static bool	parse_number(const std::string &str, int base, size_t &value)
{
	const size_t	max = std::numeric_limits<size_t>::max();
	const size_t	b = static_cast<size_t>(base);

	if (str.empty())
		return false;
	value = 0;
	for (size_t i = 0; i < str.size(); i++)
	{
		int		digit = digit_value(str[i]);

		if (digit < 0 || digit >= base)
			return false;
		size_t	d = static_cast<size_t>(digit);
		if (value > (max - d) / b)
			value = max;
		else
			value = value * b + d;
	}
	return true;
}

static bool	percent_decode(const std::string &in, std::string &out)
{
	out.clear();
	for (size_t i = 0; i < in.size(); i++)
	{
		if (in[i] != '%')
		{
			out += in[i];
			continue;
		}
		if (i + 2 >= in.size())
			return false;
		int	high = digit_value(in[i + 1]);
		int	low = digit_value(in[i + 2]);
		if (high < 0 || low < 0)
			return false;
		unsigned char	c = static_cast<unsigned char>(high * 16 + low);
		if (is_control(c))
			return false;
		out += static_cast<char>(c);
		i += 2;
	}
	return true;
}

static bool	normalize_path(const std::string &path, std::string &out)
{
	std::vector<std::string>	segments;
	std::string					segment;
	size_t						start = 1;

	while (start <= path.size())
	{
		size_t	end = path.find('/', start);
		if (end == std::string::npos)
			end = path.size();
		segment = path.substr(start, end - start);
		if (segment == "..")
		{
			if (segments.empty())
				return false;
			segments.pop_back();
		}
		else if (!segment.empty() && segment != ".")
			segments.push_back(segment);
		start = end + 1;
	}
	out = "/";
	for (size_t i = 0; i < segments.size(); i++)
	{
		if (i > 0)
			out += "/";
		out += segments[i];
	}
	if (!segments.empty() && (segment.empty() || segment == "." || segment == ".."))
		out += "/";
	return true;
}

RequestParser::RequestParser() : _state(REQUEST_LINE), _max_body_size(DEFAULT_MAX_BODY), _remaining(0), _header_bytes(0), _error_code(0)
{

}

RequestParser::RequestParser(const RequestParser &other) : _state(other._state), _buffer(other._buffer), _request(other._request), _max_body_size(other._max_body_size), _remaining(other._remaining), _header_bytes(other._header_bytes), _error_code(other._error_code)
{

}

RequestParser	&RequestParser::operator=(const RequestParser &other)
{
	if (this != &other)
	{
		_state = other._state;
		_buffer = other._buffer;
		_request = other._request;
		_max_body_size = other._max_body_size;
		_remaining = other._remaining;
		_header_bytes = other._header_bytes;
		_error_code = other._error_code;
	}
	return *this;
}

RequestParser::~RequestParser()
{

}

void	RequestParser::set_max_body_size(size_t max_body_size)
{
	_max_body_size = max_body_size;
}

void	RequestParser::feed(const char *data, size_t size)
{
	if (is_done())
		return;
	_buffer.append(data, size);
	while (!is_done() && step())
		;
}

bool	RequestParser::is_done() const
{
	return _state == COMPLETE || _state == FAILED;
}

bool	RequestParser::has_error() const
{
	return _state == FAILED;
}

int	RequestParser::get_error_code() const
{
	return _error_code;
}

Request	&RequestParser::get_request()
{
	return _request;
}

bool	RequestParser::step()
{
	switch (_state)
	{
		case REQUEST_LINE:	return parse_request_line();
		case HEADERS:		return parse_header_line();
		case BODY:			return read_body(COMPLETE);
		case CHUNK_SIZE:	return parse_chunk_size();
		case CHUNK_DATA:	return read_body(CHUNK_CRLF);
		case CHUNK_CRLF:	return parse_chunk_crlf();
		case CHUNK_TRAILER:	return parse_trailer_line();
		default:			return false;
	}
}

bool	RequestParser::fail(int error_code)
{
	_state = FAILED;
	_error_code = error_code;
	_buffer.clear();
	return false;
}

bool	RequestParser::extract_line(std::string &line, int error_code)
{
	size_t	end = _buffer.find('\n');

	if (end == std::string::npos)
	{
		if (_buffer.size() > MAX_LINE_SIZE)
			return fail(error_code);
		return false;
	}
	if (end > MAX_LINE_SIZE)
		return fail(error_code);
	line = _buffer.substr(0, end);
	_buffer.erase(0, end + 1);
	if (!line.empty() && line[line.size() - 1] == '\r')
		line.erase(line.size() - 1);
	return true;
}

bool	RequestParser::parse_request_line()
{
	std::string	line;

	if (!extract_line(line, 414))
		return false;
	if (line.empty())
		return true;

	size_t	first_space = line.find(' ');
	size_t	last_space = line.rfind(' ');
	if (first_space == std::string::npos || first_space == last_space)
		return fail(400);

	std::string	method = line.substr(0, first_space);
	std::string	target = line.substr(first_space + 1, last_space - first_space - 1);
	std::string	version = line.substr(last_space + 1);

	if (!is_token(method) || target.empty() || target.find(' ') != std::string::npos)
		return fail(400);
	if (version.size() != 8 || version.compare(0, 5, "HTTP/") != 0
		|| !std::isdigit(static_cast<unsigned char>(version[5])) || version[6] != '.'
		|| !std::isdigit(static_cast<unsigned char>(version[7])))
		return fail(400);
	if (version[5] != '1')
		return fail(505);
	if (method != "GET" && method != "POST" && method != "DELETE")
		return fail(501);
	if (!parse_target(target))
		return false;
	_request.set_method(method);
	_request.set_version(version);
	_state = HEADERS;
	return true;
}

bool	RequestParser::parse_target(const std::string &target)
{
	if (target[0] != '/')
		return fail(400);
	for (size_t i = 0; i < target.size(); i++)
	{
		if (is_control(static_cast<unsigned char>(target[i])))
			return fail(400);
	}

	size_t		question = target.find('?');
	std::string	decoded;
	std::string	path;

	if (!percent_decode(target.substr(0, question), decoded)
		|| !normalize_path(decoded, path))
		return fail(400);
	_request.set_uri(target);
	_request.set_path(path);
	if (question != std::string::npos)
		_request.set_query(target.substr(question + 1));
	return true;
}

bool	RequestParser::parse_header_line()
{
	std::string	line;

	if (!extract_line(line, 431))
		return false;
	_header_bytes += line.size() + 2;
	if (_header_bytes > MAX_HEADERS_SIZE)
		return fail(431);
	if (line.empty())
		return start_body();
	if (line[0] == ' ' || line[0] == '\t')
		return fail(400);
	size_t	colon = line.find(':');
	if (colon == std::string::npos)
		return fail(400);

	std::string	name = HttpUtils::to_lower(line.substr(0, colon));
	std::string	value = HttpUtils::trim(line.substr(colon + 1));
	if (!is_token(name))
		return fail(400);
	for (size_t i = 0; i < value.size(); i++)
	{
		if (value[i] != '\t' && is_control(static_cast<unsigned char>(value[i])))
			return fail(400);
	}
	if (_request.has_header(name))
	{
		if (name == "host" || name == "content-length")
			return fail(400);
		value = _request.get_header(name) + ", " + value;
	}
	_request.set_header(name, value);
	return true;
}

bool	RequestParser::start_body()
{
	if (_request.get_version() != "HTTP/1.0" && !_request.has_header("host"))
		return fail(400);

	bool	has_encoding = _request.has_header("transfer-encoding");
	bool	has_length = _request.has_header("content-length");

	if (has_encoding)
	{
		if (has_length)
			return fail(400);
		std::string	encoding = HttpUtils::to_lower(_request.get_header("transfer-encoding"));
		if (encoding != "chunked")
		{
			size_t		comma = encoding.rfind(',');
			std::string	last = (comma == std::string::npos) ? encoding : encoding.substr(comma + 1);
			return fail(HttpUtils::trim(last) == "chunked" ? 501 : 400);
		}
		_state = CHUNK_SIZE;
		return true;
	}
	if (has_length)
	{
		size_t	length;

		if (!parse_number(_request.get_header("content-length"), 10, length))
			return fail(400);
		if (length > _max_body_size)
			return fail(413);
		_remaining = length;
		_state = (length == 0) ? COMPLETE : BODY;
		return true;
	}
	_state = COMPLETE;
	return true;
}

bool	RequestParser::read_body(State next_state)
{
	if (_buffer.empty())
		return false;
	size_t	count = std::min(_remaining, _buffer.size());
	_request.append_body(_buffer.data(), count);
	_buffer.erase(0, count);
	_remaining -= count;
	if (_remaining == 0)
		_state = next_state;
	return true;
}

bool	RequestParser::parse_chunk_size()
{
	std::string	line;
	size_t		size;

	if (!extract_line(line, 400))
		return false;
	size_t	semicolon = line.find(';');
	if (semicolon != std::string::npos)
		line.erase(semicolon);
	if (!parse_number(HttpUtils::trim(line), 16, size))
		return fail(400);
	if (size > _max_body_size - _request.get_body().size())
		return fail(413);
	if (size == 0)
	{
		_state = CHUNK_TRAILER;
		return true;
	}
	_remaining = size;
	_state = CHUNK_DATA;
	return true;
}

bool	RequestParser::parse_chunk_crlf()
{
	std::string	line;

	if (!extract_line(line, 400))
		return false;
	if (!line.empty())
		return fail(400);
	_state = CHUNK_SIZE;
	return true;
}

bool	RequestParser::parse_trailer_line()
{
	std::string	line;

	if (!extract_line(line, 431))
		return false;
	_header_bytes += line.size() + 2;
	if (_header_bytes > MAX_HEADERS_SIZE)
		return fail(431);
	if (!line.empty())
		return true;
	std::ostringstream	length;

	length << _request.get_body().size();
	_request.set_header("content-length", length.str());
	_request.remove_header("transfer-encoding");
	_state = COMPLETE;
	return true;
}
