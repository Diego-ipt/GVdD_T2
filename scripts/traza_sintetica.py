#!/usr/bin/env python3
"""Genera una traza binaria sintética (formato pcap2bin, 24 B/registro) para pruebas
rápidas del pipeline sin necesitar las trazas MAWI.

Uso: python3 scripts/traza_sintetica.py salida.bin [--paquetes 2000000] [--dur 300] [--seed 1]

Las IPs de destino siguen una distribución tipo Zipf, y a mitad de la traza cambia
parte del conjunto de destinos, de modo que r_q tenga variación visible.
"""
import argparse
import numpy as np

REC = np.dtype([("ts_us", "<u8"), ("src", "<u4"), ("dst", "<u4"), ("sport", "<u2"),
                ("dport", "<u2"), ("len", "<u2"), ("proto", "u1"), ("flags", "u1")])

ap = argparse.ArgumentParser()
ap.add_argument("salida")
ap.add_argument("--paquetes", type=int, default=2_000_000)
ap.add_argument("--dur", type=float, default=300.0)
ap.add_argument("--seed", type=int, default=1)
a = ap.parse_args()

rng = np.random.default_rng(a.seed)
n = a.paquetes
r = np.zeros(n, dtype=REC)
t0 = 1_516_942_800_000_000
r["ts_us"] = np.sort(t0 + (rng.random(n) * a.dur * 1e6).astype(np.uint64))
rank = rng.zipf(1.3, n) % 200_000
mitad = r["ts_us"] > t0 + a.dur * 1e6 / 2
r["dst"] = (0x0A000000 + rank + np.where(mitad & (rank % 3 == 0), 1_000_000, 0)).astype(np.uint32)
r["src"] = rng.integers(0, 2**32, n, dtype=np.uint32)
r["proto"] = 6
r["dport"] = 443
r["len"] = 60
r.tofile(a.salida)
print(f"{n} paquetes escritos en {a.salida}")
