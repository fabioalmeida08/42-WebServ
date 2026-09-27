/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   mime.cpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lbento <lbento@student.42.fr>              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/18 12:29:51 by lbento            #+#    #+#             */
/*   Updated: 2026/09/26 23:18:48 by lbento           ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../includes/mime.hpp"
#include "../includes/http_utils.hpp"

struct MimeEntry
{
	const char	*extension;
	const char	*type;
};

static const MimeEntry	g_mime_types[] = {
	{"html", "text/html"},
	{"htm", "text/html"},
	{"css", "text/css"},
	{"js", "application/javascript"},
	{"json", "application/json"},
	{"xml", "application/xml"},
	{"txt", "text/plain"},
	{"png", "image/png"},
	{"jpg", "image/jpeg"},
	{"jpeg", "image/jpeg"},
	{"gif", "image/gif"},
	{"svg", "image/svg+xml"},
	{"ico", "image/x-icon"},
	{"webp", "image/webp"},
	{"pdf", "application/pdf"},
	{"zip", "application/zip"},
	{"mp3", "audio/mpeg"},
	{"mp4", "video/mp4"},
	{"woff", "font/woff"},
	{"woff2", "font/woff2"}
};

std::string	Mime::get_type(const std::string &path)
{
	size_t	dot = path.rfind('.');
	size_t	slash = path.rfind('/');

	if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
		return "application/octet-stream";
	std::string	extension = HttpUtils::to_lower(path.substr(dot + 1));
	for (size_t i = 0; i < sizeof(g_mime_types) / sizeof(g_mime_types[0]); i++)
	{
		if (extension == g_mime_types[i].extension)
			return g_mime_types[i].type;
	}
	return "application/octet-stream";
}
