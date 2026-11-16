// traza.hpp -- lectura secuencial de trazas binarias de 24 bytes (formato pcap2bin).
#pragma once
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#pragma pack(push, 1)
struct Record {
    uint64_t ts_us;
    uint32_t src, dst;
    uint16_t sport, dport, len;
    uint8_t proto, flags;
};
#pragma pack(pop)
static_assert(sizeof(Record) == 24, "registro de 24 bytes");

// Lector por bloques: for (const Record* r; (r = rd.next());) {...}
class LectorTraza {
public:
    explicit LectorTraza(const std::string &path, size_t bloque = 1 << 18) : buf_(bloque) {
        f_ = std::fopen(path.c_str(), "rb");
        if (!f_) throw std::runtime_error("no se pudo abrir " + path);
        std::setvbuf(f_, nullptr, _IOFBF, 1 << 22);
    }
    ~LectorTraza() { if (f_) std::fclose(f_); }
    LectorTraza(const LectorTraza &) = delete;

    inline const Record *next() {
        if (pos_ == n_) {
            n_ = std::fread(buf_.data(), sizeof(Record), buf_.size(), f_);
            pos_ = 0;
            if (n_ == 0) return nullptr;
        }
        return &buf_[pos_++];
    }

private:
    std::FILE *f_ = nullptr;
    std::vector<Record> buf_;
    size_t n_ = 0, pos_ = 0;
};

// Utilidades de línea de comandos.
static inline std::vector<double> parse_lista(const std::string &s) {
    std::vector<double> v;
    size_t a = 0;
    while (a <= s.size()) {
        size_t b = s.find(',', a);
        if (b == std::string::npos) b = s.size();
        if (b > a) v.push_back(std::stod(s.substr(a, b - a)));
        a = b + 1;
    }
    return v;
}

static inline std::string ip_str(uint32_t ip) {
    char b[20];
    std::snprintf(b, sizeof b, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
    return b;
}
