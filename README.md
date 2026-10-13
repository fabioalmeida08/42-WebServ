*This project has been created as part of the 42 curriculum by fabioalmeida08, lucabarbosa, RenatoWw.*

# 42-WebServ

## Description

Webserv is a HTTP server written from scratch in C++98. It serves static websites,
accepts file uploads and can execute CGI scripts, being fully compatible with
standard web browsers (Chrome, Firefox, curl, wget, ...).

The whole server runs on a single event loop built on top of one `poll()` call. Every
descriptor that can wait for data (listening sockets and client sockets) is registered
in that call and handled in non-blocking mode, so the server keeps serving many clients
at the same time without ever blocking on a single one. Disk files are the only
exception, since they do not need readiness notifications.

The main pieces are:

- **`Parser`** — reads the configuration file (nginx-like syntax) and builds the
  `Server` / `Location` objects.
- **`RequestParser`** — incremental HTTP/1.1 request parser (request line, headers,
  `Content-Length` body and `chunked` transfer encoding). It works on partial data,
  so a request can arrive split over many `recv()` calls.
- **`Router`** — matches the URI against the configured locations, checks the allowed
  methods and answers with `GET`, `POST`, `DELETE`, redirects, autoindex or errors.
- **`Response`** — builds the raw HTTP response (status line, headers, body).
- **`WebServ`** — owns the listening sockets, the `poll()` loop and the per-client
  state machine (reading the request, sending the response, CGI).

## Instructions

Requirements: a POSIX system, `c++` and `make`.

```sh
make                      # builds bin/webserv
./bin/webserv             # uses ./default.conf
./bin/webserv my.conf     # uses your own configuration file
```

The program must be stopped with `Ctrl+C` or a signal; it closes its sockets on exit.

### Configuration file

The syntax follows the `server` section of nginx. See `default.conf` for a complete
example covering every feature:

```nginx
server {
    listen              8080;
    server_name         localhost;
    client_max_body_size 1M;

    error_page          404 /errors/404.html;

    location / {
        root            www;
        index           index.html;
        methods         GET;
        autoindex       off;
    }

    location /upload {
        root            www;
        methods         GET POST DELETE;
        upload_store    www/upload;
        autoindex       on;
    }

    location /cgi-bin {
        root            www;
        methods         GET POST;
        cgi             .py;
    }
}
```

Directives: `listen`, `host`, `server_name`, `client_max_body_size`, `error_page`
(server level) and `root`, `methods`, `index`, `autoindex`, `upload_store`, `cgi`,
`redirect` (location level). Several `server` blocks can use different ports to serve
different websites.

### Layout

```
bin/webserv      the executable
default.conf     main configuration file
www/             default document root
  errors/        custom error pages
  files/         autoindex demo
  upload/        upload target
  cgi-bin/       python CGI scripts
tests/           smoke and stress tests (python3)
```

### Tests

```sh
python3 tests/smoke.py     # feature by feature, expects ./bin/webserv running
python3 tests/stress.py    # concurrency and idle connections
```

## Resources

- RFC 9110 (HTTP Semantics) and RFC 9112 (HTTP/1.1) — https://www.rfc-editor.org/
- MDN HTTP reference — https://developer.mozilla.org/en-US/docs/Web/HTTP
- nginx documentation, used as the reference for configuration and behaviour — https://nginx.org/en/docs/
- CGIC / the CGI 1.1 specification — https://www.mesa-uk.org/docs/speccgi.html
- `poll(2)`, `pipe(2)`, `fork(2)` and `execve(2)` man pages

### How AI was used

AI was used as an assistant, never as a source of code pasted without review:

- **Orientation** — comparing HTTP behaviours with nginx and reading the RFCs when a
  parser edge case (chunked encoding, header limits, percent decoding) was unclear.
- **Design review** — discussing the event loop structure and the client state machine
  before writing them, then implementing and testing them by hand.
- **Repetitive tasks** — generating the boilerplate of the error pages in `www/errors/`
  and the layout of the demo pages.
- **Debugging** — explaining a failing `curl` test and reasoning about the cause, with
  the fix always written and validated locally.

Every change was tested against the running server with `curl`, raw sockets and the
scripts in `tests/` before being committed.
