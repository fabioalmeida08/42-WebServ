# Plano de implementação — Webserv (branch `dev` → pronto pro CGI)

> Status: **aguardando confirmação antes de colocar em prática.**
> Escopo: somente parte obrigatória do subject v24.1. **Sem bonus** (cookies/sessão e múltiplos CGI types ficam de fora).
> Premissa de avaliação: Linux (sem testes de portabilidade para macOS).

---

## Resumo do diagnóstico (validado com testes reais na `dev`)

O core está sólido: poll único, non-blocking, parser HTTP com chunked deschunkado, GET/POST/DELETE,
upload, autoindex, `client_max_body_size`, múltiplas portas, compila limpo com
`-Wall -Wextra -Werror -std=c++98`.

Faltando / quebrado:

| # | Item | Impacto |
|---|------|---------|
| 0.1 | `README.md` com 1 linha (subject exige seções específicas) | Reprova direto |
| 0.2 | Makefile sem a regra `$(NAME)` | Reprova direto |
| 1.1 | `error_page` do config nunca é aplicado (path não resolve contra o root) | Feature obrigatória quebrada |
| 1.2 | `redirect` é parseado mas nunca usado no Router | Feature obrigatória faltando |
| 1.3 | Nenhum timeout: conexões ociosas ficam presas para sempre | "Never hang indefinitely" + fd leak |
| 2.x | Vários bugs médios (tabela abaixo) | Nota / demonstração |
| 2.9 | Arquivo grande lido inteiro na memória | Risco de OOM = nota 0 |
| 3.x | CGI inexistente | Metade do projeto |

---

## Fase 0 — Rápidos e independentes

### 0.1 README.md
- **Arquivo:** `README.md`
- Conteúdo, nesta ordem:
  1. Primeira linha em itálico: `*This project has been created as part of the 42 curriculum by <login1>[, <login2>...]*` (logins do grupo).
  2. `## Description` — objetivo e arquitetura em 2 parágrafos (poll único, event loop, classes `Parser` / `RequestParser` / `Router` / `Response`).
  3. `## Instructions` — requisitos, `make`, `./bin/webserv default.conf`, estrutura de diretórios.
  4. `## Resources` — RFC 9110/9112, MDN, tutorial nginx, **descrição de uso de IA** (quais tarefas e em quais partes).
- **Aceite:** linha 1 em itálico, as 4 seções presentes, em inglês.

### 0.2 Makefile: regra `$(NAME)`
- **Arquivo:** `Makefile`
- Adicionar `$(NAME): $(TARGET)` e colocar `$(NAME)` no `.PHONY`; `all: $(NAME)`.
- Opcional: `.o` dependem dos `.hpp` (`$(shell find includes -name '*.hpp')`) pra não reconectar à toa.
- **Aceite:** `make $(NAME)`, `make`, `make re`, `make clean`, `make fclean`.

### 0.3 Fallback de config default
- **Arquivo:** `src/main.cpp`
- `argc == 1` → tenta `default.conf` (depois `conf/default.conf`) em vez de morrer; `argc > 2` → erro de uso.
- **Aceite:** `./bin/webserv` sobe com o `default.conf` da raiz.

### 0.4 Limpeza
- Remover o diretório `html/` (sobra) e trocar o default de `root` em `src/location.cpp` (`html` → `www`).
- `.gitignore`: ignorar `www/upload/*` mantendo o `.gitkeep`.

---

## Fase 1 — Obrigatoriedades quebradas

> Feita antes do CGI porque mexe nas mesmas partes (event loop, `Response`, `Router`).

### 1.1 `error_page` aplicado de verdade
- **Arquivos:** `src/response.cpp` (`Response::error`), `includes/server.hpp`, `src/server.cpp`, `src/router.cpp` (reusar a lógica de match)
- Hoje `Response::error()` faz `read_file("/errors/400.html")` — path relativo nunca resolve e sempre cai na página default gerada (157 bytes).
- **Mudança:** adicionar `std::string Server::resolve_error_page(int code) const` que resolve o path do `error_page` contra o root da location que casa esse caminho (fallback: location `/`, depois primeira location). `Response::error()` usa esse caminho físico e mantém a página default como fallback (obrigatório: *"must have default error pages if none are provided"*).
- **Aceite:** `curl -i /naoexiste` devolve o HTML estilizado de `www/errors/404.html`, não o default.

