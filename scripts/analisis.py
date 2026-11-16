#!/usr/bin/env python3
"""Análisis de resultados de la Tarea 2: genera todas las tablas y figuras del informe.

Uso: python3 scripts/analisis.py [--res resultados]

Lee los CSV producidos por bin/verify, bin/cauchy_intervals y bin/exact_intervals
(ver reproducir.sh) y escribe:
  resultados/tablas/*.csv y *.tex   tablas del informe
  resultados/figuras/*.png          figuras del informe
  resultados/resumen.json           cifras clave (usadas por el informe y Docs)
"""
import argparse
import glob
import json
import math
import os
import struct

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

ap = argparse.ArgumentParser()
ap.add_argument("--res", default="resultados")
args = ap.parse_args()
R = args.res
TAB = os.path.join(R, "tablas")
FIG = os.path.join(R, "figuras")
os.makedirs(TAB, exist_ok=True)
os.makedirs(FIG, exist_ok=True)
KS = [32, 128, 512]
RES = {}

plt.rcParams.update({"figure.dpi": 130, "font.size": 9, "axes.grid": True, "grid.alpha": .3})


def tex(df, path, fmt=None, caption=None):
    """Escribe una tabla LaTeX simple (booktabs) sin depender de jinja2."""
    fmt = fmt or {}
    cols = list(df.columns)
    lines = ["\\begin{tabular}{" + "r" * len(cols) + "}", "\\toprule",
             " & ".join(str(c).replace("_", "\\_").replace("%", "\\%") for c in cols) + " \\\\", "\\midrule"]
    for _, row in df.iterrows():
        cells = []
        for c in cols:
            v = row[c]
            f = fmt.get(c)
            if f and isinstance(v, (int, float, np.integer, np.floating)) and not pd.isna(v):
                cells.append(f.format(v))
            else:
                cells.append(str(v).replace("_", "\\_").replace("%", "\\%"))
        lines.append(" & ".join(cells) + " \\\\")
    lines += ["\\bottomrule", "\\end{tabular}"]
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    df.to_csv(path.replace(".tex", ".csv"), index=False)


def leer_sketches(path):
    """Lee un archivo .sk escrito por cauchy_intervals --save-sketches."""
    with open(path, "rb") as f:
        data = f.read()
    assert data[:4] == b"CSK1", path
    k, = struct.unpack_from("<I", data, 4)
    sigma, = struct.unpack_from("<Q", data, 8)
    T, = struct.unpack_from("<d", data, 16)
    off, out = 24, []
    rec = 4 + 1 + 8 + 8 * k
    while off + rec <= len(data):
        q, cerr, N = struct.unpack_from("<IBQ", data, off)
        y = np.frombuffer(data, dtype="<f8", count=k, offset=off + 13)
        out.append({"q": q, "cerrado": cerr, "N": N, "y": y.copy()})
        off += rec
    return {"k": k, "sigma": sigma, "T": T, "int": out}


def mediana_l1(ya, yb, k=None):
    d = np.abs(ya - yb)
    if k:
        d = d[:k]
    return float(np.median(d))


# =========================================================== Actividad 1: verificación
V = os.path.join(R, "verificacion")
if os.path.exists(os.path.join(V, "pesos_muestra.csv")):
    m = pd.read_csv(os.path.join(V, "pesos_muestra.csv"))["c"].values
    fig, ax = plt.subplots(1, 2, figsize=(8, 3))
    xs = np.linspace(-10, 10, 400)
    ax[0].hist(m[np.abs(m) < 10], bins=200, density=True, alpha=.6, label="pesos $C_{ji}$")
    # densidad Cauchy condicionada a |X|<10 (el histograma está truncado)
    ax[0].plot(xs, 1 / (np.pi * (1 + xs ** 2)) / (2 / np.pi * np.arctan(10)), "r", lw=1, label="densidad Cauchy")
    ax[0].set_title("Histograma de pesos (|c|<10)")
    ax[0].legend()
    q = pd.read_csv(os.path.join(V, "pesos_cuantiles.csv"))
    ax[1].loglog(q["teorico"], q["empirico"], "o")
    lim = [q["teorico"].min(), q["teorico"].max()]
    ax[1].plot(lim, lim, "k--", lw=.8)
    ax[1].set_xlabel("cuantil teórico de |X|")
    ax[1].set_ylabel("cuantil empírico de |C|")
    ax[1].set_title("Cuantiles de |C| (>2·10^6 entradas)")
    fig.tight_layout()
    fig.savefig(os.path.join(FIG, "verif_pesos.png"))
    plt.close(fig)
    tex(q.rename(columns={"p": "p", "empirico": "empírico", "teorico": "teórico"}),
        os.path.join(TAB, "verif_cuantiles.tex"), {"empírico": "{:.4f}", "teórico": "{:.4f}"})

