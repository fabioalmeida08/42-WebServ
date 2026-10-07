#!/usr/bin/env python3
import os, sys, html
body = sys.stdin.read(int(os.environ.get("CONTENT_LENGTH") or 0))
keys = ["REQUEST_METHOD","QUERY_STRING","CONTENT_TYPE","CONTENT_LENGTH","SCRIPT_NAME","PATH_INFO","SERVER_PROTOCOL","SERVER_NAME","SERVER_PORT"]
rows = "".join(f"<tr><td>{k}</td><td>{html.escape(os.environ.get(k,''))}</td></tr>" for k in keys)
out = f"""<!DOCTYPE html><html><head><meta charset="UTF-8"><title>CGI ok</title>
<style>body{{background:#0b0d12;color:#e6e9ef;font:14px system-ui;padding:32px}}h1{{color:#7cf0c4}}
table{{border-collapse:collapse;margin:16px 0}}td{{border:1px solid #232a38;padding:6px 10px;font-family:monospace}}
td:first-child{{color:#7aa2ff}}pre{{background:#12161f;padding:12px;border-radius:8px}}</style></head>
<body><h1>CGI funcionando</h1><table>{rows}</table><p>stdin:</p><pre>{html.escape(body) or '(vazio)'}</pre></body></html>"""
data = out.encode()
sys.stdout.write("Content-Type: text/html; charset=utf-8\r\n")
sys.stdout.write(f"Content-Length: {len(data)}\r\n\r\n")
sys.stdout.flush(); sys.stdout.buffer.write(data)