### 1.2 Redirecionamento HTTP (`redirect`)
- **Arquivos:** `src/router.cpp` (`Router::handle_request`), `src/response.cpp`
- Depois do match de location e antes do dispatch por método:
  ```cpp
  if (best_match->get_redirect_code() != 0) {
      Response r;
      r.set_status(best_match->get_redirect_code());
      r.set_header("Location", best_match->get_redirect_target());
      return r; // 3xx sem body
  }
  ```
- `Response::status_has_body()` passa a excluir 1xx, 3xx e 304 (hoje só 204/304).
- **Config:** exemplo no `default.conf` (ex.: `location /ancien { redirect 301 /index.html; methods GET; }`).
- **Aceite:** `curl -i /ancien` → `301` + `Location: /index.html`.

### 1.3 Timeout de request (base pro timeout do CGI)
- **Arquivos:** `includes/webserv.hpp` (`struct Client`), `src/webserv.cpp`
- `Client` ganha `time_t last_activity` (atualizado a cada `feed`/`send`).
- `poll()` com timeout `-1` → `1000` ms; após cada retorno, varrer os clientes e expirar os ociosos (`now - last_activity > 60s` com parser ainda incompleto) → responder **408** e fechar.
- Regra: request já completo nunca expira no meio do envio.
- **Aceite:** script Python abrindo 50 conexões com request pela metade → todas fechadas em ~60s com 408, fds voltam ao normal, servidor segue respondendo.

### 1.4 Refatoração do event loop (pré-requisito do CGI)
- **Arquivos:** `includes/webserv.hpp`, `src/webserv.cpp`
- `_poll_fds` vira vetor de contexto em vez de `struct pollfd` puro:
  ```cpp
  struct FdCtx { int fd; short events; enum Type { LISTEN, CLIENT_IN, CLIENT_OUT, CGI_IN, CGI_OUT } type; int owner; };
  ```
- `Client` vira máquina de estados:
  ```cpp
  enum ClientState { READING, WRITING_RESPONSE, SENDING_FILE, CGI_RUNNING, CLOSING };
  ```
- Isso elimina o `else if (POLLIN) ... else if (POLLOUT)` atual (que impede POLLIN e POLLOUT ao mesmo tempo) e permite 1 cliente com até 3 fds (socket + stdin + stdout do CGI).
- **Refactor puro**: commit separado, sem mudança de comportamento externo.
- **Aceite:** toda a bateria de testes continua passando idêntica.

---

## Fase 2 — Bugs médios

1. **POST com URI terminando em `/`** → hoje 500 (filename vazio); virar **400** (`src/router.cpp`).
2. **POST sem `Content-Length`** → hoje aceita body vazio; virar **411** (`src/request_parser.cpp::start_body`).
3. **`Allow:`** no 405 + headers **`Date`** e **`Server`** nas respostas (`src/router.cpp`, `src/response.cpp`, `src/http_utils.cpp`).
4. **`HEAD`** → hoje 501; tratar como GET **sem corpo** (`src/request_parser.cpp` + flag `no_body` no `Response`).
5. **Match de location** por fronteira de segmento — hoje `location /files` casa com `/files2` (`src/router.cpp::_match_location`).
6. **Autoindex** — sem entries `.` e `..`, `href` URL-encoded, `d_type` com fallback pra `stat`, ordenar entradas.
7. **Diretório sem `/` final** → **301** com `Location: <path>/` preservando a query (`src/router.cpp::_handle_get_request`); conserta também os links relativos do autoindex.

---

## Fase 2.9 — Envio de arquivo em blocos (antecipa estrutura do CGI)

