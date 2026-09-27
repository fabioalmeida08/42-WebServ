/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   http_utils.cpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lbento <lbento@student.42.fr>              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/09/23 21:40:00 by lbento            #+#    #+#             */
/*   Updated: 2026/09/26 22:30:09 by lbento           ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../includes/http_utils.hpp"
#include <cctype>

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
