/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   router.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ranhaia- <ranhaia-@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/09/14 13:08:21 by ranhaia-          #+#    #+#             */
/*   Updated: 2026/09/14 19:06:16 by ranhaia-         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef ROUTER_HPP
# define ROUTER_HPP

# include <string>
# include <vector>
# include <fstream>
# include <unistd.h>
# include <sys/stat.h>
# include <dirent.h>
# include "request_test.hpp"
# include "response.hpp"
# include "server.hpp"
# include "location.hpp"

class Router {
private:
	// Encontra a maior regra de location, /upload, /cgi-bin, /, etc.
	const Location*	_match_location(const std::string &uri, const std::vector<Location> &locations);
	
	// Constrói o caminho físico no disco
	std::string		_build_physical_path(const std::string &uri, const Location *loc);

	// Descobre se é html, css, png, etc.
	std::string		_get_mime_type(const std::string &filepath);
	// Gera a resposta de erro padrão (404, 403, 405)
	Response		_generate_error_response(int status_code, const Server &server);
	Response		_generate_autoindex(const std::string &filepath, const std::string &uri, const Server &server_config);

	Response		_handle_get_request(Request &req, const Server &server_config, const Location* best_match);
	Response		_handle_post_request(Request &req, const Server &server_config, const Location* best_match);
	Response		_handle_del_request(Request &req, const Server &server_config, const Location* best_match);

public:
	Router();
	~Router();

	// A função que vai ser chamada no handle_client_read
	Response		handle_request(Request &req, const Server &server_config);
};

#endif