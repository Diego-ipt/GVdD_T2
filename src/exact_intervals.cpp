// exact_intervals.cpp -- Referencia exacta con tablas hash IP -> cuenta.
//
// Modo consecutivo (Actividad 1.3 / 2.1):
//   exact_intervals consec --trace X.bin [--T 10,30,60,120] --out exact.csv [--resumen r.csv]
//   Para cada T y q>=2: N_q, N_{q-1}, L_q = ||x_q - x_{q-1}||_1 (exacto), IPs distintas
//   y memoria aproximada de la tabla hash.
//
// Modo entre días (Actividad 2.2):
//   exact_intervals cross --a A.bin --b B.bin [--T 60] --out por_intervalo.csv
//                         --total total.csv [--top 20 --top-out top.csv]
//   Por intervalo q (cada traza con su propio t0): N^A_q, N^B_q, ||x^A_q - x^B_q||_1.
//   Traza completa: L con todos los paquetes y con sólo los intervalos cerrados.
//   Top IPs de destino que más aportan a L (traza completa) y su caracterización
//   (protocolos, puertos, orígenes distintos) en una segunda pasada.
//
// Clave: IP de destino. Misma regla de intervalos que el Algoritmo 2:
//   el intervalo q cubre (t0+(q-1)T, t0+qT]; el primer paquete va al intervalo 1.
#include "traza.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <unordered_map>
#include <unordered_set>

using Mapa = std::unordered_map<uint32_t, uint64_t>;

// Bytes aproximados de un unordered_map (nodo: puntero next + par + hash cacheado,
// redondeado por malloc a 32 B; más 8 B por bucket).
static size_t bytes_mapa(const Mapa &m) { return m.size() * 32 + m.bucket_count() * 8; }

static uint64_t l1(const Mapa &a, const Mapa &b) {
    uint64_t L = 0;
    for (auto &kv : a) {
        auto it = b.find(kv.first);
        uint64_t v = it == b.end() ? 0 : it->second;
        L += kv.second > v ? kv.second - v : v - kv.second;
    }
    for (auto &kv : b) if (!a.count(kv.first)) L += kv.second;
    return L;
}

// ------------------------------------------------------------------ consecutivo
static int modo_consec(const std::string &trace, const std::vector<double> &Ts,
                       const std::string &out, const std::string &resumen) {
    struct E { double T; uint64_t Tus, q = 1, NA = 0, NP = 0, comps = 0; Mapa A, P;
               size_t max_bytes = 0, max_dist = 0; };
    std::vector<E> es;
    for (double T : Ts) { E e; e.T = T; e.Tus = (uint64_t)std::llround(T * 1e6); es.push_back(std::move(e)); }
    std::FILE *fo = std::fopen(out.c_str(), "w");
    if (!fo) { std::cerr << "no se pudo crear " << out << "\n"; return 1; }
    std::fprintf(fo, "T,q,N_q,N_qm1,L_q,distintas_q,distintas_qm1\n");

    LectorTraza rd(trace);
    uint64_t t0 = 0, tu = 0, n = 0;
    for (const Record *r; (r = rd.next());) {
        uint64_t t = r->ts_us;
        if (n++ == 0) t0 = t;
        tu = t;
        for (auto &e : es) {
            while (t > t0 + e.q * e.Tus) {
                e.max_bytes = std::max(e.max_bytes, bytes_mapa(e.A) + bytes_mapa(e.P));
                e.max_dist = std::max(e.max_dist, e.A.size());
                if (e.q >= 2) {
                    std::fprintf(fo, "%g,%llu,%llu,%llu,%llu,%zu,%zu\n", e.T, (unsigned long long)e.q,
                                 (unsigned long long)e.NA, (unsigned long long)e.NP,
                                 (unsigned long long)l1(e.A, e.P), e.A.size(), e.P.size());
                    e.comps++;
                }
                std::swap(e.A, e.P); e.NP = e.NA;
                e.A.clear(); e.NA = 0; e.q++;
            }
            e.A[r->dst]++; e.NA++;
        }
    }
    std::fclose(fo);
    double D = (tu - t0) / 1e6;
    if (!resumen.empty()) {
        std::FILE *fr = std::fopen(resumen.c_str(), "w");
        std::fprintf(fr, "T,paquetes,D_s,Q,comparaciones,max_distintas,mem_hash_bytes\n");
        for (auto &e : es)
            std::fprintf(fr, "%g,%llu,%.6f,%lld,%llu,%zu,%zu\n", e.T, (unsigned long long)n, D,
                         (long long)std::ceil(D / e.T - 1e-12), (unsigned long long)e.comps,
                         e.max_dist, e.max_bytes);
        std::fclose(fr);
    }
    std::cerr << "paquetes=" << n << " D=" << D << " s\n";
    return 0;
}

