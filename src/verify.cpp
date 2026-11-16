// verify.cpp -- Actividad 1.2: verificación de los pesos, el sketch y la caché.
//
// Uso: verify --outdir resultados/verificacion [--trace traza.bin] [--B 4096] [--T 10]
//
// Pruebas:
//  0. Valores de referencia del enunciado (Mezclar(0), Entrada(42, j, 192.168.0.1)).
//  1. mediana(|C_ji|) ~ 1 sobre >= 10^6 entradas (y cuantiles vs la Cauchy teórica).
//  2. Determinismo: la misma (sigma, j, i) entrega siempre el mismo valor.
//  3. Una sola IP con cuenta c contra un sketch vacío: Lhat ~ c.
//  4. Caché: con B=4096, Lhat entre los dos primeros intervalos de T=10 s coincide
//     con el obtenido llamando Actualizar por cada paquete (salvo redondeo).
//     También una prueba sintética con B pequeño y muchas expulsiones.
#include "cauchy.hpp"
#include "traza.hpp"

#include <cinttypes>
#include <cmath>
#include <cstring>
#include <iostream>
#include <random>

using namespace cauchy;

static int fallas = 0;
static void chequear(bool ok, const char *msg) {
    std::printf("  [%s] %s\n", ok ? "OK" : "FALLA", msg);
    if (!ok) fallas++;
}

