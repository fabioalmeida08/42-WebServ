/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   router.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ranhaia- <ranhaia-@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/09/14 13:58:53 by ranhaia-          #+#    #+#             */
/*   Updated: 2026/09/14 20:06:28 by ranhaia-         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../includes/router.hpp"

const Location* Router::_match_location(const std::string &uri, const std::vector<Location> &locations)
{
	const	Location*	best_match = NULL;
	size_t				max_len = 0;

	for (size_t i = 0; i < locations.size(); i++)
	{
		if (uri.find(locations[i].get_path()) == 0)
		{
			if (locations[i].get_path().length() > max_len)
			{
				max_len = locations[i].get_path().length();
				best_match = &locations[i];
			}
		}
	}
	return (best_match);
}

std::string     Router::_build_physical_path(const std::string &uri, const Location *loc)
{
	std::string	dir;
	std::string	final_path;

	dir = loc->get_root();
	final_path = dir + uri;

	return (final_path);
}

std::string     Router::_get_mime_type(const std::string &filepath)
{
	size_t		pos = filepath.find_last_of('.');
	std::string	mime_type;

	if (pos == std::string::npos)
		return ("application/octet-stream");
	mime_type = filepath.substr(pos);
	if (mime_type == ".html")
		return ("text/html");	
	else if (mime_type == ".css")
		return ("text/css");
	else if (mime_type == ".png")
		return ("image/png");
	else if (mime_type == ".jpg" || mime_type == ".jpeg")
		return ("image/jpeg");
	else if (mime_type == ".py")
		return ("text/plain");
	else
		return ("text/plain");
}

Response        Router::_generate_error_response(int status_code, const Server &server)
{
	Response	err;
	std::map<int, std::string> pages = server.get_error_pages();
	std::map<int, std::string>::const_iterator it = pages.find(status_code);
	int	page_loaded = 0;

	err.set_status(status_code);
	err.set_header("Content-Type", "text/html");

	if (it != pages.end())
	{
		std::string	filepath = it->second;
		if (!filepath.empty() && filepath[0] == '/')
			filepath = "." + filepath;
		std::ifstream file(filepath.c_str());
		if (file.is_open())
		{
			std::string error_page((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			err.set_body(error_page);
			page_loaded = 1;
			file.close();
		}
	}
	if (!page_loaded)
	{
		std::stringstream ss;
		ss << status_code;
		std::string	status_msg = err.get_status_msg();
		std::string err_msg = "<html><body><h1> " + ss.str() + " - " + status_msg + " </h1></body></html>";
		err.set_body(err_msg);
	}
	return (err);
}

Response Router::_generate_autoindex(const std::string &filepath, const std::string &uri, const Server &server_config)
{
	Response res;
	DIR *dir;
	struct dirent *entry;
	std::string html;

	// Tenta abrir o diretório
	dir = opendir(filepath.c_str());
	if (dir == NULL) {
		return _generate_error_response(403, server_config);
	}

	// Cabeçalho do index
	html = "<html><head><title>Index of " + uri + "</title></head><body>";
	html += "<h1>Index of " + uri + "</h1><hr><pre>";

	while ((entry = readdir(dir)) != NULL) {
		std::string filename = entry->d_name;
		
		if (filename == ".") continue;
		std::string href = uri;
		if (href[href.length() - 1] != '/')
			href += "/";
		href += filename;

		if (entry->d_type == DT_DIR) {
			filename += "/";
		}
		html += "<a href=\"" + href + "\">" + filename + "</a>\n";
	}

	html += "</pre><hr></body></html>";
	closedir(dir);

	// Preenche a Response e retorna
	res.set_status(200);
	res.set_header("Content-Type", "text/html");
	res.set_body(html);

	return res;
}

Router::Router()
{
}
Router::~Router()
{
	
}

Response		Router::_handle_get_request(Request &req, const Server &server_config, const Location* best_match)
{
	Response	response;
	// Retorna onde o arquivo está
	std::string filepath = _build_physical_path(req.get_uri(), best_match);
	struct	stat	path_info;
	if (stat(filepath.c_str(), &path_info) < 0)
		return _generate_error_response(404, server_config);
	std::string	dir_path = filepath;

	// Verifica se é diretório
	if (S_ISDIR(path_info.st_mode))
	{
		std::string	index_file = best_match->get_index();
		if (!index_file.empty())
		{
			if (filepath[filepath.length() - 1] != '/')
				filepath += "/";
			filepath += index_file;
		}
	}
	// Checa as permissões de leitura e se o arquivo existe
	if (access(filepath.c_str(), F_OK) != 0)
	{
		// criar autoindex
		if (best_match->get_autoindex())
			return _generate_autoindex(dir_path, req.get_uri(), server_config);
		else
			return _generate_error_response(404, server_config);
	}
	// Sem permissão para ler
	if (access(filepath.c_str(), R_OK) != 0)
		return _generate_error_response(403, server_config);
	std::ifstream file(filepath.c_str());
	std::string body((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	file.close();

	response.set_status(200);
	response.set_header("Content-Type", _get_mime_type(filepath));
	response.set_body(body);
	return (response);
}

Response		Router::_handle_post_request(Request &req, const Server &server_config, const Location* best_match)
{
	Response	response;
	std::string upload_dir = best_match->get_upload_store();
		
	if (upload_dir.empty())
		return _generate_error_response(403, server_config);

	size_t pos = req.get_uri().find_last_of('/');
	std::string filename;
	if (pos == std::string::npos)
		filename = req.get_uri();
	else
		filename = req.get_uri().substr(pos + 1);

	std::string filepath = upload_dir + "/" + filename;

	std::ofstream outfile(filepath.c_str(), std::ios::out | std::ios::binary);
	if (!outfile.is_open())
		return _generate_error_response(500, server_config); // Erro interno: pasta /tmp/uploads não existe

	std::string req_body = req.get_body();
	outfile.write(req_body.data(), req_body.size());
	outfile.close();

	response.set_status(201); // 201 Created
	response.set_header("Content-Type", "text/plain");
	response.set_body("Upload realizado com sucesso!\n");
	return response;
}

Response		Router::_handle_del_request(Request &req, const Server &server_config, const Location* best_match)
{
	Response	response;
	std::string filepath;
	std::string upload_dir = best_match->get_upload_store();

	// Se essa rota lida com uploads, o arquivo está lá
	if (!upload_dir.empty())
	{
		size_t pos = req.get_uri().find_last_of('/');
		std::string filename;
		if (pos == std::string::npos)
			filename = req.get_uri();
		else
			filename = req.get_uri().substr(pos + 1);
			
		filepath = upload_dir + "/" + filename;
	}
	else
	{
		filepath = _build_physical_path(req.get_uri(), best_match);
	}
	// Verifica se o arquivo existe
	if (access(filepath.c_str(), F_OK) != 0)
		return _generate_error_response(404, server_config);
		
	// A função std::remove apaga o arquivo
	if (std::remove(filepath.c_str()) == 0)
	{
		response.set_status(204); // 204 No Content (Apagou com sucesso, nada a retornar)
		response.set_header("Content-Length", "0");
		return response;
	} else
		return _generate_error_response(403, server_config); // Sem permissão para apagar
}

Response Router::handle_request(Request &req, const Server &server_config) {
	Response response;
	std::string uri = req.get_uri();
	
	// Achar o Location
	const Location* best_match = _match_location(uri, server_config.get_locations());
	
	if (best_match == NULL)
		return _generate_error_response(404, server_config); // Not Found

	// Validação de Método
	std::string	req_method = req.get_method();
	std::vector<std::string> allowed = best_match->get_methods();
	bool is_allowed = false;
	
	for (size_t i = 0; i < allowed.size(); i++)
	{
		if (allowed[i] == req_method)
		{
			is_allowed = true;
			break;
		}
	}
	if (!is_allowed)
		return _generate_error_response(405, server_config); // Method Not Allowed

	if (req_method == "GET")
		response = _handle_get_request(req, server_config, best_match);
	else if (req_method == "POST")
		response = _handle_post_request(req, server_config, best_match);
	else if (req_method == "DELETE")
		response = _handle_del_request(req, server_config, best_match);
	else
		return _generate_error_response(501, server_config); // Not implemented
	return response;
}