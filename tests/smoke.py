#!/usr/bin/env python3
"""Smoke test do webserv.

Cobre todos os codigos que a folha de avaliacao espera ver, mais os
casos de borda que costumam quebrar: traversal, request chunkado,
upload+delete, CGI (GET/POST/502/504), HEAD sem corpo e envio de
arquivo em blocos.

Uso:
    ./bin/webserv default.conf &          # sobe o servidor
    python3 tests/smoke.py                # roda tudo (~2s)
    python3 tests/smoke.py --slow         # inclui o 504 de CGI (~30s)
    python3 tests/smoke.py --spawn        # o proprio teste sobe/derruba
    python3 tests/smoke.py --multiport    # checa tambem 8081/8082

Sai com 0 se tudo passou, 1 caso contrario.
"""

import argparse
import hashlib
import http.client
import os
import socket
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RESULTS = []


# ---------------------------------------------------------------- utilidades
class Raw:
    """Cliente socket crudo: preciso pra mandar request malformado,
    versao errada ou header gigante, coisa que http.client nao deixa."""

    def __init__(self, host, port, timeout=10):
        self.host = host
        self.port = port
        self.timeout = timeout

    def send(self, payload):
        s = socket.create_connection((self.host, self.port), self.timeout)
        s.settimeout(self.timeout)
        try:
            if isinstance(payload, str):
                payload = payload.encode()
            s.sendall(payload)
            chunks = []
            while True:
                try:
                    data = s.recv(65536)
                except socket.timeout:
                    break
                if not data:
                    break
                chunks.append(data)
            return b"".join(chunks)
        finally:
            s.close()

    @staticmethod
    def parse(raw):
        head, _, body = raw.partition(b"\r\n\r\n")
        lines = head.split(b"\r\n")
        status = 0
        if lines and lines[0].split()[1:2]:
            try:
                status = int(lines[0].split()[1])
            except (ValueError, IndexError):
                status = 0
        headers = {}
        for line in lines[1:]:
            if b":" in line:
                k, _, v = line.partition(b":")
                headers[k.strip().lower().decode()] = v.strip().decode()
        return status, headers, body


def req(method, path, body=None, headers=None, host="127.0.0.1", port=8080,
         timeout=10):
    conn = http.client.HTTPConnection(host, port, timeout=timeout)
    try:
        conn.request(method, path, body=body, headers=headers or {})
        resp = conn.getresponse()
        data = resp.read()
        # chaves em minusculo: compare de forma uniforme
        lower = dict((k.lower(), v) for k, v in resp.getheaders())
        return resp.status, lower, data
    finally:
        conn.close()


def check(name, got, want, extra=""):
    ok = got == want
    RESULTS.append((name, ok))
    mark = "OK  " if ok else "FAIL"
    detail = "%s %-42s esperado=%s obtido=%s" % (mark, name, want, got)
    if extra and not ok:
        detail += "  (%s)" % extra
    print(detail)
    return ok


def check_true(name, cond, detail=""):
    ok = bool(cond)
    RESULTS.append((name, ok))
    print("%s %-42s %s" % ("OK  " if ok else "FAIL", name, detail))
    return ok


# --------------------------------------------------------------- fixture
def make_403_fixture():
    """Um FIFO nao e arquivo regular -> o router devolve 403. Criamos na
    hora porque o git nao guarda FIFO."""
    path = os.path.join(REPO, "www", "private", "tubo-teste")
    try:
        if not os.path.exists(path):
            os.mkfifo(path)
        return path
    except OSError:
        return None


def drop_403_fixture(path):
    if path:
        try:
            os.remove(path)
        except OSError:
            pass


def make_big_file(size=5 * 1024 * 1024):
    path = os.path.join(REPO, "www", "files", "grande-teste.bin")
    with open(path, "wb") as fh:
        fh.write(os.urandom(size))
    return path, hashlib.md5(open(path, "rb").read()).hexdigest()


def drop_file(path):
    try:
        os.remove(path)
    except OSError:
        pass


