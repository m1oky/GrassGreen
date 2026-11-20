#include "CoreMath.h"
#include <cmath>
#include <random>
#include <sstream>
#include <iomanip>
#include <map>
#include <vector>
#include <algorithm>

#if defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

using namespace std;

// Quantum qubit state: alpha (|0>) and beta (|1>)
struct QubitState {
    double r0, i0; // alpha: real, imag
    double r1, i1; // beta: real, imag
    QubitState() : r0(1.0), i0(0.0), r1(0.0), i1(0.0) {}
};

static map<string, QubitState> g_qubits;

namespace CoreMath {

static bool is_prime(int n) {
    if (n < 2) return false;
    for (int i = 2; i * i <= n; i++) {
        if (n % i == 0) return false;
    }
    return true;
}

static void compute_sha256_constants(uint32_t K[64], uint32_t H[8]) {
    int found_p = 0;
    int num = 2;
    vector<int> primes;
    while (found_p < 64) {
        if (is_prime(num)) {
            primes.push_back(num);
            found_p++;
        }
        num++;
    }

    // First 8 primes square roots for H
    for (int i = 0; i < 8; i++) {
        double root = std::sqrt((double)primes[i]);
        double frac = root - std::floor(root);
        H[i] = (uint32_t)(frac * 4294967296.0);
    }

    // First 64 primes cube roots for K
    for (int i = 0; i < 64; i++) {
        double cbrt = std::cbrt((double)primes[i]);
        double frac = cbrt - std::floor(cbrt);
        K[i] = (uint32_t)(frac * 4294967296.0);
    }
}

static inline uint32_t rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

string sha256(const string& input) {
    static uint32_t K[64];
    static uint32_t H_init[8];
    static bool initialized = false;
    if (!initialized) {
        compute_sha256_constants(K, H_init);
        initialized = true;
    }

    uint32_t H[8];
    for (int i = 0; i < 8; i++) H[i] = H_init[i];

    // Padding
    vector<uint8_t> msg(input.begin(), input.end());
    uint64_t bit_len = (uint64_t)msg.size() * 8;
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) {
        msg.push_back(0x00);
    }
    for (int i = 7; i >= 0; i--) {
        msg.push_back((uint8_t)((bit_len >> (i * 8)) & 0xFF));
    }

    // Process 64-byte chunks
    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t W[64];
        for (int t = 0; t < 16; t++) {
            W[t] = ((uint32_t)msg[chunk + t * 4] << 24) |
                   ((uint32_t)msg[chunk + t * 4 + 1] << 16) |
                   ((uint32_t)msg[chunk + t * 4 + 2] << 8) |
                   ((uint32_t)msg[chunk + t * 4 + 3]);
        }
        for (int t = 16; t < 64; t++) {
            uint32_t s0 = rotr(W[t - 15], 7) ^ rotr(W[t - 15], 18) ^ (W[t - 15] >> 3);
            uint32_t s1 = rotr(W[t - 2], 17) ^ rotr(W[t - 2], 19) ^ (W[t - 2] >> 10);
            W[t] = W[t - 16] + s0 + W[t - 7] + s1;
        }

        uint32_t a = H[0], b = H[1], c = H[2], d = H[3];
        uint32_t e = H[4], f = H[5], g = H[6], h_val = H[7];

        for (int t = 0; t < 64; t++) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = h_val + S1 + ch + K[t] + W[t];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;

            h_val = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        H[0] += a; H[1] += b; H[2] += c; H[3] += d;
        H[4] += e; H[5] += f; H[6] += g; H[7] += h_val;
    }

    stringstream ss;
    ss << hex << setfill('0');
    for (int i = 0; i < 8; i++) {
        ss << setw(8) << H[i];
    }
    return ss.str();
}

vector<uint8_t> crypto_rand(size_t byte_count) {
    vector<uint8_t> buf(byte_count, 0);
    if (byte_count == 0) return buf;

#if defined(_WIN32)
    HCRYPTPROV hProv = 0;
    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        CryptGenRandom(hProv, (DWORD)byte_count, buf.data());
        CryptReleaseContext(hProv, 0);
        return buf;
    }