if os.path.exists(os.path.join(V, "una_ip.csv")):
    u = pd.read_csv(os.path.join(V, "una_ip.csv"))
    t = u.groupby("k")["ratio"].agg(["mean", "std", "count"]).reset_index()
    t["teorico"] = np.pi / (2 * np.sqrt(t["k"]))
    t.columns = ["k", "media Lhat/c", "desv Lhat/c", "n", "pi/(2 sqrt k)"]
    tex(t, os.path.join(TAB, "verif_una_ip.tex"),
        {"media Lhat/c": "{:.4f}", "desv Lhat/c": "{:.4f}", "pi/(2 sqrt k)": "{:.4f}"})
    RES["una_ip"] = t.to_dict("records")

if os.path.exists(os.path.join(V, "cache.csv")):
    c = pd.read_csv(os.path.join(V, "cache.csv"))
    tex(c[["prueba", "B", "k", "Lhat_cache", "Lhat_directo", "dif_rel", "phi"]],
        os.path.join(TAB, "verif_cache.tex"),
        {"Lhat_cache": "{:.3f}", "Lhat_directo": "{:.3f}", "dif_rel": "{:.2e}", "phi": "{:.4f}"})
    RES["cache_verif"] = c.to_dict("records")

# =========================================================== Actividad 2.1
A21 = os.path.join(R, "act21")
ex_path = os.path.join(A21, "exact_d26.csv")
if os.path.exists(ex_path):
    ex = pd.read_csv(ex_path)
    exr = pd.read_csv(os.path.join(A21, "exact_d26_resumen.csv"))
    ests = []
    for f in sorted(glob.glob(os.path.join(A21, "est_d26_s*.csv"))):
        seed = int(os.path.basename(f).split("_s")[1].split(".")[0])
        e = pd.read_csv(f)
        e["seed"] = seed
        ests.append(e)
    est = pd.concat(ests)
    df = est.merge(ex, on=["T", "q", "N_q", "N_qm1"], how="inner")
    assert len(df) == len(est), "los N_q del sketch y de la referencia exacta no coinciden"
    larga = []
    for k in KS:
        d = df[["T", "q", "seed", "N_q", "N_qm1", "L_q"]].copy()
        d["k"] = k
        d["Lhat"] = df[f"Lhat_k{k}"]
        larga.append(d)
    L = pd.concat(larga)
    L = L[L["L_q"] > 0]
    L["ratio"] = L["Lhat"] / L["L_q"]
    L["err_rel"] = (L["Lhat"] - L["L_q"]).abs() / L["L_q"]
    L.to_csv(os.path.join(TAB, "act21_detalle.csv"), index=False)

    # memoria
    res_files = sorted(glob.glob(os.path.join(A21, "resumen_d26_s*.csv")))
    rs = pd.concat([pd.read_csv(f) for f in res_files])
    B_used = int(rs["B"].iloc[0])
    mem_cache = int(rs["mem_cache_bytes"].iloc[0])
    memhash = dict(zip(exr["T"], exr["mem_hash_bytes"]))
    distintas = dict(zip(exr["T"], exr["max_distintas"]))

    tab = (L.groupby(["T", "k"]).agg(err_medio=("err_rel", "mean"),
                                    p95=("err_rel", lambda x: np.percentile(x, 95)),
                                    desv_ratio=("ratio", "std"),
                                    media_ratio=("ratio", "mean"),
                                    n=("ratio", "size")).reset_index())
    tab["pi/(2sqrt k)"] = np.pi / (2 * np.sqrt(tab["k"]))
    tab["mem_sketch_KiB"] = (2 * tab["k"] * 8 + mem_cache) / 1024
    tab["mem_hash_MiB"] = tab["T"].map(memhash) / 2 ** 20
    tab.to_csv(os.path.join(TAB, "act21_resumen.csv"), index=False)
    tex(tab[["T", "k", "n", "err_medio", "p95", "desv_ratio", "pi/(2sqrt k)", "mem_sketch_KiB", "mem_hash_MiB"]],
        os.path.join(TAB, "act21_resumen.tex"),
        {"err_medio": "{:.3f}", "p95": "{:.3f}", "desv_ratio": "{:.3f}", "pi/(2sqrt k)": "{:.3f}",
         "mem_sketch_KiB": "{:.1f}", "mem_hash_MiB": "{:.1f}"})
    RES["act21"] = tab.to_dict("records")
    RES["B_usado"] = B_used

    # duración y número de comparaciones
    s1 = rs[rs["seed"] == rs["seed"].min()]
    dq = s1[["T", "D_s", "Q", "comparaciones"]].copy()
    dq["Q-2"] = dq["Q"] - 2
    dq["ok"] = np.where(dq["comparaciones"] == dq["Q-2"], "sí", "no")
    dq["comparaciones_exacta"] = dq["T"].map(dict(zip(exr["T"], exr["comparaciones"])))
    dq["IPs_distintas_max"] = dq["T"].map(distintas)
    tex(dq, os.path.join(TAB, "act21_duracion.tex"), {"D_s": "{:.3f}"})
    RES["act21_duracion"] = dq.to_dict("records")

    # figura: error vs k por T, con la curva teórica
    fig, ax = plt.subplots(1, 2, figsize=(8, 3))
    kk = np.array(KS)
    for T, g in tab.groupby("T"):
        ax[0].plot(g["k"], g["desv_ratio"], "o-", label=f"T={T:g} s")
        ax[1].plot(g["k"], g["err_medio"], "o-", label=f"T={T:g} s")
    kx = np.linspace(28, 600, 100)
    ax[0].plot(kx, np.pi / (2 * np.sqrt(kx)), "k--", label=r"$\pi/(2\sqrt{k})$")
    ax[1].plot(kx, np.pi / (2 * np.sqrt(kx)) * np.sqrt(2 / np.pi), "k:", label=r"$\sqrt{2/\pi}\cdot\pi/(2\sqrt{k})$")
    for a, t in zip(ax, ["desv. estándar de $\\hat L_q/L_q$", "error relativo medio"]):
        a.set_xscale("log", base=2)
        a.set_yscale("log")
        a.set_xticks(KS, [str(k) for k in KS])
        a.set_xlabel("k")
        a.set_title(t)
        a.legend(fontsize=7)
    fig.tight_layout()
    fig.savefig(os.path.join(FIG, "act21_error_vs_k.png"))
    plt.close(fig)

    # figura: r_q exacto y estimado, un panel por T (semilla mínima, k=32 y 512)
    s0 = L["seed"].min()
    Ts = sorted(L["T"].unique())
    fig, axs = plt.subplots(len(Ts), 1, figsize=(8, 2.0 * len(Ts)), sharex=True)
    for a, T in zip(np.atleast_1d(axs), Ts):
        g = L[(L["T"] == T) & (L["seed"] == s0)]
        base = g[g["k"] == 512].sort_values("q")
        tmin = base["q"] * T / 60
        den = base["N_q"] + base["N_qm1"]
        a.plot(tmin, base["L_q"] / den, "k-", lw=1.4, label="exacto")
        for k, sty in [(32, ":"), (128, "-."), (512, "--")]:
            gk = g[g["k"] == k].sort_values("q")
            a.plot(gk["q"] * T / 60, gk["Lhat"] / (gk["N_q"] + gk["N_qm1"]), sty, lw=.9, label=f"k={k}")
        a.set_ylabel(f"$r_q$ (T={T:g} s)")
        a.legend(fontsize=6, ncol=4, loc="upper right")
    np.atleast_1d(axs)[-1].set_xlabel("minuto de la traza (fin del intervalo q)")
    fig.suptitle(f"Día 26: cambio relativo entre intervalos consecutivos (σ={s0})")
    fig.tight_layout()
    fig.savefig(os.path.join(FIG, "act21_rq.png"))
    plt.close(fig)

    # histograma de Lhat/L para k=512 (todas las semillas y T)
    fig, ax = plt.subplots(figsize=(5, 3))
    for k in KS:
        ax.hist(L[L["k"] == k]["ratio"], bins=60, histtype="step", density=True, label=f"k={k}")
    ax.set_xlabel(r"$\hat L_q / L_q$")
    ax.set_title("Distribución del cociente estimado/exacto")
    ax.legend()
    fig.tight_layout()
    fig.savefig(os.path.join(FIG, "act21_hist_ratio.png"))
    plt.close(fig)