- **Arquivos:** `includes/webserv.hpp`, `src/webserv.cpp`, `src/router.cpp`, `includes/response.hpp`
- Hoje `std::ifstream` lê o arquivo inteiro na memória → risco de OOM (subject: *"must not crash under any circumstances (even if it runs out of memory)"*).
- **Mudança:** `Response` ganha um "file response" (path + `Content-Length` do `stat`); o `WebServ` entra no estado `SENDING_FILE`: abre o arquivo, `read()` em blocos de 64 KB, `send()` só via POLLOUT, `close()` no fim. Disco não precisa de poll (isento pelo subject), mas o socket continua mandando via poll.
- **Aceite:** `dd` de 500 MB em `www/files/`; RSS do processo estável durante o download.

---

## Fase 3 — CGI

### 3.1 Config e scripts
- `default.conf` já tem `location /cgi-bin { root www; methods GET POST; cgi .py; }`. Criar `www/cgi-bin/upload.py` (lê o body do stdin e grava em `www/upload/`) pra demostrar POST via CGI.
- Os `.py` estão `644` → `execve` direto falha. `chmod +x` **e** no handler usar `execve("/usr/bin/env", {"python3", script, NULL}, env)` como caminho seguro.

### 3.2 Nova classe `CgiHandler`
- **Arquivos novos:** `includes/cgi.hpp`, `src/cgi.cpp` (o Makefile usa `find`, compila sozinho)
- API proposta:
  ```cpp
  class Cgi {
  public:
      Cgi();
      bool   start(const Request &req, const Server &srv, const Location &loc,
                   const std::string &script_path, std::string &err);
      int    in_fd() const;    // write end (stdin do CGI)  -> POLLOUT
      int    out_fd() const;   // read end (stdout do CGI)  -> POLLIN
      pid_t  pid() const;
      bool   feed_body(const std::string &body, size_t &sent);
      bool   collect(const char *data, size_t size);
      bool   finished() const;                 // EOF + waitpid(WNOHANG)
      Response build_response();               // parseia headers CGI
      void   terminate();                      // kill + waitpid (timeout)
  };
  ```
- Detalhes do fork: 2 `pipe()`, lados do servidor em `O_NONBLOCK`; no filho `dup2` de stdin/stdout, **fechar todos os outros fds** (`FD_CLOEXEC` nos fds anteriores ou fecho por loop), `chdir` pro diretório do script, `execve`, `_exit(127)` no erro. No pai fechar os lados errados e registrar os fds no `_poll_fds` como `CGI_IN`/`CGI_OUT` com `owner = client_fd`.

### 3.3 Variáveis de ambiente (o subject faz questão disso)
```
GATEWAY_INTERFACE=CGI/1.1        SERVER_PROTOCOL=<versão da request>
REQUEST_METHOD=<method>          QUERY_STRING=<req.get_query()>   (já existe no Request)
SCRIPT_NAME=<path da location + nome do script>
PATH_INFO=<resto depois do script>   PATH_TRANSLATED=<root + path_info>
SCRIPT_FILENAME=<caminho físico do script>
CONTENT_TYPE=<header, se houver>     CONTENT_LENGTH=<tamanho real do body, já un-chunkado>
SERVER_NAME / SERVER_PORT / SERVER_SOFTWARE / SERVER_ADDR
REMOTE_ADDR / REMOTE_HOST (guardar o peer no accept)
```
O `RequestParser` já deschunka e seta `content-length` → o CGI recebe o body como o subject exige.

### 3.4 Integração no `Router` e no `WebServ`
- `Router::handle_request` ganha um parâmetro de saída pro `Cgi` (ou devolve uma decisão explícita), porque o fork não pode acontecer dentro do Router sem passar os fds pro poll:
  - checar `best_match->get_cgi_ext()` contra a extensão do arquivo **antes** do dispatch por método (hoje `POST` sem `upload_store` retorna 403 e engoliria o CGI);
  - se for CGI → `Cgi::start(...)`, estado `CGI_RUNNING`, escrever o body no `in_fd` (POLLOUT) e ler do `out_fd` (POLLIN).
