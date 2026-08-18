/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   server.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: fabio </var/spool/mail/fabio>              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/18 00:00:00 by fabio             #+#    #+#             */
/*   Updated: 2026/08/18 00:00:00 by fabio            ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef SERVER_HPP
#define SERVER_HPP

#include <map>
#include <string>
#include <vector>

#include "../includes/location.hpp"

class Server {
  private:
    int _port;
    std::string _host;
    std::vector<std::string> _names;
    std::map<int, std::string> _error_pages;
    size_t _client_max_body;
    std::vector<Location> _locations;

  public:
    Server();

    int get_port() const;
    const std::string &get_host() const;
    const std::vector<std::string> &get_names() const;
    const std::map<int, std::string> &get_error_pages() const;
    size_t get_client_max_body() const;
    const std::vector<Location> &get_locations() const;

    void set_port(int port);
    void set_host(const std::string &host);
    void add_name(const std::string &name);
    void set_error_page(int code, const std::string &path);
    void set_client_max_body(size_t max_body);
    void add_location(const Location &location);
};

#endif