# benchmark de la caché
bench = os.path.join(A21, "bench.csv")
if os.path.exists(bench):
    b = pd.read_csv(bench)
    b["B"] = b["B"].astype(int)
    b["caché"] = np.where(b["B"] == 0, "sin caché", b["B"].astype(str))
    b["intervalos"] = np.where(b["max_intervals"] > 0, b["max_intervals"].astype(str), "todos")
    t = b[["caché", "intervalos", "paquetes", "llamadas", "phi", "t_proceso_s", "throughput_pps", "mem_cache_bytes"]]
    tex(t, os.path.join(TAB, "act21_cache.tex"),
        {"phi": "{:.4f}", "t_proceso_s": "{:.1f}", "throughput_pps": "{:,.0f}"})
    RES["bench"] = t.to_dict("records")

# =========================================================== Actividad 2.2
A22 = os.path.join(R, "act22")
SK = os.path.join(R, "sketches")


def cruzar(da, db, sa, sb, nombre, exact_prefix):
    """Compara dos días con sketches guardados (T=60) contra la referencia exacta."""
    fa = os.path.join(SK, f"d{da}_s{sa}_T60.sk")
    fb = os.path.join(SK, f"d{db}_s{sb}_T60.sk")
    if not (os.path.exists(fa) and os.path.exists(fb)):
        return None
    A, B = leer_sketches(fa), leer_sketches(fb)
    ex = pd.read_csv(os.path.join(A22, f"{exact_prefix}_intervalos.csv"))
    tot = pd.read_csv(os.path.join(A22, f"{exact_prefix}_total.csv")).set_index("alcance")
    ia = {x["q"]: x for x in A["int"]}
    ib = {x["q"]: x for x in B["int"]}
    filas = []
    for _, r in ex.iterrows():
        q = int(r["q"])
        if q in ia and q in ib and ia[q]["cerrado"] and ib[q]["cerrado"]:
            assert ia[q]["N"] == r["NA_q"] and ib[q]["N"] == r["NB_q"]
            fila = {"q": q, "NA": r["NA_q"], "NB": r["NB_q"], "L": r["L_q"]}
            for k in KS:
                fila[f"Lhat_k{k}"] = mediana_l1(ia[q]["y"], ib[q]["y"], k)
            filas.append(fila)
    d = pd.DataFrame(filas)
    d["err_k512"] = (d["Lhat_k512"] - d["L"]).abs() / d["L"]
    d["r"] = d["L"] / (d["NA"] + d["NB"])
    d["rhat_k512"] = d["Lhat_k512"] / (d["NA"] + d["NB"])
    d["|NA-NB|"] = (d["NA"] - d["NB"]).abs()
    d.to_csv(os.path.join(TAB, f"act22_{nombre}_intervalos.csv"), index=False)
    # traza completa por linealidad: suma de los sketches de todos los intervalos
    ya = sum(x["y"] for x in A["int"])
    yb = sum(x["y"] for x in B["int"])
    NA = sum(x["N"] for x in A["int"])
    NB = sum(x["N"] for x in B["int"])
    full = {"NA": NA, "NB": NB, "L_exacto": int(tot.loc["todos", "L"]),
            "|NA-NB|": abs(NA - NB)}
    assert NA == tot.loc["todos", "NA"] and NB == tot.loc["todos", "NB"]
    for k in KS:
        full[f"Lhat_k{k}"] = mediana_l1(ya, yb, k)
    full["err_k512"] = abs(full["Lhat_k512"] - full["L_exacto"]) / full["L_exacto"]
    full["r_exacto"] = full["L_exacto"] / (NA + NB)
    full["rhat_k512"] = full["Lhat_k512"] / (NA + NB)
    return d, full


