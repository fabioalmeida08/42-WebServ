#ifndef LOCATION_HPP
#define LOCATION_HPP

#include <string>
#include <vector>

class Location {
  private:
    std::string _path;
    std::string _root;
    std::vector<std::string> _methods;
    std::string _index;
    bool _autoindex;
    std::string _upload_store;
    std::string _cgi_ext;
    std::string _cgi_exec;
    size_t _max_body_size;
    int _redirect_code;
    std::string _redirect_target;

  public:
    Location();
    Location(const std::string &path);

    static const Location *match(const std::string &uri,
                                 const std::vector<Location> &locations);
    static std::string resolve_path(const std::string &uri,
                                    const Location &loc);

    const std::string &get_path() const;
    const std::string &get_root() const;
    const std::vector<std::string> &get_methods() const;
    const std::string &get_index() const;
    bool get_autoindex() const;
    const std::string &get_upload_store() const;
    const std::string &get_cgi_ext() const;
    const std::string &get_cgi_exec() const;
    size_t get_max_body_size() const;
    int get_redirect_code() const;
    const std::string &get_redirect_target() const;

    void set_path(const std::string &path);
    void set_root(const std::string &root);
    void add_method(const std::string &method);
    void set_methods(const std::vector<std::string> &methods);
    void set_index(const std::string &index);
    void set_autoindex(bool autoindex);
    void set_upload_store(const std::string &upload_store);
    void set_cgi_ext(const std::string &cgi_ext);
    void set_cgi_exec(const std::string &cgi_exec);
    void set_max_body_size(size_t max_body_size);
    void set_redirect(int code, const std::string &target);
};

#endif
