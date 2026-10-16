/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpStatus.hpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lbento <lbento@student.42.fr>              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/18 12:22:47 by lbento            #+#    #+#             */
/*   Updated: 2026/10/16 00:05:07 by lbento           ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef HTTPSTATUS_HPP
# define HTTPSTATUS_HPP

# include <string>

namespace HttpStatus
{
	std::string	reason(int code);
}

#endif