cruces = {}
for (da, db, sa, sb, nombre, pref) in [(25, 26, 1, 1, "d25_d26", "d25_d26"),
                                       (25, 26, 1, 2, "d25_d26_semillas_distintas", "d25_d26"),
                                       (26, 30, 1, 1, "d26_d30", "d26_d30")]:
    r = cruzar(da, db, sa, sb, nombre, pref)
    if r:
        cruces[nombre] = r

if cruces:
    filas = []
    for nombre, (d, full) in cruces.items():
        filas.append({"comparación": nombre, "intervalos": len(d),
                      "err_medio_k512": d["err_k512"].mean(), "p95_k512": np.percentile(d["err_k512"], 95),
                      "r_medio": d["r"].mean(), "rhat_medio": d["rhat_k512"].mean(),
                      "L_total": full["L_exacto"], "Lhat_total_k512": full["Lhat_k512"],
                      "err_total": full["err_k512"], "r_total": full["r_exacto"],
                      "|NA-NB|_total": full["|NA-NB|"]})
    t = pd.DataFrame(filas)
    tex(t, os.path.join(TAB, "act22_resumen.tex"),
        {"err_medio_k512": "{:.3f}", "p95_k512": "{:.3f}", "r_medio": "{:.3f}", "rhat_medio": "{:.3f}",
         "Lhat_total_k512": "{:,.0f}", "L_total": "{:,}", "err_total": "{:.3f}", "r_total": "{:.3f}",
         "|NA-NB|_total": "{:,}"})
    RES["act22"] = t.to_dict("records")
    RES["act22_full"] = {n: f for n, (_, f) in cruces.items()}

    # tabla por intervalo de 25 vs 26
    if "d25_d26" in cruces:
        d = cruces["d25_d26"][0]
        tex(d[["q", "NA", "NB", "|NA-NB|", "L", "Lhat_k512", "err_k512", "r", "rhat_k512"]],
            os.path.join(TAB, "act22_d25_d26_intervalos.tex"),
            {"L": "{:,}", "Lhat_k512": "{:,.0f}", "err_k512": "{:.3f}", "r": "{:.3f}", "rhat_k512": "{:.3f}",
             "NA": "{:,}", "NB": "{:,}", "|NA-NB|": "{:,}"})

    # figura: r entre días vs r entre minutos consecutivos
    fig, ax = plt.subplots(1, 2, figsize=(9, 3.2))
    if os.path.exists(ex_path):
        cons = ex[ex["T"] == 60]
        ax[0].plot(cons["q"], cons["L_q"] / (cons["N_q"] + cons["N_qm1"]), "k.-", label="día 26, q vs q−1")
    for nombre, sty in [("d25_d26", "o-"), ("d26_d30", "s-"), ("d25_d26_semillas_distintas", "x:")]:
        if nombre in cruces:
            d = cruces[nombre][0]
            if "semillas" not in nombre:
                ax[0].plot(d["q"], d["r"], sty, ms=3, label=f"{nombre} exacto")
            ax[0].plot(d["q"], d["rhat_k512"], sty, ms=3, alpha=.6, label=f"{nombre} sketch k=512")
    ax[0].set_xlabel("intervalo q (T=60 s)")
    ax[0].set_ylabel("r")
    ax[0].set_title("Cambio relativo r")
    ax[0].legend(fontsize=6)
    for nombre, col in [("d25_d26", "C0"), ("d26_d30", "C3")]:
        if nombre in cruces:
            d = cruces[nombre][0]
            ax[1].plot(d["q"], d["L"], "-", color=col, lw=1.5, label=f"{nombre}: L exacto")
            ax[1].plot(d["q"], d["Lhat_k512"], "o", color=col, ms=4, label=f"{nombre}: $\\hat L$ k=512")
            ax[1].plot(d["q"], d["|NA-NB|"], "s:", color=col, ms=3, mfc="none", label=f"{nombre}: |NA−NB|")
    ax[1].set_yscale("log")
    ax[1].set_xlabel("intervalo q (T=60 s)")
    ax[1].set_ylabel("paquetes")
    ax[1].set_title(r"Entre días: L, $\hat L$ y |NA−NB|")
    ax[1].legend(fontsize=6)
    fig.tight_layout()
    fig.savefig(os.path.join(FIG, "act22_entre_dias.png"))
    plt.close(fig)

