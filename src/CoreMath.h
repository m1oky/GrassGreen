#ifndef COREMATH_H
#define COREMATH_H

#include <string>
#include <vector>
#include <cstdint>

namespace CoreMath {
    // Cryptography and BigInt arithmetic
    long long gcd(long long a, long long b);
    std::string pow_e(double b, double e);
    uint64_t pow_mod(uint64_t base, uint64_t exp, uint64_t mod);
    std::string sha256(const std::string& input);
    std::vector<uint8_t> crypto_rand(size_t byte_count);
    std::string pi();

    // Quantum circuit simulator
    std::string qb_init(const std::string& q = "q0");
    std::string h(const std::string& q = "q0");
    std::string x(const std::string& q = "q0");
    std::string z(const std::string& q = "q0");
    std::string cnot(const std::string& control, const std::string& target);
    int q_rand(const std::string& q = "q0");
    int q_measure(const std::string& q = "q0");
}

std::string CoreMath_Calculate(const std::string& expr);

#endif // COREMATH_H
