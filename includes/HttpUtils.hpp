/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpUtils.hpp                                      :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lbento <lbento@student.42.fr>              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/09/23 21:40:00 by lbento            #+#    #+#             */
/*   Updated: 2026/10/16 00:05:01 by lbento           ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

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
