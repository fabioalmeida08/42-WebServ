#!/usr/bin/env python3
import os, sys
# abre um arquivo RELATIVO ao diretorio do script: so funciona se o
# webserv fizer chdir antes do execve
try:
    with open("dados-relativos.txt") as fh:
        conteudo = fh.read().strip()
    corpo = "RELATIVO-OK:" + conteudo
except Exception as exc:
    corpo = "RELATIVO-FALHOU:" + str(exc)
sys.stdout.write(
    "Content-Type: text/plain\r\nContent-Length: %d\r\n\r\n%s"
    % (len(corpo), corpo))
