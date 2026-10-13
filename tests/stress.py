#!/usr/bin/env python3
"""Stress test do webserv.

Sobe 100 conexoes ociosas (que nunca mandam nada) e em paralelo joga
500 requests concorrentes contra o servidor. Depois confere quatro
coisas que a folha de avaliacao pergunta:

  1. todos os requests responderam com 200
  2. o servidor continua de pé e respondendo
  3. os file descriptors voltaram ao nivel inicial (nao vazou fd)
  4. a memoria do processo nao disparou

Uso:
    ./bin/webserv default.conf &
    python3 tests/stress.py
    python3 tests/stress.py --concurrency 1000 --idle 200
    python3 tests/stress.py --cgi           # inclui requests de CGI
    python3 tests/stress.py --spawn         # o proprio teste sobe/derruba

Sai com 0 se o servidor aguentou, 1 caso contrario.
"""

import argparse
import http.client
import os
import socket
import subprocess
import sys
import threading
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FAILURES = []


def fail(msg):
    FAILURES.append(msg)
    print("FAIL %s" % msg)


def ok(msg):
    print("OK   %s" % msg)


def server_pid():
    try:
        out = subprocess.run(["pgrep", "-x", "webserv"], capture_output=True,
                             text=True).stdout.split()
        return int(out[0]) if out else None
    except (OSError, ValueError):
        return None


def fd_count(pid):
    try:
        return len(os.listdir("/proc/%d/fd" % pid))
    except OSError:
        return None


def rss_kb(pid):
    try:
        for line in open("/proc/%d/status" % pid):
            if line.startswith("VmRSS:"):
                return int(line.split()[1])
    except OSError:
        pass
    return None


def one_get(host, port, path, timeout, results, index):
    t0 = time.time()
    try:
        conn = http.client.HTTPConnection(host, port, timeout=timeout)
        conn.request("GET", path)
        resp = conn.getresponse()
        resp.read()
        status = resp.status
        conn.close()
    except Exception as exc:                     # noqa: BLE001
        status = repr(exc)
    results[index] = (status, time.time() - t0)