int main(int argc, char **argv) {
    std::string outdir = "resultados/verificacion", trace;
    uint32_t B = 4096;
    double T = 10;
    for (int a = 1; a + 1 < argc; a += 2) {
        std::string s = argv[a], v = argv[a + 1];
        if (s == "--outdir") outdir = v;
        else if (s == "--trace") trace = v;
        else if (s == "--B") B = (uint32_t)std::stoul(v);
        else if (s == "--T") T = std::stod(v);
    }

    // ---------------------------------------------------------------- 0
    std::printf("0. Valores de referencia\n");
    uint64_t m0 = mezclar(0);
    std::printf("  Mezclar(0) = 0x%016" PRIX64 " (esperado 0xE220A8397B1DCDAF)\n", m0);
    chequear(m0 == 0xE220A8397B1DCDAFull, "Mezclar(0)");
    const double ref[3] = {-0.889927650, -2.088573016, -26.926917514};
    for (int j = 1; j <= 3; ++j) {
        double e = entrada(42, j, 0xC0A80001u);
        std::printf("  Entrada(42,%d,192.168.0.1) = %.9f (esperado %.9f)\n", j, e, ref[j - 1]);
        chequear(std::fabs(e - ref[j - 1]) < 1e-8, "valor de referencia");
    }
    // la versión optimizada del Sketch (semillas de fila precalculadas) coincide
    {
        Sketch S(3, 42);
        S.actualizar(0xC0A80001u, 1.0);
        bool ok = true;
        for (int j = 0; j < 3; ++j) ok &= S.y[j] == entrada(42, j + 1, 0xC0A80001u);
        chequear(ok, "Sketch::actualizar coincide con Entrada()");
    }

    // ---------------------------------------------------------------- 1
    std::printf("1. Distribución de los pesos\n");
    {
        std::mt19937_64 rng(12345);
        const int filas = 1000, ips = 2000;          // 2.000.000 entradas
        std::vector<double> v;
        v.reserve((size_t)filas * ips);
        std::vector<uint32_t> lista(ips);
        for (auto &x : lista) x = (uint32_t)rng();
        for (int j = 1; j <= filas; ++j)
            for (uint32_t i : lista) v.push_back(std::fabs(entrada(42, j, i)));
        // también IPs consecutivas (difieren en pocos bits)
        for (int j = 1; j <= 100; ++j)
            for (uint32_t i = 0xC0A80000u; i < 0xC0A80000u + 5000; ++i) v.push_back(std::fabs(entrada(7, j, i)));
        std::FILE *fq = std::fopen((outdir + "/pesos_cuantiles.csv").c_str(), "w");
        std::fprintf(fq, "p,empirico,teorico\n");
        std::vector<double> w = v;
        std::nth_element(w.begin(), w.begin() + w.size() / 2, w.end());
        double med = w[w.size() / 2];
        std::printf("  N = %zu entradas, mediana(|C|) = %.5f\n", v.size(), med);
        chequear(std::fabs(med - 1.0) < 0.01, "mediana(|C_ji|) ~ 1 (tolerancia 0.01)");
        std::sort(w.begin(), w.end());
        for (double p : {0.05, 0.1, 0.25, 0.5, 0.75, 0.9, 0.95, 0.99, 0.999}) {
            double emp = w[(size_t)(p * (w.size() - 1))];
            double teo = std::tan(PI * p / 2);    // cuantil de |X|: P(|X|<=t) = (2/pi) atan t
            std::fprintf(fq, "%g,%.6f,%.6f\n", p, emp, teo);
            std::printf("  cuantil %.3f de |C|: empírico %.4f, teórico %.4f\n", p, emp, teo);
        }
        std::fclose(fq);
        // muestra para histograma
        std::FILE *fm = std::fopen((outdir + "/pesos_muestra.csv").c_str(), "w");
        std::fprintf(fm, "c\n");
        for (int j = 1; j <= 50; ++j)
            for (int t = 0; t < 2000; ++t) std::fprintf(fm, "%.6f\n", entrada(42, j, lista[t]));
        std::fclose(fm);
    }

    // ---------------------------------------------------------------- 2
    std::printf("2. Determinismo\n");
    {
        std::mt19937_64 rng(999);
        struct T3 { uint64_t s, j, i; double v; };
        std::vector<T3> xs(100000);
        for (auto &x : xs) { x.s = rng(); x.j = 1 + rng() % 512; x.i = (uint32_t)rng(); x.v = entrada(x.s, x.j, x.i); }
        bool ok = true;
        for (size_t t = xs.size(); t-- > 0;) {      // orden inverso, otras llamadas intercaladas
            entrada(rng(), 1, 1);
            double v2 = entrada(xs[t].s, xs[t].j, xs[t].i);
            ok &= std::memcmp(&xs[t].v, &v2, sizeof v2) == 0;   // igualdad bit a bit
        }
        chequear(ok, "10^5 triples (sigma,j,i) re-evaluados en otro orden dan el mismo valor");
        Sketch a(512, 5), b(512, 5);
        for (uint32_t i = 0; i < 1000; ++i) { a.actualizar(i * 2654435761u, 1); }
        for (uint32_t i = 1000; i-- > 0;) { b.actualizar(i * 2654435761u, 1); }
        double maxd = 0;
        for (int j = 0; j < 512; ++j) maxd = std::max(maxd, std::fabs(a.y[j] - b.y[j]) / std::max(1.0, std::fabs(a.y[j])));
        std::printf("  sketch con 1000 IPs en orden directo vs inverso: max dif. relativa %.3e\n", maxd);
        chequear(maxd < 1e-9, "el orden de llegada no importa (salvo redondeo)");
    }

    // ---------------------------------------------------------------- 3
    std::printf("3. Una IP con cuenta c contra sketch vacío\n");
    {
        std::FILE *fc = std::fopen((outdir + "/una_ip.csv").c_str(), "w");
        std::fprintf(fc, "k,c,seed,ip,Lhat,ratio\n");
        std::mt19937_64 rng(7);
        for (int k : {32, 128, 512}) {
            double suma = 0, suma2 = 0; int n = 0;
            for (double c : {1.0, 10.0, 1000.0, 1e6})
                for (uint64_t s = 1; s <= 50; ++s) {
                    uint32_t ip = (uint32_t)rng();
                    Sketch S(k, s), V(k, s);
                    S.actualizar(ip, c);
                    double L = estimar_distancia(S, V);
                    std::fprintf(fc, "%d,%g,%llu,%u,%.6f,%.6f\n", k, c, (unsigned long long)s, ip, L, L / c);
                    suma += L / c; suma2 += (L / c) * (L / c); n++;
                }
            double m = suma / n, sd = std::sqrt(suma2 / n - m * m);
            std::printf("  k=%3d: media(Lhat/c) = %.4f, desv = %.4f (teórica pi/(2 sqrt k) = %.4f)\n", k, m, sd,
                        PI / (2 * std::sqrt((double)k)));
            chequear(std::fabs(m - 1) < 3 * PI / (2 * std::sqrt((double)k)) / std::sqrt((double)n) + 0.02,
                     "Lhat ~ c en promedio");
        }
        std::fclose(fc);
    }

    // ---------------------------------------------------------------- 4
    std::printf("4. Caché\n");
    std::FILE *fk = std::fopen((outdir + "/cache.csv").c_str(), "w");
    std::fprintf(fk, "prueba,B,k,seed,Lhat_cache,Lhat_directo,dif_rel,max_dif_rel_y,paquetes,llamadas,phi\n");
    {   // sintética: stream Zipf-like con B=7 (muchas expulsiones)
        std::mt19937_64 rng(3);
        Sketch Sc(128, 9), Sd(128, 9), V(128, 9);
        Cache K(7);
        uint64_t n = 200000;
        for (uint64_t t = 0; t < n; ++t) {
            uint32_t ip = (uint32_t)(std::pow(rng() % 1000000 + 1, 0.5));
            K.registrar(Sc, ip);
            Sd.actualizar(ip, 1);
        }
        K.vaciar(Sc);
        double maxd = 0;
        for (int j = 0; j < 128; ++j) maxd = std::max(maxd, std::fabs(Sc.y[j] - Sd.y[j]) / std::max(1.0, std::fabs(Sd.y[j])));
        double Lc = estimar_distancia(Sc, V), Ld = estimar_distancia(Sd, V);
        std::fprintf(fk, "sintetica,7,128,9,%.6f,%.6f,%.3e,%.3e,%llu,%llu,%.4f\n", Lc, Ld, std::fabs(Lc - Ld) / Ld, maxd,
                     (unsigned long long)n, (unsigned long long)Sc.llamadas, (double)Sc.llamadas / n);
        std::printf("  sintética B=7: Lhat %.3f vs %.3f, max dif rel en y %.2e, phi=%.3f\n", Lc, Ld, maxd,
                    (double)Sc.llamadas / n);
        chequear(maxd < 1e-9, "caché sintética exacta salvo redondeo");
    }
    if (!trace.empty()) {
        const int k = 512;
        const uint64_t sigma = 1;
        uint64_t Tus = (uint64_t)std::llround(T * 1e6);
        Sketch Ac(k, sigma), Pc(k, sigma), Ad(k, sigma), Pd(k, sigma);
        Cache K(B);
        LectorTraza rd(trace);
        uint64_t t0 = 0, q = 1, n = 0, n12 = 0;
        for (const Record *r; (r = rd.next());) {
            if (n++ == 0) t0 = r->ts_us;
            if (r->ts_us > t0 + q * Tus) {
                if (q == 2) break;            // cerramos el intervalo 2: listo
                K.vaciar(Ac);
                std::swap(Ac, Pc); std::swap(Ad, Pd); q++;
            }
            K.registrar(Ac, r->dst);
            Ad.actualizar(r->dst, 1);
            n12++;
        }
        K.vaciar(Ac);
        uint64_t llam = Ac.llamadas + Pc.llamadas;
        for (int kp : {32, 128, 512}) {
            double Lc = estimar_distancia(Ac.y.data(), Pc.y.data(), kp);
            double Ld = estimar_distancia(Ad.y.data(), Pd.y.data(), kp);
            double maxd = 0;
            for (int j = 0; j < kp; ++j) {
                maxd = std::max(maxd, std::fabs(Ac.y[j] - Ad.y[j]) / std::max(1.0, std::fabs(Ad.y[j])));
                maxd = std::max(maxd, std::fabs(Pc.y[j] - Pd.y[j]) / std::max(1.0, std::fabs(Pd.y[j])));
            }
            std::fprintf(fk, "traza_T%g_q1q2,%u,%d,%llu,%.6f,%.6f,%.3e,%.3e,%llu,%llu,%.4f\n", T, B, kp,
                         (unsigned long long)sigma, Lc, Ld, std::fabs(Lc - Ld) / Ld, maxd,
                         (unsigned long long)n12, (unsigned long long)llam, (double)llam / n12);
            std::printf("  traza, T=%g s, intervalos 1-2, k=%d: Lhat cache %.4f vs directo %.4f, dif rel %.2e\n", T, kp,
                        Lc, Ld, std::fabs(Lc - Ld) / Ld);
            chequear(std::fabs(Lc - Ld) / Ld < 1e-9, "Lhat con caché = Lhat sin caché (salvo redondeo)");
        }
        std::printf("  paquetes en los 2 intervalos: %llu, llamadas con caché: %llu (phi=%.4f)\n",
                    (unsigned long long)n12, (unsigned long long)llam, (double)llam / n12);
    }
    std::fclose(fk);
    std::printf("%s (%d fallas)\n", fallas ? "HAY FALLAS" : "TODO OK", fallas);
    return fallas ? 1 : 0;
}