# ---------------------------------------------------------------- testes
def run_basic(host, port):
    print("--- metodos e codigos basicos ---")
    status, headers, body = req("GET", "/", host=host, port=port)
    check("GET /", status, 200)
    check_true("GET / devolve corpo", len(body) > 100, "%d bytes" % len(body))
    check_true("header Date presente", "date" in headers)
    check_true("header Server presente", "server" in headers)

    status, headers, body = req("HEAD", "/index.html", host=host, port=port)
    check("HEAD /index.html", status, 200)
    check_true("HEAD nao manda corpo", len(body) == 0, "%d bytes" % len(body))
    check_true("HEAD mantem Content-Length",
               headers.get("content-length") == "25103",
               headers.get("content-length", "ausente"))

    status, _, _ = req("GET", "/files", host=host, port=port)
    check("GET /files (dir sem barra)", status, 301)
    status, _, _ = req("GET", "/antigo", host=host, port=port)
    check("GET /antigo (redirect)", status, 301)
    status, headers, _ = req("GET", "/antigo", host=host, port=port)
    check_true("redirect manda Location",
               headers.get("location") == "/files/exemplo.txt",
               headers.get("location", "ausente"))
    status, _, _ = req("GET", "/files/", host=host, port=port)
    check("GET /files/ (autoindex)", status, 200)

    status, _, _ = req("GET", "/nao-existe", host=host, port=port)
    check("GET /nao-existe", status, 404)
    status, _, body = req("GET", "/nao-existe", host=host, port=port)
    check_true("404 usa a error_page configurada", len(body) == 1689,
               "%d bytes (padrao gerado seria 157)" % len(body))

    status, _, _ = req("PUT", "/index.html", host=host, port=port)
    check("PUT (metodo desconhecido)", status, 501)
    status, headers, _ = req("DELETE", "/index.html", host=host, port=port)
    check("DELETE /index.html (nao permitido)", status, 405)
    check_true("405 manda Allow", "allow" in headers,
               headers.get("allow", "ausente"))


def run_errors(host, port, fifo):
    print("--- erros ---")
    raw = Raw(host, port)

    st, _, _ = Raw.parse(raw.send(
        "POST /upload/x HTTP/1.1\r\nHost: x\r\n"
        "Content-Length: abc\r\n\r\n"))
    check("Content-Length invalido", st, 400)

    st, _, _ = Raw.parse(raw.send(
        "POST /upload/ HTTP/1.1\r\nHost: x\r\n"
        "Content-Length: 1\r\n\r\nx"))
    check("POST com barra final no path", st, 400)

    st, _, _ = Raw.parse(raw.send(
        "GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"
        "GET /ainda-nao HTTP/1.1\r\n"))
    check("pipeline: 1o request completo", st, 200)

    st, _, _ = Raw.parse(raw.send(
        "GET /%s HTTP/1.1\r\nHost: x\r\n\r\n" % ("a" * 12000)))
    check("URI enorme", st, 414)

    # Uma linha de header acima de 8192 dispara o 431. Mandar muito mais
    # que isso de uma vez faz o servidor fechar com dados nao lidos e o
    # kernel responder RST, entao fica logo acima do limite.
    st, _, _ = Raw.parse(raw.send(
        "GET / HTTP/1.1\r\nHost: x\r\nX-Big: %s\r\n\r\n" % ("v" * 9000)))
    check("header gigante", st, 431)

    st, _, _ = Raw.parse(raw.send("GET / HTTP/2.0\r\nHost: x\r\n\r\n"))
    check("versao HTTP desconhecida", st, 505)

    st, _, _ = Raw.parse(raw.send("GIMME / HTTP/1.1\r\nHost: x\r\n\r\n"))
    check("metodo invalido", st, 501)

    st, _, _ = Raw.parse(raw.send(
        "POST /upload/x HTTP/1.1\r\nHost: x\r\n\r\n"))
    check("POST sem Content-Length", st, 411)

    # So os headers ja bastam pra rejeitar: mandar 1MB junto faz o
    # servidor fechar com dados na fila e o cliente ver RST em vez de 413
    st, _, _ = Raw.parse(raw.send(
        "POST /upload/grande.bin HTTP/1.1\r\nHost: x\r\n"
        "Content-Length: 1048586\r\n\r\n"))
    check("body maior que client_max_body_size", st, 413)

    if fifo:
        st, _, _ = Raw.parse(raw.send(
            "GET /private/tubo-teste HTTP/1.1\r\nHost: x\r\n\r\n"))
        check("arquivo que nao e regular", st, 403)
    else:
        check_true("403 (FIFO indisponivel neste SO)", True, "pulado")