#elif defined(__linux__)
    FILE* f = fopen("/dev/urandom", "rb");
    if (f) {
        fread(buf.data(), 1, byte_count, f);
        fclose(f);
        return buf;
    }
#else // BARE_METAL
    for (size_t i = 0; i < byte_count; i++) {
        uint32_t v = 0;
#if defined(__x86_64__) || defined(_M_X64)
        if (__builtin_ia32_rdrand32_step(&v)) {
            buf[i] = (uint8_t)v;
            continue;
        }
#endif
        uint64_t t = __builtin_ia32_rdtsc();
        buf[i] = (uint8_t)((t ^ (t >> 8) ^ (t >> 16)) & 0xFF);
    }
    return buf;
#endif

    // Fallback pseudo-random
    for (size_t i = 0; i < byte_count; i++) {
        buf[i] = (uint8_t)(rand() % 256);
    }
    return buf;
}

static inline uint64_t mul_mod(uint64_t a, uint64_t b, uint64_t m) {
#if defined(__SIZEOF_INT128__)
    return (uint64_t)(((__int128)a * b) % m);
#else
    uint64_t res = 0;
    a %= m;
    while (b > 0) {
        if (b & 1) res = (res + a) % m;
        a = (a * 2) % m;
        b >>= 1;
    }
    return res;
#endif
}

uint64_t pow_mod(uint64_t base, uint64_t exp, uint64_t mod) {
    if (mod == 0) return 0;
    uint64_t res = 1;
    base %= mod;
    while (exp > 0) {
        if (exp & 1) {
            res = mul_mod(res, base, mod);
        }
        base = mul_mod(base, base, mod);
        exp >>= 1;
    }
    return res;
}

long long gcd(long long a, long long b) {
    a = std::llabs(a); b = std::llabs(b);
    while (b != 0) {
        long long t = b;
        b = a % b;
        a = t;
    }
    return a;
}

string pow_e(double b, double e) {
    double res = std::pow(b, e);
    stringstream ss;
    ss << fixed << setprecision(10) << res;
    string s = ss.str();
    if (s.find('.') != string::npos) {
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    return s;
}

string pi() {
    return "3.1415926535897932384626433832795028841971";
}

// Quantum simulation
string qb_init(const string& q) {
    g_qubits[q] = QubitState(); // |0>
    return "Quantum Register: Qubit '" + q + "' initialized to |0>";
}

string h(const string& q) {
    if (!g_qubits.count(q)) g_qubits[q] = QubitState();
    QubitState& s = g_qubits[q];
    double inv_sqrt2 = 0.7071067811865475;
    double new_r0 = (s.r0 + s.r1) * inv_sqrt2;
    double new_i0 = (s.i0 + s.i1) * inv_sqrt2;
    double new_r1 = (s.r0 - s.r1) * inv_sqrt2;
    double new_i1 = (s.i0 - s.i1) * inv_sqrt2;
    s.r0 = new_r0; s.i0 = new_i0;
    s.r1 = new_r1; s.i1 = new_i1;
    return "Quantum Action: Hadamard gate applied to '" + q + "' (Superposition)";
}

string x(const string& q) {
    if (!g_qubits.count(q)) g_qubits[q] = QubitState();
    QubitState& s = g_qubits[q];
    std::swap(s.r0, s.r1);
    std::swap(s.i0, s.i1);
    return "Quantum Action: Pauli-X gate applied to '" + q + "'";
}

string z(const string& q) {
    if (!g_qubits.count(q)) g_qubits[q] = QubitState();
    QubitState& s = g_qubits[q];
    s.r1 = -s.r1;
    s.i1 = -s.i1;
    return "Quantum Action: Pauli-Z phase gate applied to '" + q + "'";
}

string cnot(const string& control, const string& target) {
    if (!g_qubits.count(control)) g_qubits[control] = QubitState();
    if (!g_qubits.count(target))  g_qubits[target]  = QubitState();
    int m = q_measure(control);
    if (m == 1) {
        x(target);
    }
    return "Quantum Action: CNOT entangled '" + control + "' -> '" + target + "'";
}

int q_measure(const string& q) {
    if (!g_qubits.count(q)) return 0;
    QubitState& s = g_qubits[q];
    double prob1 = s.r1 * s.r1 + s.i1 * s.i1;

    static mt19937_64 rng(1337);
    uniform_real_distribution<double> dist(0.0, 1.0);
    int outcome = (dist(rng) < prob1) ? 1 : 0;

    // Collapse
    if (outcome == 0) {
        s.r0 = 1.0; s.i0 = 0.0;
        s.r1 = 0.0; s.i1 = 0.0;
    } else {
        s.r0 = 0.0; s.i0 = 0.0;
        s.r1 = 1.0; s.i1 = 0.0;
    }
    return outcome;
}

int q_rand(const string& q) {
    return q_measure(q);
}

}

