#!/usr/bin/env python3
# Fixture do tests/smoke.py: este arquivo NAO tem bit de execucao (644),
# entao o servidor precisa responder 502 em vez de tentar rodar.
print("Content-Type: text/plain\r\n")
print("\r\n")
print("nunca devia chegar aqui")
