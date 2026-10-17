#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

#include "../includes/parser.hpp"
#include "../includes/webserv.hpp"

static std::string	resolve_config(int argc, char *argv[])
{
	if (argc == 2)
		return std::string(argv[1]);
	if (argc == 1)
	{
		if (access("default.conf", R_OK) == 0)
			return std::string("default.conf");
		if (access("conf/default.conf", R_OK) == 0)
			return std::string("conf/default.conf");
	}
	throw std::invalid_argument("Expected: ./webserv [config_file]");
}

int main(int argc, char *argv[])
{
	try
	{
		std::vector<Server> servers;
		servers = Parser(resolve_config(argc, argv)).get_servers();

		WebServ server;
		server.loadConfig(servers);
		if (!server.run())
			return 1;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << std::endl;
		return 1;
	}
	return 0;
}
