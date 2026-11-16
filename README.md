# Tarea 2 — Sketch de Cauchy para estimar cuánto cambia el tráfico de una red

Tópicos en Grandes Volúmenes de Datos 2026. Implementación del sketch de Cauchy
(Indyk 2000) con una caché de tamaño fijo, una referencia exacta y los experimentos
de las Actividades 1 y 2 sobre trazas MAWI.

La documentación explicativa (qué pide la tarea, teoría, diseño de la caché, código,
resultados y respuestas) está en [`Docs/index.html`](Docs/index.html).

## Dependencias (versiones usadas)

| Herramienta | Versión |
|---|---|
| Linux / WSL2 | Ubuntu 24.04 |
| g++ (C++17) | 13.3.0 |
| GNU make | 4.3 |
| Python | 3.12.3 (con `venv`) |
| numpy / pandas / matplotlib | 2.1.3 / 2.2.3 / 3.9.2 (`requirements.txt`) |
| curl, gzip | cualquiera reciente |

En Windows todo se ejecuta dentro de WSL (`wsl -d Ubuntu`), no desde PowerShell.

## Trazas

Fuente: <https://mawi.wide.ad.jp/mawi/samplepoint-F/2018/>

| Archivo | Día | Uso |
|---|---|---|
| `201801251400.pcap.gz` | jue 25 ene 2018 | Act. 2.2 |
| `201801261400.pcap.gz` | vie 26 ene 2018 | Act. 2.1 y 2.2 |
| `201801301400.pcap.gz` | mar 30 ene 2018 | Act. 2.2 (contraste) |

Deben quedar en la carpeta **`trazas/`** de la raíz del repositorio (ignorada por git).
`reproducir.sh` las descarga si faltan (`scripts/descargar_trazas.sh`, unos 6,7 GB) y
las convierte a `trazas/*.bin` (24 bytes por paquete, sólo IPv4; unos 7,2 GB) con
`pcap2bin` (`scripts/convertir_trazas.sh`).

**Semillas usadas:** σ ∈ {1, 2, 3, 4, 5} en la Actividad 2.1; σ = 1 para ambos días
en la Actividad 2.2 (y σ = 1 para el día 25 / σ = 2 para el día 26 en la prueba de
semillas distintas).

## Ejecución

```bash
git clone https://github.com/Diego-ipt/GVdD_T2.git && cd GVdD_T2
# (opcional) copiar las trazas a trazas/
./reproducir.sh
```

`reproducir.sh` compila, descarga y convierte las trazas si hace falta, crea `.venv`,
corre todos los experimentos y genera tablas y figuras. Variables opcionales:
`SEMILLAS="1 2 3 4 5"`, `B=4096`, `K=512`, `SALTAR_BENCH=1`.

**Tiempo aproximado** (16 hilos, trazas en disco NTFS vía WSL): conversión unos 10 min;
experimentos entre 1 y 2 h, dominadas por las corridas del sketch con k = 512 y el benchmark
sin caché. Los tiempos medidos de cada corrida quedan en `resultados/act21/resumen_*.csv`.

## Programas

| Programa | Fuente | Qué hace |
|---|---|---|
| `bin/pcap2bin` | `tools/pcap2bin.cpp` | Conversor pcap → registros de 24 B (de la Tarea 1, sin cambios). |
| `bin/verify` | `src/verify.cpp` | Actividad 1.2: valores de referencia, distribución y determinismo de los pesos, una IP con cuenta c, caché exacta (B = 4096, T = 10 s). |
| `bin/cauchy_intervals` | `src/cauchy_intervals.cpp` | Algoritmo 2: sketch + caché por intervalos consecutivos; varios T en una pasada; k' = 32, 128 como prefijo de k = 512; guarda sketches por intervalo (`--save-sketches`); mide φ_B y el throughput. |
| `bin/exact_intervals` | `src/exact_intervals.cpp` | Referencia exacta con tablas hash: `consec` (N_q, L_q consecutivos) y `cross` (entre días, por intervalo, traza completa y top IPs con caracterización). |
| `bin/perfil` | `src/perfil.cpp` | Perfil de una traza (protocolos, puertos, destinos de un solo paquete y sus orígenes). Se usa para caracterizar el día 30, porque MAWI anonimiza las IPs con una clave distinta en cada traza diaria y no se pueden emparejar entre días. |
| `scripts/analisis.py` | — | Tablas (`.tex`/`.csv`), figuras (`.png`) y `resumen.json`. |
| `scripts/traza_sintetica.py` | — | Traza sintética pequeña para probar el pipeline sin MAWI. |

Núcleo: `src/cauchy.hpp` (Mezclar, Entrada, Sketch, EstimarDistancia, Cache) y
`src/traza.hpp` (lector de trazas).

## Resultados

| Ruta | Contenido |
|---|---|
| `resultados/verificacion/` | Salida de `verify` y CSV de las pruebas. |
| `resultados/act21/` | Estimaciones por semilla, referencia exacta, resúmenes y benchmark de la caché. |
| `resultados/act22/` | Referencia exacta entre días y caracterización de las top IPs. |
| `resultados/sketches/` | Sketches por intervalo (binario, ignorado por git). |
| `resultados/tablas/`, `resultados/figuras/` | Tablas y figuras del informe. |
| `informe/` | Informe en LaTeX. |
