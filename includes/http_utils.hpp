/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   http_utils.hpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lbento <lbento@student.42.fr>              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/09/23 21:40:00 by lbento            #+#    #+#             */
/*   Updated: 2026/09/26 22:51:13 by lbento           ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef HTTP_UTILS_HPP
# define HTTP_UTILS_HPP

# include <string>

namespace HttpUtils
{
	std::string	to_lower(const std::string &str);
	std::string	trim(const std::string &str);
}

#endif
