#ifndef ROUTER_HPP
# define ROUTER_HPP

# include <string>
# include <vector>
# include <fstream>
# include <unistd.h>
# include <sys/stat.h>
# include <dirent.h>
# include "request.hpp"
# include "response.hpp"
# include "server.hpp"
# include "location.hpp"
# include "cgi.hpp"

class Router {
private:
	std::string		_build_physical_path(const std::string &uri, const Location *loc);

	std::string		_get_mime_type(const std::string &filepath);
	Response		_generate_error_response(int status_code, const Server &server);
	Response		_generate_autoindex(const std::string &filepath, const std::string &uri, const Server &server_config);

	Response		_handle_get_request(Request &req, const Server &server_config, const Location* best_match);
	Response		_handle_post_request(Request &req, const Server &server_config, const Location* best_match);
	Response		_handle_del_request(Request &req, const Server &server_config, const Location* best_match);

public:
	Router();
	~Router();

	Response		handle_request(Request &req, const Server &server_config,
		                       Cgi *cgi = NULL);
};

#endif
