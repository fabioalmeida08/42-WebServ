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
        methods         GET HEAD;
        autoindex       off;
    }

    location /files {
        root            www/files;
        methods         GET HEAD;
        autoindex       on;
    }

    location /upload {
        root            www/upload;
        methods         GET HEAD POST DELETE;
        upload_store    www/upload;
        autoindex       on;
    }

    location /cgi-bin {
        root            www/cgi-bin;
        methods         GET HEAD POST;
        cgi             .py;
    }

    location /kapouet {
        root            /tmp/www;
        methods         GET HEAD;
    }
}
```

Directives: `listen`, `host`, `server_name`, `client_max_body_size`, `error_page`
(server level) and `root`, `methods`, `index`, `autoindex`, `upload_store`, `cgi`,
`redirect`, `client_max_body_size` (location level). Several `server` blocks can
use different ports to serve different websites — `multiport.conf` demonstrates
exactly that with `8081` (`www/`) and `8082` (`www2/`):

```sh
./bin/webserv multiport.conf
curl http://127.0.0.1:8081/    # the default site
curl http://127.0.0.1:8082/    # a second, independent document root
```

Three details of the configuration are easy to get wrong:

**`root` is the directory the location points at — the prefix is not repeated.**
The subject spells it out: *"if URL `/kapouet` is rooted to `/tmp/www`, URL
`/kapouet/pouic/toto/pouet` will search for `/tmp/www/pouic/toto/pouet`"*. So
`location /kapouet { root /tmp/www; }` maps `/kapouet/pouic` to `/tmp/www/pouic`,
never to `/tmp/www/kapouet/pouic`. Every `root` above is written from its own
location's point of view, which is why `/files` reads `www/files` and not `www`.

**`methods` is taken literally: `HEAD` is not implied by `GET`.** A location
declared `methods GET` answers `405` to `HEAD`. That is what the official 42
tester expects from a route that "must answer to GET request ONLY", so write
`methods GET HEAD` when you want `curl -I` to work — `default.conf` does.

**`client_max_body_size` can be declared per location**, which is what makes a
tiny limit on one route and a huge one on another possible at the same time
(the official tester needs `100` on `/post_body` and `100M` on the server).

`cgi` takes the extension and, optionally, the executable to call:

```nginx
cgi .py;                  # run the file itself (needs a shebang and +x)
cgi .bla cgi_test;        # run cgi_test with the file as argv[1]
```

Two blocks sharing the same port is a configuration error: virtual hosting is
out of scope (the subject states it explicitly), so there is nothing to serve
there. The server **refuses to start** (`exit 1`) instead of coming up with a
dead block. The same applies to a port that is already taken — a second
`./bin/webserv` on a busy port fails loudly instead of pretending it listens.

`Ctrl-C` and `SIGTERM` shut the server down cleanly: every client, CGI pipe
and listening socket is closed before `main` returns.

### Official 42 tester

The official package ships its own binary, its CGI executable and the files it
serves, so none of that lives in this repository (`tester.conf`, `YoupiBanane/`
and `cgi_test` are local-only and gitignored). The setup the evaluator asks for
is `default.conf` with four changes:

```nginx
location /          { ... methods GET; }                    # GET only, no HEAD
location /post_body { ... methods POST; client_max_body_size 100; }
location /directory {
    root    YoupiBanane;      # stripped prefix, as in the subject
    index   youpi.bad_extension;
    methods GET POST;
    cgi     .bla cgi_test;    # cgi_test = the 42 cgi_tester binary
}
```

Then, from the repository root:

```sh
cp /path/to/cgi_tester cgi_test   # from the 42 package; not committed (4 MB)
./bin/webserv tester.conf
/path/to/tester http://127.0.0.1:8080/
```

The binary walks the whole list and stops at the first problem with
`FATAL ERROR ON LAST TEST: ...`; a clean run prints every test name and exits
with status `0`. Its last check is 20 workers posting 100 MB each at the same
time, which is the only moment the server's resident memory goes into the
gigabytes — the request bodies of those 20 connections are held at once while
they are being fed to the CGI.

### Layout

```
bin/webserv      the executable
default.conf     main configuration file
multiport.conf   two ports with two different document roots
www/             default document root
  errors/        custom error pages
  files/         autoindex demo
  upload/        upload target
  cgi-bin/       python CGI scripts
  private/       directory with autoindex off (no index.html)
www2/            second document root, used by multiport.conf
tests/           smoke and stress tests (python3)
```

### Tests

The tests start and stop the server themselves with `--spawn`; without it they
expect one to already be listening on `127.0.0.1:8080`.

```sh
python3 tests/smoke.py --spawn --slow --multiport
python3 tests/stress.py --spawn
python3 tests/stress.py --spawn --concurrency 500 --cgi
```

`smoke.py` walks the feature list one assertion at a time and prints a per-case
pass/fail table. It covers the status codes (`200/201/204/301/400/403/404/405/
411/413/414/431/501/502/505`), `HEAD`, `Allow`, the `Date`/`Server` headers,
path traversal (including `--path-as-is` and raw sockets), chunked bodies,
upload + delete on disk, autoindex on and off, 64 KB-block file streaming,
CGI (GET/POST/query/HEAD, crash, non-executable script, request headers
reaching the script as `HTTP_*`, a 12 MB output streamed from a temporary
file, script reading a file with a relative path — i.e. `chdir` into the
script's directory) and `multiport.conf` on 8081/8082. Flags:

| flag | adds |
| --- | --- |
| `--spawn` | starts `./bin/webserv` and stops it at the end |
| `--slow` | the 30 s CGI timeout test (a 504 that really takes 30 s) |
| `--multiport` | the second server block of `multiport.conf` |

`stress.py` opens idle connections that never send anything, then fires
`--concurrency` requests in parallel and checks that every one answered `200`,
that the server still responds afterwards, that file descriptors returned to the
baseline (no leak) and that the process memory did not grow. It also prints the
**availability** (share of `2xx` answers) and fails below `99.5%`, which is the
number the evaluation sheet asks for with `siege -b` — `siege` is not installable
here, so `stress.py` measures the same thing directly.

| flag | default | meaning |
| --- | --- | --- |
| `--concurrency` | 500 | simultaneous requests |
| `--idle` | 100 | connections held open without sending anything |
| `--cgi` | off | half of the requests go through CGI (`fork` per request) |
| `--timeout` | 60 | client timeout; set it to `62` to observe the server's `408` on the idle connections (the test then waits the full 60 s, so it takes ~65 s) |

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
