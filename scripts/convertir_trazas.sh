#!/usr/bin/env bash
# Convierte las trazas MAWI .pcap.gz a registros binarios de 24 bytes con pcap2bin
# (herramienta de la Tarea 1, copiada en tools/pcap2bin.cpp). IPv6 se descarta (default).
# Uso: scripts/convertir_trazas.sh [dir_trazas]   (default: trazas)
# Omite las trazas cuyo .bin ya existe y es no vacío. Escribe a .bin.tmp y renombra
# al final, para no dejar un .bin incompleto si se interrumpe.
set -euo pipefail
cd "$(dirname "$0")/.."

DIR="${1:-trazas}"
BIN=bin/pcap2bin
[ -x "$BIN" ] || make "$BIN"

for gz in "$DIR"/201801251400.pcap.gz "$DIR"/201801261400.pcap.gz "$DIR"/201801301400.pcap.gz; do
  [ -f "$gz" ] || continue
  out="${gz%.pcap.gz}.bin"
  if [ -s "$out" ]; then echo "[ok] $out ya existe"; continue; fi
  echo "[convirtiendo] $gz -> $out"
  # Las estadísticas de pcap2bin (stderr) se guardan junto a la traza para el informe.
  gzip -dc "$gz" | "$BIN" > "$out.tmp" 2> "${out%.bin}.pcap2bin.txt"
  mv "$out.tmp" "$out"
  cat "${out%.bin}.pcap2bin.txt"
done
