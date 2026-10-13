/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   location.cpp                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lbento <lbento@student.42.fr>              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/09/26 23:17:33 by lbento            #+#    #+#             */
/*   Updated: 2026/09/26 23:17:55 by lbento           ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../includes/location.hpp"

static bool	is_dir_path(const std::string &path)
{
	return !path.empty() && path[path.size() - 1] == '/';
}

// O prefixo precisa bater na fronteira de um segmento: "location /files" nao
// pode casar com "/files2".
static bool	path_matches(const std::string &uri, const std::string &path)
{
	if (uri.compare(0, path.size(), path) != 0)
		return false;
	if (is_dir_path(path) || uri.size() == path.size())
		return true;
	return uri[path.size()] == '/';
}

const Location *Location::match(const std::string &uri,
                                const std::vector<Location> &locations) {
  const Location *best_match = NULL;
  size_t max_len = 0;

  for (size_t i = 0; i < locations.size(); i++) {
    if (path_matches(uri, locations[i].get_path()) &&
        locations[i].get_path().length() > max_len) {
      max_len = locations[i].get_path().length();
      best_match = &locations[i];
    }
  }
  return best_match;
}

std::string Location::resolve_path(const std::string &uri, const Location &loc) {
  std::string root = loc.get_root();
  const std::string &prefix = loc.get_path();
  std::string suffix = uri;

  if (uri.size() >= prefix.size() && uri.compare(0, prefix.size(), prefix) == 0)
    suffix = uri.substr(prefix.size());
  while (!root.empty() && root[root.size() - 1] == '/')
    root.erase(root.size() - 1);
  if (suffix.empty())
    return root;
  if (suffix[0] != '/')
    suffix.insert(suffix.begin(), '/');
  if (root.empty())
    return suffix;
  return root + suffix;
}

Location::Location()
    : _path("/"), _root("www"), _index("index.html"), _autoindex(false),
      _max_body_size(0), _redirect_code(0) {
  _methods.push_back("GET");
}

Location::Location(const std::string &path)
    : _path(path), _root("www"), _index("index.html"), _autoindex(false),
      _max_body_size(0), _redirect_code(0) {
  _methods.push_back("GET");
}

const std::string &Location::get_path() const { return _path; }

const std::string &Location::get_root() const { return _root; }

const std::vector<std::string> &Location::get_methods() const {
  return _methods;
}

const std::string &Location::get_index() const { return _index; }

bool Location::get_autoindex() const { return _autoindex; }

const std::string &Location::get_upload_store() const { return _upload_store; }

const std::string &Location::get_cgi_ext() const { return _cgi_ext; }

const std::string &Location::get_cgi_exec() const { return _cgi_exec; }

size_t Location::get_max_body_size() const { return _max_body_size; }

int Location::get_redirect_code() const { return _redirect_code; }

const std::string &Location::get_redirect_target() const {
  return _redirect_target;
}

void Location::set_path(const std::string &path) { _path = path; }

void Location::set_root(const std::string &root) { _root = root; }

void Location::add_method(const std::string &method) {
  _methods.push_back(method);
}

void Location::set_methods(const std::vector<std::string> &methods)
{
  _methods = methods;
}

void Location::set_index(const std::string &index) { _index = index; }

void Location::set_autoindex(bool autoindex) { _autoindex = autoindex; }

void Location::set_upload_store(const std::string &upload_store) {
  _upload_store = upload_store;
}

void Location::set_cgi_ext(const std::string &cgi_ext) {
  _cgi_ext = cgi_ext;
}

void Location::set_cgi_exec(const std::string &cgi_exec) {
  _cgi_exec = cgi_exec;
}

void Location::set_max_body_size(size_t max_body_size) {
  _max_body_size = max_body_size;
}

void Location::set_redirect(int code, const std::string &target) {
  _redirect_code = code;
  _redirect_target = target;
}