# top IPs por aporte a L (cada fila: la IP en el día donde aparece)
def tabla_top(path, dias, nombre):
    if not os.path.exists(path):
        return None
    tp = pd.read_csv(path, keep_default_na=False)
    tp = tp[tp["paquetes"] > 0].copy()          # el día en que la IP tiene tráfico
    tp["dia"] = tp["dia"].map(dict(zip(["A", "B"], dias)))
    tp["proto"] = np.select([tp["tcp"] > .5, tp["udp"] > .5, tp["icmp"] > .5], ["TCP", "UDP", "ICMP"], "mixto")
    tp["frac_L_acum"] = tp["frac_L"].cumsum()
    tp["src_distintos"] = tp["src_distintos"].map(lambda v: ">=1.000.000" if v >= 1000000 else f"{v:,}")
    tp.to_csv(os.path.join(TAB, f"act22_top_{nombre}.csv"), index=False)
    tex(tp.head(10)[["rank", "ip_dst", "dia", "paquetes", "frac_L", "proto", "bytes_medios", "src_distintos",
                     "top_sport"]],
        os.path.join(TAB, f"act22_top_{nombre}.tex"),
        {"frac_L": "{:.3f}", "paquetes": "{:,}", "bytes_medios": "{:.0f}"})
    return tp