string CoreMath_Calculate(const string& expr) {
    size_t p1 = expr.find('(');
    size_t p2 = expr.rfind(')');
    if (p1 == string::npos || p2 == string::npos || p2 < p1) return "ERROR: Bad Syntax";

    string func = expr.substr(0, p1);
    string args = expr.substr(p1 + 1, p2 - p1 - 1);
    func.erase(0, func.find_first_not_of(" \t")); func.erase(func.find_last_not_of(" \t") + 1);
    args.erase(0, args.find_first_not_of(" \t\"")); args.erase(args.find_last_not_of(" \t\"") + 1);

    if (func == "qb_init") return CoreMath::qb_init(args.empty() ? "q0" : args);
    if (func == "h")       return CoreMath::h(args.empty() ? "q0" : args);
    if (func == "x")       return CoreMath::x(args.empty() ? "q0" : args);
    if (func == "z")       return CoreMath::z(args.empty() ? "q0" : args);
    if (func == "q_rand" || func == "q_measure") return to_string(CoreMath::q_measure(args.empty() ? "q0" : args));
    if (func == "pi")      return CoreMath::pi();
    if (func == "sha256")  return CoreMath::sha256(args);
    if (func == "cnot") {
        size_t comma = args.find(',');
        if (comma != string::npos) {
            string c = args.substr(0, comma);
            string t = args.substr(comma + 1);
            c.erase(0, c.find_first_not_of(" \t\"")); c.erase(c.find_last_not_of(" \t\"") + 1);
            t.erase(0, t.find_first_not_of(" \t\"")); t.erase(t.find_last_not_of(" \t\"") + 1);
            return CoreMath::cnot(c, t);
        }
        return "CNOT requires two arguments";
    }
    if (func == "crypto_rand") {
        size_t cnt = args.empty() ? 32 : (size_t)stoul(args);
        auto bytes = CoreMath::crypto_rand(cnt);
        stringstream ss;
        ss << hex << setfill('0');
        for (auto b : bytes) ss << setw(2) << (int)b;
        return ss.str();
    }
    if (func == "pow_mod") {
        stringstream ss(args);
        string item;
        vector<string> parts;
        while (getline(ss, item, ',')) parts.push_back(item);
        if (parts.size() >= 3) {
            try {
                uint64_t b = stoull(parts[0]);
                uint64_t e = stoull(parts[1]);
                uint64_t m = stoull(parts[2]);
                return to_string(CoreMath::pow_mod(b, e, m));
            } catch (...) { return "Error"; }
        }
    }

    size_t comma = args.find(',');
    if (comma != string::npos) {
        try {
            if (func == "gcd")   return to_string(CoreMath::gcd(stoll(args.substr(0, comma)), stoll(args.substr(comma + 1))));
            if (func == "pow_e") return CoreMath::pow_e(stod(args.substr(0, comma)), stod(args.substr(comma + 1)));
        } catch (...) { return "Error"; }
    }
    return "CoreMath Error: Unknown token";
}
