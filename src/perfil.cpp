// perfil.cpp -- Perfil de tráfico de una traza (Actividad 2.2, caracterización del día 30).
//
// Como MAWI anonimiza las IPs con una clave distinta por traza (Crypto-PAn, "consistent
// only within a single trace"), las IPs de días distintos no se pueden emparejar. Para
// explicar qué cambió se comparan PERFILES: mezcla de protocolos, puertos, número de
// destinos distintos y destinos que reciben un solo paquete (firma de escaneo), y los
// orígenes que más destinos de un solo paquete generan.
//
// Uso: perfil --trace X.bin --out perfil.csv [--top 15]
// Escribe filas (seccion,clave,valor1,valor2,...) en un CSV largo.
#include "traza.hpp"

#include <algorithm>
#include <iostream>
#include <unordered_map>

using Cuenta = std::unordered_map<uint32_t, uint64_t>;

template <class M>
static std::vector<std::pair<uint64_t, typename M::key_type>> top(const M &m, size_t n) {
    std::vector<std::pair<uint64_t, typename M::key_type>> v;
    v.reserve(m.size());
    for (auto &kv : m) v.push_back({kv.second, kv.first});
    size_t k = std::min(n, v.size());
    std::partial_sort(v.begin(), v.begin() + k, v.end(), [](auto &a, auto &b) { return a.first > b.first; });
    v.resize(k);
    return v;
}

int main(int argc, char **argv) {
    std::string trace, out;
    size_t ntop = 15;
    for (int a = 1; a + 1 < argc; a += 2) {
        std::string s = argv[a], v = argv[a + 1];
        if (s == "--trace") trace = v;
        else if (s == "--out") out = v;
        else if (s == "--top") ntop = std::stoul(v);
    }
    if (trace.empty() || out.empty()) { std::cerr << "uso: perfil --trace X.bin --out p.csv\n"; return 2; }

    // Pasada 1: paquetes por destino y por protocolo/puerto.
    Cuenta dst;
    std::unordered_map<uint32_t, uint64_t> puerto[3];   // clave: dport, para TCP/UDP; ICMP: tipo no disponible
    uint64_t proto[4] = {0}, bytes[4] = {0}, n = 0, syn_sin_ack = 0;
    {
        LectorTraza rd(trace);
        for (const Record *r; (r = rd.next());) {
            n++;
            dst[r->dst]++;
            int p = r->proto == 6 ? 0 : r->proto == 17 ? 1 : r->proto == 1 ? 2 : 3;
            proto[p]++; bytes[p] += r->len;
            if (p < 2) puerto[p][r->dport]++;
            if (p == 0 && (r->flags & 1) && !(r->flags & 2)) syn_sin_ack++;
        }
    }
    uint64_t dst1 = 0;
    for (auto &kv : dst) dst1 += kv.second == 1;

    // Pasada 2: paquetes cuyo destino recibe un solo paquete en toda la traza.
    Cuenta src_de_unicos;
    std::unordered_map<uint32_t, uint64_t> proto_unicos, dport_unicos;
    {
        LectorTraza rd(trace);
        for (const Record *r; (r = rd.next());) {
            if (dst[r->dst] != 1) continue;
            src_de_unicos[r->src]++;
            proto_unicos[r->proto]++;
            dport_unicos[((uint32_t)r->proto << 16) | r->dport]++;
        }
    }

    std::FILE *f = std::fopen(out.c_str(), "w");
    std::fprintf(f, "seccion,clave,valor,frac\n");
    std::fprintf(f, "total,paquetes,%llu,1\n", (unsigned long long)n);
    std::fprintf(f, "total,destinos_distintos,%zu,\n", dst.size());
    std::fprintf(f, "total,destinos_1_paquete,%llu,%.6f\n", (unsigned long long)dst1, (double)dst1 / dst.size());
    std::fprintf(f, "total,syn_sin_ack,%llu,%.6f\n", (unsigned long long)syn_sin_ack, (double)syn_sin_ack / n);
    const char *pn[4] = {"tcp", "udp", "icmp", "otro"};
    for (int p = 0; p < 4; ++p) {
        std::fprintf(f, "proto,%s,%llu,%.6f\n", pn[p], (unsigned long long)proto[p], (double)proto[p] / n);
        std::fprintf(f, "bytes_medios,%s,%.1f,\n", pn[p], proto[p] ? (double)bytes[p] / proto[p] : 0.0);
    }
    for (int p = 0; p < 2; ++p)
        for (auto &t : top(puerto[p], ntop))
            std::fprintf(f, "dport_%s,%u,%llu,%.6f\n", pn[p], t.second, (unsigned long long)t.first,
                         (double)t.first / proto[p]);
    for (auto &t : top(proto_unicos, 5))
        std::fprintf(f, "unicos_proto,%u,%llu,%.6f\n", t.second, (unsigned long long)t.first, (double)t.first / dst1);
    for (auto &t : top(dport_unicos, ntop))
        std::fprintf(f, "unicos_dport,%u/%u,%llu,%.6f\n", t.second >> 16, t.second & 0xffff,
                     (unsigned long long)t.first, (double)t.first / dst1);
    for (auto &t : top(src_de_unicos, ntop))
        std::fprintf(f, "unicos_src,%s,%llu,%.6f\n", ip_str(t.second).c_str(), (unsigned long long)t.first,
                     (double)t.first / dst1);
    std::fclose(f);
    std::cerr << trace << ": " << n << " paquetes, " << dst.size() << " destinos, " << dst1 << " con 1 paquete\n";
    return 0;
}