// ------------------------------------------------------------------ entre días
// Lector que entrega los paquetes de un intervalo a la vez.
struct LectorIntervalos {
    LectorTraza rd;
    uint64_t Tus, t0 = 0, q = 1;
    const Record *pend = nullptr;
    bool inicio = true;
    LectorIntervalos(const std::string &p, uint64_t T) : rd(p), Tus(T) {}
    // Llena m con el intervalo q actual. Devuelve false si no hay más paquetes.
    // 'cerrado' indica si después vino un paquete de un intervalo posterior.
    bool siguiente(Mapa &m, uint64_t &N, bool &cerrado, Mapa &total) {
        m.clear(); N = 0; cerrado = false;
        if (inicio) { pend = rd.next(); if (!pend) return false; t0 = pend->ts_us; inicio = false; }
        if (!pend) return false;
        while (pend && pend->ts_us <= t0 + q * Tus) {
            m[pend->dst]++; total[pend->dst]++; N++;
            pend = rd.next();
        }
        cerrado = pend != nullptr;
        q++;
        return true;
    }
};

struct Carac {   // caracterización de una IP de destino en una traza
    uint64_t pk = 0, bytes = 0, tcp = 0, udp = 0, icmp = 0, otro = 0, syn = 0;
    std::unordered_map<uint16_t, uint64_t> dport, sport;
    std::unordered_set<uint32_t> srcs;
};

static std::string top_puertos(const std::unordered_map<uint16_t, uint64_t> &m, uint64_t tot, int n) {
    std::vector<std::pair<uint64_t, uint16_t>> v;
    for (auto &kv : m) v.push_back({kv.second, kv.first});
    std::sort(v.rbegin(), v.rend());
    std::string s;
    for (int i = 0; i < (int)v.size() && i < n; ++i) {
        char b[48]; std::snprintf(b, sizeof b, "%s%u(%.0f%%)", i ? " " : "", v[i].second, 100.0 * v[i].first / tot);
        s += b;
    }
    return s;
}

