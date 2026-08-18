#include <arpa/inet.h>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "../includes/parser.hpp"

static bool is_valid_method(const std::string &method) {
  return method == "GET" || method == "POST" || method == "DELETE";
}

static bool is_redirect_code(int code) {
  return code >= 300 && code < 400;
}

Parser::Parser(const std::string &filename) : _filename(filename), _pos(0) {
  std::ifstream file(filename.c_str());
  if (!file.is_open()) {
    throw std::runtime_error("config: nao foi possivel abrir o arquivo '" +
                             filename + "'");
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  tokenize(buffer.str());
}

void Parser::error(const std::string &message, int line) const {
  std::ostringstream oss;
  oss << _filename << ":" << line << ": error: " << message;
  throw std::runtime_error(oss.str());
}

void Parser::tokenize(const std::string &content) {
  size_t i = 0;
  int line = 1;
  const size_t size = content.size();

  while (i < size) {
    char c = content[i];

    if (c == '\n') {
      line++;
      i++;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(c))) {
      i++;
      continue;
    }
    if (c == '#') {
      while (i < size && content[i] != '\n')
        i++;
      continue;
    }
    if (c == '{' || c == '}' || c == ';') {
      Token token;
      token.value = std::string(1, c);
      token.line = line;
      _tokens.push_back(token);
      i++;
      continue;
    }

    std::string word;
    while (i < size && !std::isspace(static_cast<unsigned char>(content[i])) &&
           content[i] != '{' && content[i] != '}' && content[i] != ';' &&
           content[i] != '#') {
      word += content[i];
      i++;
    }

    if (!word.empty()) {
      Token token;
      token.value = word;
      token.line = line;
      _tokens.push_back(token);
    }
  }
}

bool Parser::has_next() const { return _pos < _tokens.size(); }

const Parser::Token &Parser::peek() const {
  if (_pos >= _tokens.size())
    error("fim inesperado do arquivo", -1);
  return _tokens[_pos];
}

void Parser::advance() { _pos++; }

void Parser::expect(const std::string &what, int line) {
  if (!has_next() || peek().value != what)
    error("esperava '" + what + "'", line);
  advance();
}

int Parser::to_int(const std::string &s, int line) const {
  for (size_t i = 0; i < s.size(); i++) {
    if (!std::isdigit(static_cast<unsigned char>(s[i])))
      error("'" + s + "' nao e um numero valido", line);
  }

  std::stringstream ss(s);
  long value;
  ss >> value;
  if (value > 65535 || value < 0)
    error("numero fora do intervalo 0-65535: '" + s + "'", line);
  return static_cast<int>(value);
}

size_t Parser::to_size(const std::string &s, int line) const {
  std::string number;
  long multiplier = 1;

  if (!s.empty()) {
    char last = s[s.size() - 1];
    if (last == 'k' || last == 'K')
      multiplier = 1024;
    else if (last == 'm' || last == 'M')
      multiplier = 1024 * 1024;
    else if (last == 'g' || last == 'G')
      multiplier = 1024 * 1024 * 1024;

    if (multiplier != 1)
      number = s.substr(0, s.size() - 1);
    else
      number = s;
  }

  for (size_t i = 0; i < number.size(); i++) {
    if (!std::isdigit(static_cast<unsigned char>(number[i])))
      error("'" + s + "' nao e um tamanho valido (ex: 100, 100k, 10M, 1G)",
            line);
  }

  std::stringstream ss(number);
  unsigned long long value;
  ss >> value;
  return static_cast<size_t>(value * static_cast<unsigned long long>(multiplier));
}

std::vector<std::string> Parser::read_values() {
  std::vector<std::string> values;

  while (has_next() && peek().value != ";" && peek().value != "{" &&
         peek().value != "}") {
    values.push_back(peek().value);
    advance();
  }

  if (!has_next() || peek().value != ";")
    error("esperava ';' ao final da diretiva", -1);
  advance();

  return values;
}

void Parser::parse(std::vector<Server> &servers) {
  while (has_next()) {
    int line = peek().line;
    if (peek().value != "server")
      error("esperava 'server', encontrado '" + peek().value + "'", line);

    advance();
    expect("{", line);

    Server server;
    parse_server(server);
    servers.push_back(server);
  }
}