def run_traversal(host, port):
    print("--- path traversal ---")
    raw = Raw(host, port)
    for path in ("/../../etc/passwd",
                 "/files/../../etc/passwd",
                 "/cgi-bin/../../etc/passwd",
                 "/./../../etc/passwd",
                 "/files/..%2f..%2fetc%2fpasswd",
                 "/files/%2e%2e/%2e%2e/etc/passwd"):
        st, _, _ = Raw.parse(raw.send(
            "GET %s HTTP/1.1\r\nHost: x\r\n\r\n" % path))
        check("traversal %s" % path, st, 400)


def run_upload(host, port):
    print("--- upload + delete ---")
    name = "smoke-upload.txt"
    path = os.path.join(REPO, "www", "upload", name)
    drop_file(path)

    st, _, _ = req("POST", "/upload/" + name, body=b"conteudo de teste",
                    headers={"Content-Type": "text/plain"},
                    host=host, port=port)
    check("POST cria arquivo (201)", st, 201)
    check_true("arquivo esta no disco",
               os.path.isfile(path) and open(path).read() == "conteudo de teste")

    st, _, body = req("GET", "/upload/" + name, host=host, port=port)
    check("GET do arquivo enviado", st, 200)
    check_true("conteudo bate", body == b"conteudo de teste", body[:40])

    st, _, _ = req("DELETE", "/upload/" + name, host=host, port=port)
    check("DELETE remove (204)", st, 204)
    check_true("arquivo saiu do disco", not os.path.exists(path))


def run_chunked(host, port):
    print("--- request chunkado ---")
    raw = Raw(host, port)
    payload = b"corpo_enviado_por_chunks"
    req = ("POST /upload/chunked.txt HTTP/1.1\r\nHost: x\r\n"
           "Transfer-Encoding: chunked\r\nContent-Type: text/plain\r\n"
           "Connection: close\r\n\r\n")
    req = req.encode() + b"%x\r\n%s\r\n0\r\n\r\n" % (len(payload), payload)
    st, _, _ = Raw.parse(raw.send(req))
    check("POST chunkado", st, 201)
    path = os.path.join(REPO, "www", "upload", "chunked.txt")
    ok = os.path.isfile(path) and open(path, "rb").read() == payload
    check_true("chunked desmontado corretamente", ok,
               open(path, "rb").read()[:40] if os.path.isfile(path) else "sumiu")
    drop_file(path)


def run_autoindex(host, port):
    print("--- autoindex ---")
    st, _, body = req("GET", "/files/", host=host, port=port)
    check("autoindex ligado", st, 200)
    text = body.decode(errors="replace")
    check_true("autoindex lista arquivo", "exemplo.txt" in text)
    check_true("autoindex nao lista . e ..",
               "href=\"./\"" not in text and "href=\"../\"" not in text)
    check_true("autoindex nao lista .gitkeep", ".gitkeep" not in text)

    st, _, body = req("GET", "/private/", host=host, port=port)
    text = body.decode(errors="replace")
    # autoindex off e sem index.html: nao pode devolver um listing com o
    # nome dos arquivos que existem na pasta (o FIFO do teste de 403)
    check_true("autoindex desligado nao vaza listing",
               st in (403, 404) and "tubo-teste" not in text,
               "status=%d, contem nome de arquivo=%s"
               % (st, "tubo-teste" in text))


def run_big_file(host, port):
    print("--- envio em blocos (5MB) ---")
    path, expected = make_big_file()
    try:
        t0 = time.time()
        st, _, body = req("GET", "/files/grande-teste.bin",
                           host=host, port=port, timeout=60)
        elapsed = time.time() - t0
        check("GET de 5MB", st, 200)
        got = hashlib.md5(body).hexdigest()
        check_true("5MB chega integro", got == expected, got)
        check_true("levou menos de 10s", elapsed < 10, "%.2fs" % elapsed)
    finally:
        drop_file(path)