- `Cgi::build_response()`: achar `\r\n\r\n` (ou `\n\n`), parsear `Chave: valor`; se vier `Status: 404 Not Found` usar esse código/msg (precisa do novo `Response::set_status_msg()`), senão 200. Body = resto. Sem `Content-Length` do CGI → o `build()` calcula (EOF marca o fim).
- Fechar o `in_fd` no fim do body (o CGI precisa do EOF).
- Teto do buffer de stdout (ex. 10 MB) → estourou, mata o CGI e devolve 500 (evita OOM).

### 3.5 Timeout/kill do CGI
- Deadline de 30s em `CGI_RUNNING`; estourou → `kill(SIGTERM)`, `waitpid`, e se em 2s não sair `SIGKILL` → resposta **504**. O `www/cgi-bin/loop.py` já existe pra testar isso.
- `crash.py` (exit 1 sem output) → **502**.

### 3.6 Ajustes no `Response`
- `set_status_msg(const std::string&)` (reason arbitrário do CGI).
- `set_no_body(bool)` (HEAD, 204, 304).
- `status_has_body()` exclui 1xx, 204, 3xx e 304.

### 3.7 Validação do CGI
- `curl -i /cgi-bin/hello.py?nome=teste` → 200 com `QUERY_STRING` preenchido.
- `curl -i -X POST --data-binary @arquivo /cgi-bin/upload.py` → arquivo em `www/upload/`.
- `loop.py` → 504 em 30s **e o servidor continua ativo** (teste de 300 conexões logo depois).
- `crash.py` → 502.
- POST chunked pra CGI → body chega un-chunkado.

---

## Fase 4 — Fechamento e validação final

1. **Configs de demonstração** (subject: *"You must provide configuration files and default files to test and demonstrate that every feature works"*): `default.conf` cobrindo redirect, autoindex on/off, upload, cgi, error_page; mais um `multiport.conf` com duas portas e roots diferentes.
2. **Aviso no startup** quando dois blocos `server` dividem a mesma porta (o segundo é inalcançável — vhost fora de escopo, mas deixar explícito).
3. **Testes:** `tests/smoke.py` cobrindo 200/201/204/301/400/403/404/405/411/413/414/431/501/502/504, traversal, chunked, upload+delete, CGI GET/POST; e `tests/stress.py` (500 conexões concorrentes + 100 ociosas).
4. **Checklist vs evaluation sheet:**
   - [ ] Compila `-Wall -Wextra -Werror -std=c++98`
   - [ ] Nunca lê/escreve socket sem passar pelo poll (só disco é direto)
   - [ ] Nenhum `errno` após read/write
   - [ ] `fork` só pra CGI
   - [ ] Nunca trava: timeout de request e de CGI
   - [ ] GET/POST/DELETE, upload, site estático completo
   - [ ] Status corretos + error pages configuradas **e** default
   - [ ] Stress: servidor continua de pé
   - [ ] README completo
   - [ ] `make all / clean / fclean / re / $(NAME)`

---

## Ordem de commits

```
Fase 0   →  docs: README + regra $(NAME) + config default + limpeza de html/
Fase 2   →  fix: 411, 400 em POST /, Allow+Date+Server, HEAD, match de location, autoindex, 301
Fase 1   →  feat: timeout de request + refactor do event loop (FdCtx/estados)  ← commit separado
            feat: error_page resolvido + redirect
Fase 2.9 →  feat: envio de arquivo em blocos (SENDING_FILE)
Fase 3   →  feat: CgiHandler (fork/exec/env/pipes) + integração + timeout CGI
Fase 4   →  chore: configs de demo + testes python + stress
```

O commit da Fase 1.4 (refactor do loop) fica isolado de propósito: dá pra reverter só ele se algo quebrar, e a partir daí o CGI vira pura adição de estado.