static int modo_cross(const std::string &A, const std::string &B, double T, const std::string &out,
                      const std::string &total_out, int ntop, const std::string &top_out) {
    uint64_t Tus = (uint64_t)std::llround(T * 1e6);
    LectorIntervalos la(A, Tus), lb(B, Tus);
    Mapa ma, mb, ta, tb, ta_c, tb_c;  // por intervalo, totales, totales sólo cerrados
    std::FILE *fo = std::fopen(out.c_str(), "w");
    std::fprintf(fo, "q,NA_q,NB_q,L_q,cerrado_A,cerrado_B\n");
    uint64_t NA_tot = 0, NB_tot = 0, NA_c = 0, NB_c = 0;
    bool okA = true, okB = true;
    for (uint64_t q = 1; okA || okB; ++q) {
        uint64_t na = 0, nb = 0; bool ca = false, cb = false;
        okA = okA && la.siguiente(ma, na, ca, ta);
        okB = okB && lb.siguiente(mb, nb, cb, tb);
        if (!okA) ma.clear(), na = 0;
        if (!okB) mb.clear(), nb = 0;
        if (!okA && !okB) break;
        NA_tot += na; NB_tot += nb;
        if (okA && ca && okB && cb) {   // sólo intervalos cerrados presentes en ambos
            std::fprintf(fo, "%llu,%llu,%llu,%llu,%d,%d\n", (unsigned long long)q,
                         (unsigned long long)na, (unsigned long long)nb, (unsigned long long)l1(ma, mb), ca, cb);
        }
        if (okA && ca) { for (auto &kv : ma) ta_c[kv.first] += kv.second; NA_c += na; }
        if (okB && cb) { for (auto &kv : mb) tb_c[kv.first] += kv.second; NB_c += nb; }
    }
    std::fclose(fo);

    uint64_t L_all = l1(ta, tb), L_c = l1(ta_c, tb_c);
    std::FILE *ft = std::fopen(total_out.c_str(), "w");
    std::fprintf(ft, "alcance,NA,NB,L,distintas_A,distintas_B\n");
    std::fprintf(ft, "todos,%llu,%llu,%llu,%zu,%zu\n", (unsigned long long)NA_tot, (unsigned long long)NB_tot,
                 (unsigned long long)L_all, ta.size(), tb.size());
    std::fprintf(ft, "cerrados,%llu,%llu,%llu,%zu,%zu\n", (unsigned long long)NA_c, (unsigned long long)NB_c,
                 (unsigned long long)L_c, ta_c.size(), tb_c.size());
    std::fclose(ft);
    std::cerr << "L(todos)=" << L_all << " L(cerrados)=" << L_c << "\n";

    if (ntop <= 0 || top_out.empty()) return 0;
    // Top IPs por aporte |a_i - b_i| a L (traza completa, todos los paquetes).
    std::vector<std::pair<uint64_t, uint32_t>> ap;
    for (auto &kv : ta) {
        auto it = tb.find(kv.first); uint64_t v = it == tb.end() ? 0 : it->second;
        ap.push_back({kv.second > v ? kv.second - v : v - kv.second, kv.first});
    }
    for (auto &kv : tb) if (!ta.count(kv.first)) ap.push_back({kv.second, kv.first});
    std::partial_sort(ap.begin(), ap.begin() + std::min<size_t>(ntop, ap.size()), ap.end(),
                      [](auto &x, auto &y) { return x.first > y.first; });
    ap.resize(std::min<size_t>(ntop, ap.size()));

    std::unordered_map<uint32_t, Carac> ca[2];
    for (auto &p : ap) { ca[0][p.second]; ca[1][p.second]; }
    // Mezcla global de protocolos de cada traza (contexto para la caracterización).
    uint64_t glob[2][4] = {{0}};
    const std::string rutas[2] = {A, B};
    for (int d = 0; d < 2; ++d) {
        LectorTraza rd(rutas[d]);
        for (const Record *r; (r = rd.next());) {
            int pi = r->proto == 6 ? 0 : r->proto == 17 ? 1 : r->proto == 1 ? 2 : 3;
            glob[d][pi]++;
            auto it = ca[d].find(r->dst);
            if (it == ca[d].end()) continue;
            Carac &c = it->second;
            c.pk++; c.bytes += r->len;
            if (pi == 0) c.tcp++; else if (pi == 1) c.udp++; else if (pi == 2) c.icmp++; else c.otro++;
            if (r->flags & 1) c.syn++;
            c.dport[r->dport]++; c.sport[r->sport]++;
            if (c.srcs.size() < 1000000) c.srcs.insert(r->src);
        }
    }
    std::FILE *fp = std::fopen(top_out.c_str(), "w");
    std::fprintf(fp, "rank,ip_dst,aporte,frac_L,dia,paquetes,bytes_medios,tcp,udp,icmp,otro,syn,"
                     "src_distintos,top_dport,top_sport\n");
    const char *dn[2] = {"A", "B"};
    for (size_t i = 0; i < ap.size(); ++i)
        for (int d = 0; d < 2; ++d) {
            Carac &c = ca[d][ap[i].second];
            double tp = c.pk ? (double)c.pk : 1.0;
            std::fprintf(fp, "%zu,%s,%llu,%.6f,%s,%llu,%.1f,%.4f,%.4f,%.4f,%.4f,%.4f,%zu,%s,%s\n", i + 1,
                         ip_str(ap[i].second).c_str(), (unsigned long long)ap[i].first,
                         (double)ap[i].first / L_all, dn[d], (unsigned long long)c.pk,
                         c.pk ? (double)c.bytes / c.pk : 0.0, c.tcp / tp, c.udp / tp, c.icmp / tp,
                         c.otro / tp, c.syn / tp, c.srcs.size(),
                         c.pk ? top_puertos(c.dport, c.pk, 3).c_str() : "",
                         c.pk ? top_puertos(c.sport, c.pk, 3).c_str() : "");
        }
    std::fclose(fp);
    std::FILE *fg = std::fopen((top_out + ".protocolos.csv").c_str(), "w");
    std::fprintf(fg, "dia,tcp,udp,icmp,otro\n");
    for (int d = 0; d < 2; ++d)
        std::fprintf(fg, "%s,%llu,%llu,%llu,%llu\n", dn[d], (unsigned long long)glob[d][0],
                     (unsigned long long)glob[d][1], (unsigned long long)glob[d][2], (unsigned long long)glob[d][3]);
    std::fclose(fg);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { std::cerr << "uso: exact_intervals consec|cross ...\n"; return 2; }
    std::string modo = argv[1], trace, a, b, out, resumen, total, top_out;
    std::vector<double> Ts = {10, 30, 60, 120};
    int ntop = 0;
    for (int i = 2; i < argc; ++i) {
        std::string s = argv[i];
        if (i + 1 >= argc) { std::cerr << "falta valor para " << s << "\n"; return 2; }
        std::string v = argv[++i];
        if (s == "--trace") trace = v;
        else if (s == "--a") a = v;
        else if (s == "--b") b = v;
        else if (s == "--T") Ts = parse_lista(v);
        else if (s == "--out") out = v;
        else if (s == "--resumen") resumen = v;
        else if (s == "--total") total = v;
        else if (s == "--top") ntop = std::stoi(v);
        else if (s == "--top-out") top_out = v;
        else { std::cerr << "argumento desconocido: " << s << "\n"; return 2; }
    }
    if (modo == "consec") return modo_consec(trace, Ts, out, resumen);
    if (modo == "cross") return modo_cross(a, b, Ts.size() == 4 ? 60.0 : Ts[0], out, total, ntop, top_out);
    std::cerr << "modo desconocido\n";
    return 2;
}
