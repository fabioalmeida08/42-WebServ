#include "../includes/location.hpp"

Location::Location()
    : _path("/"), _root("html"), _index("index.html"), _autoindex(false),
      _redirect_code(0) {
  _methods.push_back("GET");
}

Location::Location(const std::string &path)
    : _path(path), _root("html"), _index("index.html"), _autoindex(false),
      _redirect_code(0) {
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

int Location::get_redirect_code() const { return _redirect_code; }

const std::string &Location::get_redirect_target() const {
  return _redirect_target;
}

void Location::set_path(const std::string &path) { _path = path; }

void Location::set_root(const std::string &root) { _root = root; }

void Location::add_method(const std::string &method) {
  _methods.push_back(method);
}

void Location::set_index(const std::string &index) { _index = index; }

void Location::set_autoindex(bool autoindex) { _autoindex = autoindex; }

void Location::set_upload_store(const std::string &upload_store) {
  _upload_store = upload_store;
}

void Location::set_cgi_ext(const std::string &cgi_ext) {
  _cgi_ext = cgi_ext;
}

void Location::set_redirect(int code, const std::string &target) {
  _redirect_code = code;
  _redirect_target = target;
}
