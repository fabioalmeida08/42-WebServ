/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   request_parser.hpp                                 :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lbento <lbento@student.42.fr>              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/18 12:22:27 by lbento            #+#    #+#             */
/*   Updated: 2026/09/26 23:03:56 by lbento           ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef REQUEST_PARSER_HPP
# define REQUEST_PARSER_HPP

# include <string>
# include "request.hpp"

class RequestParser
{
	private:
		enum State
		{
			REQUEST_LINE,
			HEADERS,
			BODY,
			CHUNK_SIZE,
			CHUNK_DATA,
			CHUNK_CRLF,
			CHUNK_TRAILER,
			COMPLETE,
			FAILED
		};

		State		_state;
		std::string	_buffer;
		Request		_request;
		size_t		_max_body_size;
		size_t		_remaining;
		size_t		_header_bytes;
		int			_error_code;
		bool	step();
		bool	parse_request_line();
		bool	parse_target(const std::string &target);
		bool	parse_header_line();
		bool	start_body();
		bool	read_body(State next_state);
		bool	parse_chunk_size();
		bool	parse_chunk_crlf();
		bool	parse_trailer_line();
		bool	extract_line(std::string &line, int error_code);
		bool	fail(int error_code);
	public:
		RequestParser();
		RequestParser(const RequestParser &other);
		RequestParser &operator=(const RequestParser &other);
		~RequestParser();
		void	set_max_body_size(size_t max_body_size);
		void	feed(const char *data, size_t size);
		bool	is_done() const;
		bool	has_error() const;
		int		get_error_code() const;
		Request	&get_request();
};

#endif
