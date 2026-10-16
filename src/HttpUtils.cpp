#include "../includes/HttpUtils.hpp"
#include <cctype>
#include <cstdio>
#include <ctime>

std::string	HttpUtils::to_lower(const std::string &str)
{
	std::string	result(str);

	for (size_t i = 0; i < result.size(); i++)
		result[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[i])));
	return result;
}

std::string	HttpUtils::trim(const std::string &str)
{
	size_t	start = str.find_first_not_of(" \t");

	if (start == std::string::npos)
		return "";
	size_t	end = str.find_last_not_of(" \t");
	return str.substr(start, end - start + 1);
}

std::string	HttpUtils::http_date()
{
	char		buffer[64];
	time_t		now = time(NULL);
	struct tm	*utc = gmtime(&now);

	if (utc == NULL)
		return "";
	if (strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", utc) == 0)
		return "";
	return std::string(buffer);
}

static bool	is_unreserved(unsigned char c)
{
	if (std::isalnum(c))
		return true;
	return c == '-' || c == '_' || c == '.' || c == '~';
}

std::string	HttpUtils::url_encode(const std::string &str)
{
	static const char	hex[] = "0123456789ABCDEF";
	std::string			encoded;

	for (size_t i = 0; i < str.size(); i++)
	{
		unsigned char	c = static_cast<unsigned char>(str[i]);

		if (is_unreserved(c))
			encoded += static_cast<char>(c);
		else
		{
			encoded += '%';
			encoded += hex[c / 16];
			encoded += hex[c % 16];
		}
	}
	return encoded;
}

std::string	HttpUtils::html_escape(const std::string &str)
{
	std::string	escaped;

	for (size_t i = 0; i < str.size(); i++)
	{
		if (str[i] == '&')
			escaped += "&amp;";
		else if (str[i] == '<')
			escaped += "&lt;";
		else if (str[i] == '>')
			escaped += "&gt;";
		else if (str[i] == '"')
			escaped += "&quot;";
		else
			escaped += str[i];
	}
	return escaped;
}