def percentile(values, pct):
    if not values:
        return 0.0
    ordered = sorted(values)
    idx = min(len(ordered) - 1, int(len(ordered) * pct / 100.0))
    return ordered[idx]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--concurrency", type=int, default=500,
                    help="requests simultaneos (padrao 500)")
    ap.add_argument("--idle", type=int, default=100,
                    help="conexoes ociosas mantidas abertas (padrao 100)")
    ap.add_argument("--cgi", action="store_true",
                    help="metade dos requests vao pro CGI (fork por request)")
    ap.add_argument("--timeout", type=float, default=60.0)
    ap.add_argument("--spawn", action="store_true",
                    help="sobe e derruba o servidor neste teste")
    args = ap.parse_args()

    proc = None
    if args.spawn:
        proc = subprocess.Popen([os.path.join(REPO, "bin", "webserv"),
                                 "default.conf"], cwd=REPO,
                                stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
        for _ in range(50):
            try:
                socket.create_connection((args.host, args.port), 0.2).close()
                break
            except OSError:
                time.sleep(0.1)
        else:
            proc.terminate()
            sys.exit("servidor nao subiu")

    try:
        try:
            socket.create_connection((args.host, args.port), 3).close()
        except OSError:
            sys.exit("servidor nao responde em %s:%d — suba com "
                     "./bin/webserv default.conf ou use --spawn"
                     % (args.host, args.port))

        pid = server_pid()
        fd_before = fd_count(pid)
        rss_before = rss_kb(pid)
        print("pid=%s  fds=%s  RSS=%s KB" % (pid, fd_before, rss_before))
        print()

        # --- conexoes ociosas: conecta e nunca manda nada -------------
        idles = []
        idle_opened_at = time.time()
        for _ in range(args.idle):
            try:
                s = socket.create_connection((args.host, args.port), 5)
                s.setblocking(False)
                idles.append(s)
            except OSError as exc:
                fail("abriu so %d/%d conexoes ociosas (%s)"
                     % (len(idles), args.idle, exc))
                break
        ok("%d conexoes ociosas abertas" % len(idles))

        # --- os 500 requests ------------------------------------------
        paths = ["/"] * args.concurrency
        if args.cgi:
            for i in range(args.concurrency // 2):
                paths[i * 2] = "/cgi-bin/hello.py"
        results = [None] * args.concurrency
        threads = []
        t0 = time.time()
        for i in range(args.concurrency):
            t = threading.Thread(target=one_get,
                                 args=(args.host, args.port, paths[i],
                                       args.timeout, results, i))
            threads.append(t)
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        elapsed = time.time() - t0

        good = [r for r in results if r[0] == 200]
        bad = [(i, r[0]) for i, r in enumerate(results) if r[0] != 200]
        lat = [r[1] for r in results if r[0] == 200]

        print()
        print("%d requests em %.2fs -> %.0f req/s"
              % (args.concurrency, elapsed,
                 args.concurrency / elapsed if elapsed else 0))
        if lat:
            print("latencia p50=%.0fms  p95=%.0fms  max=%.0fms"
                  % (percentile(lat, 50) * 1000,
                     percentile(lat, 95) * 1000,
                     percentile(lat, 100) * 1000))
        if bad:
            kinds = {}
            for _, status in bad:
                kinds[status] = kinds.get(status, 0) + 1
            fail("%d requests falharam: %s" % (len(bad), kinds))
            for i, status in bad[:5]:
                fail("  req %d [%s] -> %s" % (i, paths[i], status))
        else:
            ok("todos os %d requests responderam 200" % args.concurrency)

        # --- servidor continua de pé ---------------------------------
        try:
            conn = http.client.HTTPConnection(args.host, args.port, timeout=10)
            conn.request("GET", "/")
            status = conn.getresponse().status
            conn.close()
            if status == 200:
                ok("servidor continua respondendo apos o stress")
            else:
                fail("apos o stress GET / -> %d" % status)
        except Exception as exc:                 # noqa: BLE001
            fail("apos o stress o servidor nao respondeu: %r" % exc)

        # --- as ociosas devem ter sido cortadas pelo 408 --------------
        # (o timeout do servidor e de 60s; o cliente precisa esperar mais
        # que isso para conseguir ler a resposta antes de estourar)
        if args.timeout > 60:
            # O servidor so corta depois de 60s, entao o teste precisa
            # ESPERAR de verdade antes de ler: sem isso ele so conta as
            # respostas que ja chegaram e mente sobre o resultado.
            espera = 62.0 - (time.time() - idle_opened_at)
            if espera > 0:
                print("      (esperando os 60s do REQUEST_TIMEOUT: %.0fs)"
                      % espera)
                time.sleep(espera)
            answered = 0
            for s in idles:
                s.setblocking(True)
                s.settimeout(5)
                try:
                    if s.recv(1024):
                        answered += 1
                except OSError:
                    pass
                s.close()
            if idles and answered == len(idles):
                ok("%d/%d conexoes ociosas receberam 408"
                   % (answered, len(idles)))
            elif answered:
                fail("so %d/%d conexoes ociosas receberam 408"
                     % (answered, len(idles)))
            else:
                fail("conexoes ociosas ficaram presas sem resposta")
        else:
            for s in idles:
                s.close()
            print("      (conexoes ociosas fechadas pelo teste; para ver "
                  "o 408 use --timeout 62, leva ~65s)")

        # --- fd e memoria --------------------------------------------
        for _ in range(20):
            time.sleep(0.2)
            if fd_count(pid) == fd_before:
                break
        fd_after = fd_count(pid)
        rss_after = rss_kb(pid)
        print()
        if fd_after is None or fd_before is None:
            print("      (sem acesso a /proc, pulando checagem de fd)")
        elif fd_after <= fd_before + 2:
            ok("fds de volta ao normal: %s -> %s" % (fd_before, fd_after))
        else:
            fail("vazamento de fd: %s -> %s" % (fd_before, fd_after))

        if rss_before and rss_after:
            delta = rss_after - rss_before
            if delta < 20 * 1024:
                ok("RSS %s -> %s KB (delta %+d KB)"
                   % (rss_before, rss_after, delta))
            else:
                fail("RSS disparou: %s -> %s KB" % (rss_before, rss_after))

        print()
        if FAILURES:
            print("%d problemas encontrados" % len(FAILURES))
            return 1
        print("STRESS OK")
        return 0
    finally:
        if proc is not None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()


if __name__ == "__main__":
    sys.exit(main())
