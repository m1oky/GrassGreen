#include "FastMath.h"
#include <sstream>

using namespace std;

namespace FastMath {
    int rand(int min_val, int max_val) {
        if (max_val <= min_val) return min_val;
        static std::mt19937_64 rng(std::random_device{}());
        std::uniform_int_distribution<int> dist(min_val, max_val);
        return dist(rng);
    }
}

double FastMath_Calculate(const string& expr) {
    size_t p1 = expr.find('(');
    size_t p2 = expr.rfind(')');
    if (p1 == string::npos || p2 == string::npos || p2 <= p1) {
        try { return stod(expr); } catch (...) { return 0.0; }
    }

    string func = expr.substr(0, p1);
    string args = expr.substr(p1 + 1, p2 - p1 - 1);
    func.erase(0, func.find_first_not_of(" \t"));
    func.erase(func.find_last_not_of(" \t") + 1);

    if (func == "discor") return FastMath::discor(FastMath_Calculate(args));
    if (func == "sin")    return FastMath::sin(FastMath_Calculate(args));
    if (func == "cos")    return FastMath::cos(FastMath_Calculate(args));
    if (func == "abs")    return FastMath::abs(FastMath_Calculate(args));
    if (func == "round")  return FastMath::round(FastMath_Calculate(args));

    size_t c1 = args.find(',');
    if (c1 != string::npos && func != "lerp") {
        double a = FastMath_Calculate(args.substr(0, c1));
        double b = FastMath_Calculate(args.substr(c1 + 1));
        if (func == "min")  return FastMath::min(a, b);
        if (func == "max")  return FastMath::max(a, b);
        if (func == "rand") return FastMath::rand((int)a, (int)b);
    }

    if (func == "lerp" && c1 != string::npos) {
        size_t c2 = args.find(',', c1 + 1);
        if (c2 != string::npos) {
            double s = FastMath_Calculate(args.substr(0, c1));
            double e = FastMath_Calculate(args.substr(c1 + 1, c2 - c1 - 1));
            double t = FastMath_Calculate(args.substr(c2 + 1));
            return FastMath::lerp(s, e, t);
        }
    }
    return 0.0;
}