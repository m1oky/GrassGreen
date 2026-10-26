#ifndef COREMATH_H
#define COREMATH_H

#include <string>

namespace CoreMath {
    std::string qb_init(const std::string& q = "q0");
    std::string h(const std::string& q = "q0");
    std::string x(const std::string& q = "q0");
    std::string cnot(const std::string& control, const std::string& target);
    int q_rand(const std::string& q = "q0");
    std::string pi();
    long long gcd(long long a, long long b);
    std::string pow_e(double b, double e);
}

std::string CoreMath_Calculate(const std::string& expr);

#endif // COREMATH_H