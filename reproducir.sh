#!/usr/bin/env bash
# =============================================================================
# reproducir.sh -- Corre TODOS los experimentos de la Tarea 2 y genera todas las
# tablas y figuras del informe, sin intervención manual.
#
# Requisitos: Linux o WSL con g++ (C++17), make, python3 (con venv), gzip, curl.
# Trazas:     trazas/2018012{5,6}1400.pcap.gz y trazas/201801301400.pcap.gz
#             (si no están, se descargan con scripts/descargar_trazas.sh).
# Resultados: resultados/{verificacion,act21,act22,sketches,tablas,figuras}
# Tiempo:     ~2-3 h en un equipo de 16 hilos (ver README.md).
#
# Variables opcionales:
#   SEMILLAS="1 2 3 4 5"   semillas sigma de la Actividad 2.1
#   B=4096                 tamaño de la caché en los experimentos principales
#   K=512                  filas del sketch (k=32 y 128 se obtienen como prefijo)
#   SALTAR_BENCH=1         omite la medición de throughput (la más lenta)
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")"

SEMILLAS="${SEMILLAS:-1 2 3 4 5}"
B="${B:-4096}"
K="${K:-512}"
TR="${TR:-trazas}"        # carpeta de trazas (configurable para pruebas)
R="${R:-resultados}"      # carpeta de resultados
D25=$TR/201801251400.bin
D26=$TR/201801261400.bin
D30=$TR/201801301400.bin
log() { echo -e "\n[$(date +%H:%M:%S)] === $* ==="; }

# ---------------------------------------------------------------- 0. compilar
log "Compilando (make)"
make -s all

# ---------------------------------------------------------------- 1. trazas
# Descarga (si faltan) y convierte .pcap.gz -> .bin (24 B/paquete, sólo IPv4).
for f in 201801251400 201801261400 201801301400; do
  if [ ! -s "$TR/$f.bin" ] && [ ! -s "$TR/$f.pcap.gz" ]; then
    log "Faltan trazas: descargando"
    bash scripts/descargar_trazas.sh "$TR"
    break
  fi
done
log "Convirtiendo trazas con pcap2bin (omite las ya convertidas)"
bash scripts/convertir_trazas.sh "$TR"

# ---------------------------------------------------------------- 2. entorno Python
log "Entorno Python (.venv)"
if [ ! -x .venv/bin/python ]; then
  python3 -m venv .venv
  .venv/bin/pip install -q -r requirements.txt
fi
PY=.venv/bin/python

mkdir -p $R/verificacion $R/act21 $R/act22 $R/sketches

# ---------------------------------------------------------------- 3. Actividad 1.2
# Pesos, determinismo, una IP, y caché (B=4096 vs Actualizar por paquete, T=10 s, día 26).
log "Actividad 1.2: verificación"
./bin/verify --outdir $R/verificacion --trace $D26 --B 4096 --T 10 | tee $R/verificacion/verify.txt

# ---------------------------------------------------------------- 4. Actividad 1.3 / 2.1
# Referencia exacta (tabla hash) para T = 10, 30, 60, 120 s sobre el día 26.
log "Referencia exacta día 26 (consecutivos)"
./bin/exact_intervals consec --trace $D26 --T 10,30,60,120 \
    --out $R/act21/exact_d26.csv --resumen $R/act21/exact_d26_resumen.csv

# Sketch de Cauchy: una corrida por semilla (en paralelo), todos los T en una pasada.
# k=512 y k'=32,128 como prefijos. La semilla 1 guarda además los sketches por
# intervalo (se reutilizan en la Actividad 2.2), y la semilla 2 también (prueba de
# semillas distintas entre días).
log "Sketch de Cauchy día 26, semillas: $SEMILLAS"
rm -f $R/act21/resumen_d26_s*.csv
for s in $SEMILLAS; do
  extra=""
  if [ "$s" = 1 ] || [ "$s" = 2 ]; then extra="--save-sketches $R/sketches/d26_s$s"; fi
  ./bin/cauchy_intervals --trace $D26 --T 10,30,60,120 --k $K --seed $s --B $B \
      --kprefix 32,128,512 --out $R/act21/est_d26_s$s.csv \
      --resumen $R/act21/resumen_d26_s$s.csv $extra &
done
wait

# ---------------------------------------------------------------- 5. Actividad 2.2
log "Actividad 2.2: sketches días 25 y 30 (sigma=1, T=60 s)"
./bin/cauchy_intervals --trace $D25 --T 60 --k $K --seed 1 --B $B --kprefix $K \
    --save-sketches $R/sketches/d25_s1 &
./bin/cauchy_intervals --trace $D30 --T 60 --k $K --seed 1 --B $B --kprefix $K \
    --save-sketches $R/sketches/d30_s1 &
log "Referencia exacta entre días (25 vs 26 y 26 vs 30), con caracterización top-20"
./bin/exact_intervals cross --a $D25 --b $D26 --T 60 --out $R/act22/d25_d26_intervalos.csv \
    --total $R/act22/d25_d26_total.csv --top 20 --top-out $R/act22/d25_d26_top.csv &
./bin/exact_intervals cross --a $D26 --b $D30 --T 60 --out $R/act22/d26_d30_intervalos.csv \
    --total $R/act22/d26_d30_total.csv --top 20 --top-out $R/act22/d26_d30_top.csv &
wait

# MAWI anonimiza las IPs con una clave distinta en cada traza diaria (Crypto-PAn; ver
# https://mawi.wide.ad.jp/mawi/faq.html), así que las IPs no se pueden emparejar entre
# días. Para caracterizar el día 30 se comparan perfiles (protocolos, puertos, destinos
# de un solo paquete) y el cambio entre minutos consecutivos DENTRO de cada día.
log "Perfil de cada traza y referencia exacta de minutos consecutivos (días 25 y 30)"
for d in 25 26 30; do
  ./bin/perfil --trace $TR/201801${d}1400.bin --out $R/act22/perfil_d$d.csv &
done
for d in 25 30; do
  ./bin/exact_intervals consec --trace $TR/201801${d}1400.bin --T 60 \
      --out $R/act22/exact_consec_d$d.csv --resumen $R/act22/exact_consec_d${d}_resumen.csv &
done
wait

# ---------------------------------------------------------------- 6. caché: phi_B y throughput
# T=60 s, k=512. Se corre en serie (sin otros procesos) y con la traza precargada en
# RAM, para que el throughput mida el procesamiento y no la lectura de disco.
# Sin caché (B=0) se mide sólo el primer intervalo, como permite el enunciado.
if [ "${SALTAR_BENCH:-0}" != 1 ]; then
  log "Benchmark de la caché (B = 256, 4096, 65536 y sin caché)"
  rm -f $R/act21/bench.csv
  for b in 256 4096 65536; do
    ./bin/cauchy_intervals --trace $D26 --T 60 --k 512 --seed 1 --B $b --preload \
        --resumen $R/act21/bench.csv
  done
  ./bin/cauchy_intervals --trace $D26 --T 60 --k 512 --seed 1 --B 0 --preload \
      --max-intervals 1 --resumen $R/act21/bench.csv
fi

# ---------------------------------------------------------------- 7. tablas y figuras
log "Análisis: tablas y figuras"
$PY scripts/analisis.py --res $R
log "Listo. Tablas en $R/tablas, figuras en $R/figuras, cifras en $R/resumen.json"