void Parser::parse_server(Server &server) {
  while (has_next() && peek().value != "}") {
    int line = peek().line;
    std::string name = peek().value;
    advance();

    if (name == "location") {
      if (!has_next() || peek().value == "{" || peek().value == "}")
        error("diretiva 'location' espera um caminho", line);
      std::string path = peek().value;
      advance();
      if (path[0] != '/')
        error("caminho de location deve comecar com '/': '" + path + "'",
              line);
      expect("{", line);
      Location location(path);
      parse_location(location);
      server.add_location(location);
      continue;
    }

    std::vector<std::string> values = read_values();
    parse_server_directive(server, name, values, line);
  }

  if (!has_next())
    error("bloco 'server' nao foi fechado com '}'", -1);
  advance();
}

void Parser::parse_location(Location &location) {
  while (has_next() && peek().value != "}") {
    int line = peek().line;
    std::string name = peek().value;
    advance();

    if (name == "location") {
      error("bloco 'location' aninhado nao e permitido", line);
      continue;
    }

    std::vector<std::string> values = read_values();
    parse_location_directive(location, name, values, line);
  }

  if (!has_next())
    error("bloco 'location' nao foi fechado com '}'", -1);
  advance();
}

void Parser::parse_server_directive(Server &server, const std::string &name,
                                    const std::vector<std::string> &values,
                                    int line) {
  if (name == "listen") {
    if (values.size() != 1)
      error("diretiva 'listen' espera 1 valor", line);
    server.set_port(to_int(values[0], line));
  } else if (name == "server_name") {
    if (values.size() < 1)
      error("diretiva 'server_name' espera ao menos 1 valor", line);
    for (size_t i = 0; i < values.size(); i++)
      server.add_name(values[i]);
  } else if (name == "error_page") {
    if (values.size() != 2)
      error("diretiva 'error_page' espera 2 valores (codigo caminho)", line);
    int code = to_int(values[0], line);
    if (code < 100 || code > 599)
      error("codigo de erro invalido: '" + values[0] + "'", line);
    server.set_error_page(code, values[1]);
  } else if (name == "host") {
    if (values.size() != 1)
      error("diretiva 'host' espera 1 valor", line);
    struct in_addr addr;
    if (inet_pton(AF_INET, values[0].c_str(), &addr) != 1)
      error("host invalido: '" + values[0] + "' (esperado IPv4)", line);
    server.set_host(values[0]);
  } else if (name == "client_max_body_size") {
    if (values.size() != 1)
      error("diretiva 'client_max_body_size' espera 1 valor", line);
    server.set_client_max_body(to_size(values[0], line));
  } else {
    error("diretiva desconhecida '" + name + "'", line);
  }
}

void Parser::parse_location_directive(Location &location,
                                      const std::string &name,
                                      const std::vector<std::string> &values,
                                      int line) {
  if (name == "root") {
    if (values.size() != 1)
      error("diretiva 'root' espera 1 valor", line);
    location.set_root(values[0]);
  } else if (name == "methods") {
    if (values.size() < 1)
      error("diretiva 'methods' espera ao menos 1 metodo", line);
    for (size_t i = 0; i < values.size(); i++) {
      if (!is_valid_method(values[i]))
        error("metodo invalido: '" + values[i] + "' (esperado GET, POST ou "
              "DELETE)",
              line);
      location.add_method(values[i]);
    }
  } else if (name == "index") {
    if (values.size() != 1)
      error("diretiva 'index' espera 1 valor", line);
    location.set_index(values[0]);
  } else if (name == "autoindex") {
    if (values.size() != 1)
      error("diretiva 'autoindex' espera 1 valor (on/off)", line);
    if (values[0] == "on")
      location.set_autoindex(true);
    else if (values[0] == "off")
      location.set_autoindex(false);
    else
      error("valor invalido para 'autoindex': '" + values[0] +
                "' (esperado on/off)",
            line);
  } else if (name == "upload_store") {
    if (values.size() != 1)
      error("diretiva 'upload_store' espera 1 valor", line);
    location.set_upload_store(values[0]);
  } else if (name == "cgi") {
    if (values.size() != 1)
      error("diretiva 'cgi' espera 1 valor", line);
    if (values[0].empty() || values[0][0] != '.')
      error("extensao de cgi deve comecar com '.': '" + values[0] + "'",
            line);
    location.set_cgi_ext(values[0]);
  } else if (name == "redirect") {
    if (values.size() != 2)
      error("diretiva 'redirect' espera 2 valores (codigo destino)", line);
    int code = to_int(values[0], line);
    if (!is_redirect_code(code))
      error("codigo de redirect invalido (esperado 3xx): '" + values[0] +
                "'",
            line);
    location.set_redirect(code, values[1]);
  } else {
    error("diretiva desconhecida '" + name + "'", line);
  }
}

std::vector<Server> Parser::get_servers() {
  std::vector<Server> servers;
  parse(servers);
  return servers;
}
