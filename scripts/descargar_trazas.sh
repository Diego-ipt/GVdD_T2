#!/usr/bin/env bash
# Descarga las 3 trazas MAWI (samplepoint-F, 2018, 14:00 h) usadas en la Tarea 2.
# Uso:  scripts/descargar_trazas.sh [directorio_destino]     (default: ./trazas)
# - Reanuda descargas interrumpidas (curl -C -) y reintenta ante fallos de red.
# - Omite archivos ya completos (compara tamano con Content-Length del servidor).
# - Verifica integridad del gzip al final (gzip -t).
# Las trazas NO deben subirse al repositorio (el directorio esta en .gitignore).
set -euo pipefail

BASE_URL="https://mawi.wide.ad.jp/mawi/samplepoint-F/2018"
DEST="${1:-trazas}"
FILES=(
  201801251400.pcap.gz   # jue 25 ene 2018 (~2.36 GB)
  201801261400.pcap.gz   # vie 26 ene 2018 (~2.30 GB)
  201801301400.pcap.gz   # mar 30 ene 2018 (~2.07 GB, contraste)
)

mkdir -p "$DEST"

for f in "${FILES[@]}"; do
  url="$BASE_URL/$f"
  out="$DEST/$f"

  remote=$(curl -sIL --fail "$url" | tr -d '\r' | awk 'tolower($1)=="content-length:"{v=$2} END{print v}')
  [ -n "$remote" ] || { echo "ERROR: no se pudo consultar $url" >&2; exit 1; }

  if [ -f "$out" ] && [ "$(stat -c %s "$out")" = "$remote" ]; then
    echo "[ok] $f ya descargado ($remote bytes)"
  else
    echo "[descargando] $f ($remote bytes)"
    curl -L --fail -C - --retry 10 --retry-delay 5 --retry-all-errors \
         --progress-bar -o "$out" "$url"
  fi

  gzip -t "$out" && echo "[ok] $f integridad gzip verificada"
done

echo "Listo. Trazas en: $DEST"
echo "Siguiente paso: convertir con pcap2bin (Tarea 1) -> .bin (24 bytes/paquete, solo IPv4)."