t30 = tabla_top(os.path.join(A22, "d26_d30_top.csv"), ["26", "30"], "d26_d30")
t25 = tabla_top(os.path.join(A22, "d25_d26_top.csv"), ["25", "26"], "d25_d26")
if t30 is not None:
    RES["top_d26_d30"] = t30.head(10).to_dict("records")
    RES["top10_fracL_d26_d30"] = float(t30.head(10)["frac_L"].sum())
if t25 is not None:
    RES["top_d25_d26"] = t25.head(10).to_dict("records")
    RES["top10_fracL_d25_d26"] = float(t25.head(10)["frac_L"].sum())

# perfil de cada traza (bin/perfil)
perf = {}
for d in (25, 26, 30):
    f = os.path.join(A22, f"perfil_d{d}.csv")
    if os.path.exists(f):
        perf[d] = pd.read_csv(f, keep_default_na=False, dtype={"clave": str})
if perf:
    filas = []
    for d, p in perf.items():
        g = lambda sec, key: p[(p["seccion"] == sec) & (p["clave"] == key)]
        v = lambda sec, key, col="valor": float(g(sec, key)[col].iloc[0]) if len(g(sec, key)) else 0.0
        top_src = p[p["seccion"] == "unicos_src"].head(2)
        filas.append({
            "día": d, "paquetes": int(v("total", "paquetes")),
            "TCP_%": 100 * v("proto", "tcp", "frac"), "UDP_%": 100 * v("proto", "udp", "frac"),
            "ICMP_%": 100 * v("proto", "icmp", "frac"),
            "destinos": int(v("total", "destinos_distintos")),
            "dest_1pkt": int(v("total", "destinos_1_paquete")),
            "1pkt_ICMP": int(v("unicos_proto", "1")), "1pkt_UDP53": int(v("unicos_dport", "17/53")),
            "UDP53_%UDP": 100 * v("dport_udp", "53", "frac"),
            "top_src_1pkt": "; ".join(f"{r.clave} ({int(r.valor):,})" for r in top_src.itertuples()),
        })
    pf = pd.DataFrame(filas)
    tex(pf.drop(columns=["top_src_1pkt"]), os.path.join(TAB, "act22_perfil.tex"),
        {"paquetes": "{:,}", "TCP_%": "{:.1f}", "UDP_%": "{:.1f}", "ICMP_%": "{:.1f}", "destinos": "{:,}",
         "dest_1pkt": "{:,}", "1pkt_ICMP": "{:,}", "1pkt_UDP53": "{:,}", "UDP53_%UDP": "{:.1f}"})
    pf.to_csv(os.path.join(TAB, "act22_perfil.csv"), index=False)
    RES["perfil"] = pf.to_dict("records")

# r entre minutos consecutivos dentro de cada día (exacto, T=60) vs r entre días
cons = {}
for d, f in [(25, os.path.join(A22, "exact_consec_d25.csv")), (26, ex_path),
             (30, os.path.join(A22, "exact_consec_d30.csv"))]:
    if os.path.exists(f):
        c = pd.read_csv(f)
        c = c[c["T"] == 60]
        cons[d] = c.assign(r=c["L_q"] / (c["N_q"] + c["N_qm1"]))
