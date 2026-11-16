// cauchy_intervals.cpp -- Algoritmo 2 del enunciado: cambio de tráfico entre
// intervalos consecutivos, estimado con el sketch de Cauchy + caché.
//
// Uso:
//   cauchy_intervals --trace traza.bin [--T 10,30,60,120] [--k 512] [--seed 1]
//                    [--B 4096] [--kprefix 32,128,512] [--out est.csv]
//                    [--resumen resumen.csv] [--save-sketches prefijo]
//                    [--max-intervals N] [--preload]
//
// Clave: IP de destino. Se procesan todos los T en una sola pasada; cada T tiene
// sus propios sketches A (actual), P (anterior) y su propia caché.
// Como Entrada no depende de k, un sketch de k filas contiene los de k' < k
// (prefijo); --kprefix indica qué k' se estiman a partir de la misma corrida.
//
// Salidas:
//   --out:      T,q,N_q,N_q-1,Lhat_k<k'>... (una fila por comparación emitida)
//   --resumen:  T,seed,k,B,paquetes,D_s,Q,comparaciones,llamadas,phi,
//               t_proceso_s,throughput_pps,mem_sketch_bytes,mem_cache_bytes
//   --save-sketches P: archivo P_T<T>.sk con el sketch de cada intervalo
//               (incluido el último, abierto, tras Vaciar) para la Actividad 2.2.
#include "cauchy.hpp"
#include "traza.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>

using namespace cauchy;

struct EstadoT {
    double T_s;
    uint64_t T_us;
    Sketch A, P;
    Cache K;
    uint64_t NA = 0, NP = 0, q = 1, comparaciones = 0;
    std::FILE *fsk = nullptr;
    uint32_t n_guardados = 0;
    EstadoT(double T, int k, uint64_t sigma, uint32_t B)
        : T_s(T), T_us((uint64_t)std::llround(T * 1e6)), A(k, sigma), P(k, sigma), K(B) {}
};

static void guardar_sketch(EstadoT &e, const Sketch &S, uint64_t q, uint64_t N, uint8_t cerrado) {
    if (!e.fsk) return;
    uint32_t qq = (uint32_t)q;
    std::fwrite(&qq, 4, 1, e.fsk);
    std::fwrite(&cerrado, 1, 1, e.fsk);
    std::fwrite(&N, 8, 1, e.fsk);
    std::fwrite(S.y.data(), sizeof(double), S.k, e.fsk);
    e.n_guardados++;
}

