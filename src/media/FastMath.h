#ifndef FASTMATH_H
#define FASTMATH_H

#include <string>
#include <cmath>
#include <random>

namespace FastMath {
    inline double discor(double x) { return (x >= 0.0) ? std::sqrt(x) : 0.0; }
    inline double sin(double x)    { return std::sin(x); }
    inline double cos(double x)    { return std::cos(x); }
    inline double abs(double x)    { return std::fabs(x); }
    inline double round(double x)  { return std::round(x); }
    inline double min(double a, double b) { return (a < b) ? a : b; }
    inline double max(double a, double b) { return (a > b) ? a : b; }
    inline double lerp(double a, double b, double t) { return a + t * (b - a); }
    int rand(int min_val, int max_val);
}

double FastMath_Calculate(const std::string& expr);

#endif // FASTMATH_H