// cauchy.hpp -- Sketch de Cauchy (Indyk 2000) y caché de tamaño fijo.
//
// Tarea 2, Tópicos en Grandes Volúmenes de Datos 2026.
// Implementa exactamente el Algoritmo 1 del enunciado:
//   Mezclar  = finalizador splitmix64
//   Entrada  = C_ji regenerada desde (sigma, j, i), nunca almacenada
//   Actualizar / EstimarDistancia (mediana de |ya - yb|)
// y la caché de mapeo directo descrita en Docs/03_cache.html.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace cauchy {

// ------------------------------------------------------------ Mezclar (splitmix64)
static inline uint64_t mezclar(uint64_t z) {
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static const double PI = 3.14159265358979323846;
static const double TWO_M52 = 1.0 / 4503599627370496.0;  // 2^-52

// u en (0,1) desde los 52 bits altos de h, y Cauchy por transformada inversa.
static inline double cauchy_desde_hash(uint64_t h) {
    double u = ((double)(h >> 12) + 0.5) * TWO_M52;
    return std::tan(PI * (u - 0.5));
}

// Semilla de la fila j (1-indexada, como en el pseudocódigo).
static inline uint64_t semilla_fila(uint64_t sigma, uint64_t j) {
    uint64_t s = mezclar(sigma);
    return mezclar(s ^ j);
}

// Entrada(sigma, j, i) completa (versión de referencia, sin precálculo).
static inline double entrada(uint64_t sigma, uint64_t j, uint64_t i) {
    return cauchy_desde_hash(mezclar(semilla_fila(sigma, j) ^ i));
}

// ------------------------------------------------------------ Sketch S = (y, k, sigma)
struct Sketch {
    int k;
    uint64_t sigma;
    std::vector<double> y;     // k doubles: el sketch propiamente tal
    std::vector<uint64_t> g;   // semillas de fila g_j precalculadas (derivables de sigma)
    uint64_t llamadas = 0;     // número de llamadas a Actualizar (para phi_B)

    Sketch(int k_, uint64_t sigma_) : k(k_), sigma(sigma_), y(k_, 0.0), g(k_) {
        for (int j = 0; j < k; ++j) g[j] = semilla_fila(sigma, (uint64_t)(j + 1));
    }
    void reiniciar() { std::fill(y.begin(), y.end(), 0.0); }

    // Actualizar(S, i, c): y += c * C_{.,i}
    inline void actualizar(uint32_t i, double c) {
        ++llamadas;
        const uint64_t ii = i;
        for (int j = 0; j < k; ++j) y[j] += c * cauchy_desde_hash(mezclar(g[j] ^ ii));
    }
};

// EstimarDistancia usando sólo las primeras kp filas (prefijo válido, ver enunciado).
static inline double estimar_distancia(const double *ya, const double *yb, int kp) {
    std::vector<double> d(kp);
    for (int j = 0; j < kp; ++j) d[j] = std::fabs(ya[j] - yb[j]);
    // mediana: con kp par se promedian los dos centrales.
    std::nth_element(d.begin(), d.begin() + kp / 2, d.end());
    double hi = d[kp / 2];
    if (kp % 2) return hi;
    double lo = *std::max_element(d.begin(), d.begin() + kp / 2);
    return 0.5 * (lo + hi);
}

static inline double estimar_distancia(const Sketch &a, const Sketch &b, int kp = -1) {
    if (a.k != b.k || a.sigma != b.sigma)
        throw std::runtime_error("EstimarDistancia: sketches con distinto k o sigma");
    if (kp < 0) kp = a.k;
    return estimar_distancia(a.y.data(), b.y.data(), kp);
}

// ------------------------------------------------------------ Caché de mapeo directo
//
// B ranuras (ip, cuenta). La ranura de la IP i es h(i) = floor(hash32(i) * B / 2^32).
//   Registrar(S,K,i):
//     r <- h(i)
//     si K[r].ip == i y K[r].c > 0: K[r].c += 1                       (acierto)
//     si no: si K[r].c > 0: Actualizar(S, K[r].ip, K[r].c)            (expulsión)
//            K[r] <- (i, 1)
//   Vaciar(S,K): para cada r con K[r].c > 0: Actualizar(S, K[r].ip, K[r].c); K[r].c <- 0
// Invariante: S.y = C * (x_registrado - x_cache), donde x_cache[i] = cuenta de i en K.
// B = 0 significa "sin caché": Registrar llama Actualizar(S, i, 1) directamente.
struct Cache {
    uint32_t B;
    std::vector<uint32_t> ip;
    std::vector<uint32_t> c;   // 0 = ranura vacía

    explicit Cache(uint32_t B_) : B(B_), ip(B_, 0), c(B_, 0) {}

    static inline uint32_t hash32(uint32_t x) {   // mezcla de 32 bits (murmur3 fmix32)
        x ^= x >> 16; x *= 0x85ebca6bu; x ^= x >> 13; x *= 0xc2b2ae35u; x ^= x >> 16;
        return x;
    }
    inline uint32_t ranura(uint32_t i) const { return (uint32_t)(((uint64_t)hash32(i) * B) >> 32); }

    inline void registrar(Sketch &S, uint32_t i) {
        if (B == 0) { S.actualizar(i, 1.0); return; }
        uint32_t r = ranura(i);
        if (c[r] > 0 && ip[r] == i) {
            if (++c[r] == UINT32_MAX) { S.actualizar(i, (double)c[r]); c[r] = 0; } // desborde: descarga
            return;
        }
        if (c[r] > 0) S.actualizar(ip[r], (double)c[r]);
        ip[r] = i; c[r] = 1;
    }
    void vaciar(Sketch &S) {
        for (uint32_t r = 0; r < B; ++r)
            if (c[r] > 0) { S.actualizar(ip[r], (double)c[r]); c[r] = 0; }
    }
    size_t bytes() const { return (size_t)B * (sizeof(uint32_t) * 2); }
};

}  // namespace cauchy
