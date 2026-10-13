#!/usr/bin/env python3
"""Sai 12MB. Garante que a saida do CGI continua fluindo quando passa do
antigo teto de 10MB em memoria — hoje ela vai para um arquivo temporario
e e mandada em blocos."""
import sys

CHUNK = b"x" * 65536
TOTAL = 12 * 1024 * 1024

sys.stdout.write("Content-Type: application/octet-stream\r\n\r\n")
sys.stdout.flush()
sent = 0
while sent < TOTAL:
    sys.stdout.buffer.write(CHUNK)
    sys.stdout.buffer.flush()
    sent += len(CHUNK)