if cons:
    fig, ax = plt.subplots(figsize=(7, 3))
    for d, c in cons.items():
        ax.plot(c["q"], c["r"], "o-", ms=3, label=f"día {d}: minuto q vs q−1")
    for nombre, sty in [("d25_d26", "k--"), ("d26_d30", "r:")]:
        if nombre in cruces:
            dd = cruces[nombre][0]
            ax.plot(dd["q"], dd["r"], sty, label=f"{nombre}: minuto q de cada día")
    ax.set_ylim(0, 1.05)
    ax.set_xlabel("intervalo q (T = 60 s)")
    ax.set_ylabel("r exacto")
    ax.set_title("Cambio relativo dentro de cada día y entre días")
    ax.legend(fontsize=7, loc="center right")
    fig.tight_layout()
    fig.savefig(os.path.join(FIG, "act22_r_dias.png"))
    plt.close(fig)
    RES["r_consec_medio"] = {str(d): float(c["r"].mean()) for d, c in cons.items()}

# semillas distintas dentro del mismo día: sketch del minuto q con sigma=1 y del q-1 con sigma=2
f1, f2 = os.path.join(SK, "d26_s1_T60.sk"), os.path.join(SK, "d26_s2_T60.sk")
if os.path.exists(f1) and os.path.exists(f2) and os.path.exists(ex_path):
    S1, S2 = leer_sketches(f1), leer_sketches(f2)
    i1 = {x["q"]: x for x in S1["int"]}
    i2 = {x["q"]: x for x in S2["int"]}
    c = pd.read_csv(ex_path)
    c = c[c["T"] == 60]
    filas = []
    for _, r in c.iterrows():
        q = int(r["q"])
        igual = mediana_l1(i1[q]["y"], i1[q - 1]["y"])
        dist = mediana_l1(i1[q]["y"], i2[q - 1]["y"])
        filas.append({"q": q, "L": r["L_q"], "N_q+N_qm1": r["N_q"] + r["N_qm1"],
                      "Lhat_misma": igual, "Lhat_distintas": dist})
    sd = pd.DataFrame(filas)
    sd["err_misma"] = (sd["Lhat_misma"] - sd["L"]).abs() / sd["L"]
    sd["Lhat_dist/(N_q+N_qm1)"] = sd["Lhat_distintas"] / sd["N_q+N_qm1"]
    sd["Lhat_dist/L"] = sd["Lhat_distintas"] / sd["L"]
    tex(sd, os.path.join(TAB, "act22_semillas_mismo_dia.tex"),
        {"L": "{:,}", "N_q+N_qm1": "{:,}", "Lhat_misma": "{:,.0f}", "Lhat_distintas": "{:,.0f}",
         "err_misma": "{:.3f}", "Lhat_dist/(N_q+N_qm1)": "{:.3f}", "Lhat_dist/L": "{:.2f}"})
    RES["semillas_mismo_dia"] = {"Lhat_dist_sobre_L_medio": float(sd["Lhat_dist/L"].mean()),
                                 "Lhat_dist_sobre_Nsuma_medio": float(sd["Lhat_dist/(N_q+N_qm1)"].mean()),
                                 "err_misma_medio": float(sd["err_misma"].mean())}

# estadísticas de pcap2bin por traza
pc = {}
for f in sorted(glob.glob("trazas/*.pcap2bin.txt")):
    d = {}
    for ln in open(f, encoding="utf-8", errors="replace"):
        if ":" in ln:
            a, b = ln.split(":", 1)
            d[a.strip()] = b.strip()
    pc[os.path.basename(f).split(".")[0]] = d
RES["pcap2bin"] = pc


def limpiar(o):
    if isinstance(o, dict):
        return {str(k): limpiar(v) for k, v in o.items()}
    if isinstance(o, list):
        return [limpiar(v) for v in o]
    if isinstance(o, (np.integer,)):
        return int(o)
    if isinstance(o, (np.floating,)):
        return float(o)
    return o


with open(os.path.join(R, "resumen.json"), "w", encoding="utf-8") as f:
    json.dump(limpiar(RES), f, indent=1, ensure_ascii=False)
print("tablas en", TAB, "| figuras en", FIG)
