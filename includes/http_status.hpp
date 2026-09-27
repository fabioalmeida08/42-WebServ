/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   http_status.hpp                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lbento <lbento@student.42.fr>              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/18 12:22:47 by lbento            #+#    #+#             */
/*   Updated: 2026/09/26 22:50:25 by lbento           ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef HTTP_STATUS_HPP
# define HTTP_STATUS_HPP

# include <string>

namespace HttpStatus
{
	std::string	reason(int code);
}

#endif
