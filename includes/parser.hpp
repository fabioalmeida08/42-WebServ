/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   parser.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: fabio </var/spool/mail/fabio>              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/18 00:00:00 by fabio             #+#    #+#             */
/*   Updated: 2026/08/18 00:00:00 by fabio            ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef PARSER_HPP
#define PARSER_HPP

#include <string>
#include <vector>

#include "../includes/server.hpp"

class Parser {
  private:
    struct Token {
      std::string value;
      int line;
    };

    std::string _filename;
    std::vector<Token> _tokens;
    size_t _pos;

    void tokenize(const std::string &content);
    void parse(std::vector<Server> &servers);
    void parse_server(Server &server);
    void parse_location(Location &location);
    void parse_server_directive(Server &server, const std::string &name,
                                const std::vector<std::string> &values,
                                int line);
    void parse_location_directive(Location &location, const std::string &name,
                                  const std::vector<std::string> &values,
                                  int line);

    std::vector<std::string> read_values();
    const Token &peek() const;
    void advance();
    bool has_next() const;
    void expect(const std::string &what, int line);
    int to_int(const std::string &s, int line) const;
    size_t to_size(const std::string &s, int line) const;
    void error(const std::string &message, int line) const;

  public:
    explicit Parser(const std::string &filename);
    std::vector<Server> get_servers();
};

#endif
