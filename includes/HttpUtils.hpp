#ifndef HTTPUTILS_HPP
# define HTTPUTILS_HPP

# include <string>

namespace HttpUtils
{
	std::string	to_lower(const std::string &str);
	std::string	trim(const std::string &str);
	std::string	http_date();
	std::string	url_encode(const std::string &str);
	std::string	html_escape(const std::string &str);
}

#endif