def run_cgi(host, port, slow):
    print("--- CGI ---")
    st, _, body = req("GET", "/cgi-bin/hello.py", host=host, port=port)
    check("CGI GET", st, 200)
    check_true("CGI devolve saida do script", b"CGI funcionando" in body,
               body[:60])

    st, _, body = req("GET", "/cgi-bin/hello.py?nome=fabio",
                       host=host, port=port)
    check_true("QUERY_STRING repassada", b"nome=fabio" in body)

    marker = "corpo_postado_no_cgi"
    st, _, body = req("POST", "/cgi-bin/hello.py", body=marker.encode(),
                       headers={"Content-Type": "text/plain"},
                       host=host, port=port)
    check("CGI POST", st, 200)
    check_true("body chegou no stdin do CGI", marker.encode() in body)

    st, headers, body = req("HEAD", "/cgi-bin/hello.py", host=host, port=port)
    check("CGI HEAD", st, 200)
    check_true("CGI HEAD sem corpo", len(body) == 0, "%d bytes" % len(body))

    st, _, _ = req("GET", "/cgi-bin/crash.py", host=host, port=port)
    check("CGI quebra (502)", st, 502)
    st, _, _ = req("GET", "/cgi-bin/nao-existe.py", host=host, port=port)
    check("CGI inexistente (502)", st, 502)
    st, _, _ = req("GET", "/cgi-bin/sem-permissao.py", host=host, port=port)
    check("CGI sem +x (502)", st, 502)

    if slow:
        t0 = time.time()
        st, _, _ = req("GET", "/cgi-bin/loop.py", host=host, port=port,
                        timeout=60)
        elapsed = time.time() - t0
        check("CGI infinito (504)", st, 504)
        check_true("504 chega perto de 30s", 25 <= elapsed <= 40,
                   "%.1fs" % elapsed)
    else:
        print("      (504 pulado; use --slow)")


def run_multiport():
    print("--- multiport.conf (8081 / 8082) ---")
    st, _, body = req("GET", "/", port=8081)
    check("porta 8081 responde", st, 200)
    check_true("8081 serve root www", b"webserv" in body.lower(), body[:60])
    st, _, body = req("GET", "/", port=8082)
    check("porta 8082 responde", st, 200)
    check_true("8082 serve root www2", b"Segundo server block" in body)
    st, _, body = req("GET", "/docs/", port=8082)
    check("autoindex do root www2", st, 200)
    check_true("www2 listing visivel", b"leia-me.txt" in body)
    st, _, _ = req("POST", "/upload/x", body=b"oi",
                    headers={"Content-Type": "text/plain"}, port=8082)
    check("upload na 8082", st, 201)
    req("DELETE", "/upload/x", port=8082)


# ---------------------------------------------------------------- spawn
def spawn(conf, port):
    proc = subprocess.Popen([os.path.join(REPO, "bin", "webserv"), conf],
                            cwd=REPO, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    for _ in range(50):
        try:
            socket.create_connection(("127.0.0.1", port), 0.2).close()
            return proc
        except OSError:
            time.sleep(0.1)
    proc.terminate()
    sys.exit("servidor nao subiu em %s" % conf)


def wait_gone(proc):
    if proc is None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()


# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--slow", action="store_true",
                    help="inclui o 504 de CGI (30s)")
    ap.add_argument("--spawn", action="store_true",
                    help="sobe e derruba o servidor neste teste")
    ap.add_argument("--multiport", action="store_true",
                    help="checa tambem o multiport.conf (8081/8082)")
    args = ap.parse_args()

    procs = []
    try:
        if args.spawn:
            procs.append(spawn("default.conf", args.port))
        if args.multiport:
            procs.append(spawn("multiport.conf", 8081))

        try:
            socket.create_connection((args.host, args.port), 3).close()
        except OSError:
            sys.exit("servidor nao responde em %s:%d — suba com "
                     "./bin/webserv default.conf ou use --spawn"
                     % (args.host, args.port))

        fifo = make_403_fixture()
        try:
            run_basic(args.host, args.port)
            run_errors(args.host, args.port, fifo)
            run_traversal(args.host, args.port)
            run_upload(args.host, args.port)
            run_chunked(args.host, args.port)
            run_autoindex(args.host, args.port)
            run_big_file(args.host, args.port)
            run_cgi(args.host, args.port, args.slow)
        finally:
            drop_403_fixture(fifo)

        if args.multiport:
            run_multiport()
    finally:
        for proc in procs:
            wait_gone(proc)

    failed = [n for n, ok in RESULTS if not ok]
    print()
    print("%d testes, %d falhas" % (len(RESULTS), len(failed)))
    if failed:
        for name in failed:
            print("  falhou: %s" % name)
        return 1
    print("TUDO OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
