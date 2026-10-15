/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   router.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lbento <lbento@student.42.fr>              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/09/14 13:58:53 by ranhaia-          #+#    #+#             */
/*   Updated: 2026/09/26 23:38:21 by lbento           ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../includes/router.hpp"
#include "../includes/http_utils.hpp"
#include "../includes/mime.hpp"
#include <algorithm>
#include <iostream>

static bool	is_dir_path(const std::string &path)
{
	return !path.empty() && path[path.size() - 1] == '/';
}

// Extrai o "boundary" de um Content-Type multipart/form-data.
// Devolve string vazia se nao for multipart ou se faltar o parametro.
static std::string	get_multipart_boundary(const std::string &content_type)
{
	std::string	lower = HttpUtils::to_lower(content_type);
	size_t		pos = lower.find("multipart/form-data");

	if (pos == std::string::npos)
		return "";
	pos = lower.find("boundary=", pos);
	if (pos == std::string::npos)
		return "";
	std::string	boundary = content_type.substr(pos + 9);
	size_t		semi = boundary.find(';');
	if (semi != std::string::npos)
		boundary = boundary.substr(0, semi);
	boundary = HttpUtils::trim(boundary);
	// o boundary pode vir entre aspas: boundary="----xyz"
	if (boundary.size() >= 2 && boundary[0] == '"'
		&& boundary[boundary.size() - 1] == '"')
		boundary = boundary.substr(1, boundary.size() - 2);
	return boundary;
}

// Le o filename de um header Content-Disposition de uma parte do
// multipart (form-data; name="file"; filename="foto.png").
static std::string	extract_disposition_filename(const std::string &headers)
{
	std::string	lower = HttpUtils::to_lower(headers);
	size_t		pos = lower.find("content-disposition");

	if (pos == std::string::npos)
		return "";
	pos = lower.find("filename=", pos);
	if (pos == std::string::npos)
		return "";
	size_t		i = pos + 9;
	std::string	value;

	while (i < headers.size() && (headers[i] == ' ' || headers[i] == '\t'))
		i++;
	if (i < headers.size() && headers[i] == '"')
	{
		i++;
		while (i < headers.size() && headers[i] != '"')
		{
			if (headers[i] == '\\' && i + 1 < headers.size())
				i++;
			value += headers[i++];
		}
	}
	else
	{
		while (i < headers.size() && headers[i] != ';'
			&& headers[i] != '\r' && headers[i] != '\n')
			value += headers[i++];
	}
	return HttpUtils::trim(value);
}

// O filename vem do cliente: nunca pode escapar da pasta de upload.
// Fica so com o basename (barra normal e invertida sao separadores) e
// descarta nomes vazios, "." e "..".
static std::string	safe_upload_name(const std::string &name)
{
	size_t		pos = name.find_last_of("/\\");
	std::string	base = (pos == std::string::npos) ? name : name.substr(pos + 1);

	if (base.empty() || base == "." || base == "..")
		return "";
	if (base.find('\0') != std::string::npos)
		return "";
	return base;
}

// Grava cada parte com filename do corpo multipart em upload_dir,
// respeitando o nome que veio do formulario. Devolve true se ao menos
// um arquivo foi criado.
static bool	store_multipart_files(const std::string &body,
			const std::string &boundary, const std::string &upload_dir)
{
	std::string	delim = "--" + boundary;
	size_t		pos = 0;
	bool		stored = false;

	while (true)
	{
		size_t	start = body.find(delim, pos);
		if (start == std::string::npos)
			break;
		size_t	after = start + delim.size();
		// "--boundary--" fecha o corpo: acabaram as partes
		if (body.compare(after, 2, "--") == 0)
			break;
		if (body.compare(after, 2, "\r\n") == 0)
			after += 2;
		size_t	header_end = body.find("\r\n\r\n", after);
		if (header_end == std::string::npos)
			break;
		std::string	headers = body.substr(after, header_end - after);
		size_t		content_start = header_end + 4;
		size_t		next = body.find(delim, content_start);
		if (next == std::string::npos)
			break;
		size_t		content_end = next;
		// o CRLF que precede o proximo boundary nao faz parte do arquivo
		if (content_end >= 2 && body.compare(content_end - 2, 2, "\r\n") == 0)
			content_end -= 2;
		pos = next;

		std::string	filename =
			safe_upload_name(extract_disposition_filename(headers));
		if (filename.empty())
			continue; // campo de formulario comum, sem arquivo
		std::string	filepath = upload_dir + "/" + filename;
		std::ofstream	outfile(filepath.c_str(),
			std::ios::out | std::ios::binary);
		if (!outfile.is_open())
			return false;
		outfile.write(body.data() + content_start, content_end - content_start);
		outfile.close();
		stored = true;
	}
	return stored;
}

std::string     Router::_build_physical_path(const std::string &uri, const Location *loc)
{
	// O prefixo da location nao entra na URL que vira caminho de disco
	// (exemplo do subject: /kapouet -> /tmp/www)
	return Location::resolve_path(uri, *loc);
}

std::string     Router::_get_mime_type(const std::string &filepath)
{
	return (Mime::get_type(filepath));
}

Response        Router::_generate_error_response(int status_code, const Server &server)
{
	return (Response::error(status_code, server));
}

Response Router::_generate_autoindex(const std::string &filepath, const std::string &uri, const Server &server_config)
{
	Response res;
	DIR *dir;
	struct dirent *entry;
	std::string html;
	std::vector<std::string> names;

	dir = opendir(filepath.c_str());
	if (dir == NULL) {
		return _generate_error_response(403, server_config);
	}

	while ((entry = readdir(dir)) != NULL) {
		std::string filename = entry->d_name;
		if (filename == "." || filename == "..")
			continue;
		names.push_back(filename);
	}
	closedir(dir);
	std::sort(names.begin(), names.end());

	std::string base = uri;
	if (base.empty() || base[base.size() - 1] != '/')
		base += "/";

	html = "<html>\n<head><title>Index of " + HttpUtils::html_escape(uri) + "</title></head>\n";
	html += "<body>\n<h1>Index of " + HttpUtils::html_escape(uri) + "</h1><hr><pre>\n";

	for (size_t i = 0; i < names.size(); i++) {
		std::string name = names[i];
		std::string full = filepath;
		struct stat info;

		if (full.empty() || full[full.size() - 1] != '/')
			full += "/";
		full += name;
		if (stat(full.c_str(), &info) == 0 && S_ISDIR(info.st_mode))
			name += "/";
		html += "<a href=\"" + base + HttpUtils::url_encode(name) + "\">";
		html += HttpUtils::html_escape(name) + "</a>\n";
	}

	html += "</pre><hr>\n</body>\n</html>\n";

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
	std::string filepath = _build_physical_path(req.get_path(), best_match);
	struct	stat	path_info;
	if (stat(filepath.c_str(), &path_info) < 0)
		return _generate_error_response(404, server_config);
	std::string	dir_path = filepath;

	// Verifica se é diretório
	if (S_ISDIR(path_info.st_mode))
	{
		// Sem a barra final o browser resolve os links do autoindex relativos
		// ao pai; manda um 301 como o nginx faz.
		if (!is_dir_path(req.get_path()))
		{
			Response	redirect;
			std::string	location = req.get_path() + "/";

			if (!req.get_query().empty())
				location += "?" + req.get_query();
			redirect.set_status(301);
			redirect.set_header("Location", location);
			return (redirect);
		}
		std::string	index_file = best_match->get_index();
		if (index_file.empty())
		{
			if (best_match->get_autoindex())
				return _generate_autoindex(dir_path, req.get_path(), server_config);
			return _generate_error_response(404, server_config);
		}
		if (!is_dir_path(filepath))
			filepath += "/";
		filepath += index_file;
	}
	// Checa as permissões de leitura e se o arquivo existe
	if (access(filepath.c_str(), F_OK) != 0)
	{
		// criar autoindex
		if (best_match->get_autoindex())
			return _generate_autoindex(dir_path, req.get_path(), server_config);
		else
			return _generate_error_response(404, server_config);
	}
	// Sem permissão para ler
	if (access(filepath.c_str(), R_OK) != 0)
		return _generate_error_response(403, server_config);

	struct stat file_info;
	if (stat(filepath.c_str(), &file_info) < 0 || !S_ISREG(file_info.st_mode))
		return _generate_error_response(403, server_config);

	// Não lemos o arquivo aqui: o WebServ manda em blocos pra não
	// estourar a memoria com arquivos grandes
	response.set_status(200);
	response.set_header("Content-Type", _get_mime_type(filepath));
	response.set_file(filepath, static_cast<size_t>(file_info.st_size));
	return (response);
}

Response		Router::_handle_post_request(Request &req, const Server &server_config, const Location* best_match)
{
	Response	response;
	std::string upload_dir = best_match->get_upload_store();
		
	if (upload_dir.empty())
		return _generate_error_response(403, server_config);

	// Upload de formulario: o nome do arquivo esta no filename de cada
	// parte do multipart, nao na URL. Senao dois arquivos diferentes
	// virariam o mesmo "upload" (nome do ultimo segmento do path).
	std::string	boundary = get_multipart_boundary(req.get_header("content-type"));
	if (!boundary.empty())
	{
		if (!store_multipart_files(req.get_body(), boundary, upload_dir))
			return _generate_error_response(400, server_config);
		response.set_status(201); // 201 Created
		response.set_header("Content-Type", "text/plain");
		response.set_body("Upload realizado com sucesso!\n");
		return response;
	}

	size_t pos = req.get_path().find_last_of('/');
	std::string filename = (pos == std::string::npos) ? req.get_path()
	                                                  : req.get_path().substr(pos + 1);
	if (filename.empty())
		return _generate_error_response(400, server_config);

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
		size_t pos = req.get_path().find_last_of('/');
		std::string filename = (pos == std::string::npos) ? req.get_path()
		                                                  : req.get_path().substr(pos + 1);
		if (filename.empty())
			return _generate_error_response(400, server_config);
		filepath = upload_dir + "/" + filename;
	}
	else
	{
		filepath = _build_physical_path(req.get_path(), best_match);
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

Response Router::handle_request(Request &req, const Server &server_config, Cgi *cgi) {
	Response response;
	std::string uri = req.get_path();
	
	// Achar o Location
	const Location* best_match = Location::match(uri, server_config.get_locations());
	
	if (best_match == NULL)
		return _generate_error_response(404, server_config); // Not Found

	// Validação de Método — antes do redirect e do CGI: quem manda na
	// location e a lista "methods". E HEAD NAO herda de GET: o tester
	// oficial do 42 manda HEAD numa location marcada so com GET e espera
	// 405, entao quem quiser HEAD precisa declara-lo.
	std::string	req_method = req.get_method();
	const std::vector<std::string> &allowed = best_match->get_methods();
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
	{
		Response	refused = _generate_error_response(405, server_config);
		std::string	allow;

		for (size_t i = 0; i < allowed.size(); i++)
		{
			if (i > 0)
				allow += ", ";
			allow += allowed[i];
		}
		refused.set_header("Allow", allow);
		return refused; // Method Not Allowed
	}

	// Limite de corpo por location. O parser so tinha o valor do server na
	// hora de ler os headers (a location so fica conhecida depois da linha
	// de requisicao), entao e aqui que o estouro e barrado de vez.
	if (best_match->get_max_body_size() > 0
		&& req.get_body().size() > best_match->get_max_body_size())
		return _generate_error_response(413, server_config);

	// Redirecionamento configurado na location (nginx: return 301 ...)
	if (best_match->get_redirect_code() != 0)
	{
		response.set_status(best_match->get_redirect_code());
		response.set_header("Location", best_match->get_redirect_target());
		return response;
	}

	// CGI: um POST pra CGI não é upload — o corpo vai pro stdin do script,
	// não pro disco
	if (Cgi::is_cgi_path(uri, *best_match))
	{
		if (cgi == NULL)
			return _generate_error_response(500, server_config);
		// Aqui nao ha o que servir de arquivo: se o script nao existe nao
		// da pra invocar o interpretador, entao a resposta e 502 (mesmo
		// racional de "nao deu pra falar com o de tras" do nginx). O
		// tester oficial do 42 tem justamente um POST pra um .bla que nao
		// existe (/directory/youpla.bla).
		std::string	err;
		if (!cgi->start(req, server_config, *best_match,
		                Cgi::script_path_of(uri, *best_match), "", err))
		{
			std::cerr << "cgi: " << err << std::endl;
			return _generate_error_response(502, server_config);
		}
		response.set_is_cgi(true);
		return response;
	}

	if (req_method == "GET" || req_method == "HEAD")
		response = _handle_get_request(req, server_config, best_match);
	else if (req_method == "POST")
		response = _handle_post_request(req, server_config, best_match);
	else if (req_method == "DELETE")
		response = _handle_del_request(req, server_config, best_match);
	else
		return _generate_error_response(501, server_config); // Not implemented
	return response;
}