int main(int argc, char **argv) {
    std::string trace, out, resumen, save_prefix;
    std::vector<double> Ts = {10, 30, 60, 120};
    std::vector<double> kpref;
    int k = 512;
    uint64_t sigma = 1;
    long B = 4096;
    uint64_t max_int = UINT64_MAX;
    bool preload = false;

    for (int a = 1; a < argc; ++a) {
        std::string s = argv[a];
        auto val = [&]() -> std::string {
            if (a + 1 >= argc) { std::cerr << "falta valor para " << s << "\n"; std::exit(2); }
            return argv[++a];
        };
        if (s == "--trace") trace = val();
        else if (s == "--T") Ts = parse_lista(val());
        else if (s == "--k") k = std::stoi(val());
        else if (s == "--seed") sigma = std::stoull(val());
        else if (s == "--B") B = std::stol(val());
        else if (s == "--kprefix") kpref = parse_lista(val());
        else if (s == "--out") out = val();
        else if (s == "--resumen") resumen = val();
        else if (s == "--save-sketches") save_prefix = val();
        else if (s == "--max-intervals") max_int = std::stoull(val());
        else if (s == "--preload") preload = true;
        else { std::cerr << "argumento desconocido: " << s << "\n"; return 2; }
    }
    if (trace.empty()) { std::cerr << "uso: cauchy_intervals --trace traza.bin [...]\n"; return 2; }
    if (kpref.empty()) kpref = {(double)k};
    for (double kp : kpref)
        if (kp < 1 || kp > k) { std::cerr << "kprefix fuera de rango\n"; return 2; }

    std::vector<std::unique_ptr<EstadoT>> E;
    for (double T : Ts) {
        E.emplace_back(new EstadoT(T, k, sigma, (uint32_t)B));
        if (!save_prefix.empty()) {
            char nom[64]; std::snprintf(nom, sizeof nom, "_T%g.sk", T);
            auto &e = *E.back();
            e.fsk = std::fopen((save_prefix + nom).c_str(), "wb");
            if (!e.fsk) { std::cerr << "no se pudo crear sketches\n"; return 1; }
            const char magic[4] = {'C', 'S', 'K', '1'};
            std::fwrite(magic, 1, 4, e.fsk);
            uint32_t kk = (uint32_t)k; std::fwrite(&kk, 4, 1, e.fsk);
            std::fwrite(&sigma, 8, 1, e.fsk);
            std::fwrite(&T, 8, 1, e.fsk);
        }
    }

    std::FILE *fo = out.empty() ? nullptr : std::fopen(out.c_str(), "w");
    if (fo) {
        std::fprintf(fo, "T,q,N_q,N_qm1");
        for (double kp : kpref) std::fprintf(fo, ",Lhat_k%d", (int)kp);
        std::fprintf(fo, "\n");
    }

    // Carga opcional en memoria, para medir throughput sin incluir la lectura de disco.
    std::vector<Record> mem;
    LectorTraza rd(trace);
    if (preload) {
        for (const Record *r; (r = rd.next());) mem.push_back(*r);
        std::cerr << "precargados " << mem.size() << " registros\n";
    }
    size_t mpos = 0;
    auto siguiente = [&]() -> const Record * {
        if (preload) return mpos < mem.size() ? &mem[mpos++] : nullptr;
        return rd.next();
    };

    auto t_ini = std::chrono::steady_clock::now();
    uint64_t t0 = 0, t_ult = 0, paquetes = 0;
    bool primero = true, detener = false;

    for (const Record *r; !detener && (r = siguiente());) {
        uint64_t t = r->ts_us;
        if (primero) { t0 = t; primero = false; }
        t_ult = t;
        for (auto &ep : E) {
            EstadoT &e = *ep;
            while (t > t0 + e.q * e.T_us) {          // se cierra el intervalo q
                e.K.vaciar(e.A);                      // antes de estimar
                if (e.q >= 2) {
                    if (fo) {
                        std::fprintf(fo, "%g,%llu,%llu,%llu", e.T_s, (unsigned long long)e.q,
                                     (unsigned long long)e.NA, (unsigned long long)e.NP);
                        for (double kp : kpref)
                            std::fprintf(fo, ",%.6f", estimar_distancia(e.A, e.P, (int)kp));
                        std::fprintf(fo, "\n");
                    }
                    e.comparaciones++;
                }
                guardar_sketch(e, e.A, e.q, e.NA, 1);
                std::swap(e.A, e.P); e.NP = e.NA;
                e.A.reiniciar(); e.NA = 0; e.q++;
                if (e.q > max_int) detener = true;
            }
            if (detener) break;
            e.K.registrar(e.A, r->dst);
            e.NA++;
        }
        if (!detener) paquetes++;
    }
    // El último intervalo no se compara (Algoritmo 2). Si se guardan sketches, se vacía
    // la caché y se guarda también, para poder sumar la traza completa (Act. 2.2).
    for (auto &ep : E) {
        EstadoT &e = *ep;
        if (e.fsk && !detener) { e.K.vaciar(e.A); guardar_sketch(e, e.A, e.q, e.NA, 0); }
    }
    double t_proc = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_ini).count();

    double D = (t_ult - t0) / 1e6;
    if (!resumen.empty()) {
        bool nuevo = true;
        if (std::FILE *ft = std::fopen(resumen.c_str(), "r")) { nuevo = false; std::fclose(ft); }
        std::FILE *fr = std::fopen(resumen.c_str(), "a");
        if (nuevo)
            std::fprintf(fr, "T,seed,k,B,paquetes,D_s,Q,comparaciones,llamadas,phi,t_proceso_s,"
                             "throughput_pps,mem_sketch_bytes,mem_cache_bytes,preload,max_intervals\n");
        for (auto &ep : E) {
            EstadoT &e = *ep;
            // llamadas totales a Actualizar en A y P (se intercambian, así que se suman)
            uint64_t ll = e.A.llamadas + e.P.llamadas;
            long long Q = (long long)std::ceil(D / e.T_s - 1e-12);
            std::fprintf(fr, "%g,%llu,%d,%ld,%llu,%.6f,%lld,%llu,%llu,%.6f,%.3f,%.0f,%zu,%zu,%d,%lld\n",
                         e.T_s, (unsigned long long)sigma, k, B, (unsigned long long)paquetes, D, Q,
                         (unsigned long long)e.comparaciones, (unsigned long long)ll,
                         paquetes ? (double)ll / paquetes : 0.0, t_proc,
                         t_proc > 0 ? paquetes / t_proc : 0.0,
                         (size_t)2 * k * sizeof(double), e.K.bytes(), (int)preload,
                         max_int == UINT64_MAX ? -1LL : (long long)max_int);
        }
        std::fclose(fr);
    }
    for (auto &ep : E) if (ep->fsk) std::fclose(ep->fsk);
    if (fo) std::fclose(fo);
    std::cerr << "paquetes=" << paquetes << " D=" << D << "s t_proceso=" << t_proc << "s\n";
    return 0;
}
