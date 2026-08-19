/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   request_test.hpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ranhaia- <ranhaia-@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/19 18:15:19 by ranhaia-          #+#    #+#             */
/*   Updated: 2026/08/19 18:28:55 by ranhaia-         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef REQUEST_TEST_HPP
# define REQUEST_TEST_HPP

# include <iostream>

class	Request
{
private:
	std::string buffer;
	std::string	method;
	std::string	uri;
	
public:
	Request();
	~Request();
	std::string	get_method();
	std::string	get_uri();
	std::string	get_buffer();
	void		set_buffer(char *buffer);
	void		set_method(std::string buffer);
	void		set_uri(std::string buffer);
	
};

#endif