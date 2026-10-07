/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   main.cpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lbento <lbento@student.42.fr>              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/10/07 05:16:07 by lbento            #+#    #+#             */
/*   Updated: 2026/10/07 05:16:32 by lbento           ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include <iostream>
#include <stdexcept>

#include "../includes/parser.hpp"
#include "../includes/webserv.hpp"

int main(int argc, char *argv[])
{
	try
	{
		std::vector<Server> servers;
		if (argc == 2)
			servers = Parser(argv[1]).get_servers();
		else
			throw std::invalid_argument("Expected: ./webserv <config_file>");

		WebServ server;
		server.loadConfig(servers);
		server.run();
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << std::endl;
		return 1;
	}
	return 0;
}
