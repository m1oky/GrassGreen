#include "CoreMath.h"
#include <cmath>
#include <random>
#include <sstream>
#include <iomanip>
#include <map>

using namespace std;

static map<string, double> quantum_register;

namespace CoreMath {
    string qb_init(const string& q) {
        quantum_register[q] = 0.0;
        return "Quantum Register: Qubit '" + q + "' initialized to |0>";
    }

    string h(const string& q) {
        if (!quantum_register.count(q)) quantum_register[q] = 0.0;
        quantum_register[q] = 0.5;
        return "Quantum Action: Hadamard gate applied to '" + q + "' (Superposition)";
    }

    string x(const string& q) {
        if (!quantum_register.count(q)) quantum_register[q] = 0.0;
        if (quantum_register[q] == 0.0) quantum_register[q] = 1.0;
        else if (quantum_register[q] == 1.0) quantum_register[q] = 0.0;
        return "Quantum Action: Pauli-X gate applied to '" + q + "'";
    }

    string cnot(const string& control, const string& target) {
        if (!quantum_register.count(control)) quantum_register[control] = 0.0;
        if (!quantum_register.count(target))  quantum_register[target] = 0.0;
        if (q_rand(control) == 1) x(target);
        return "Quantum Action: CNOT entangled '" + control + "' -> '" + target + "'";
    }

    int q_rand(const string& q) {
        if (!quantum_register.count(q)) return 0;
        double state = quantum_register[q];
        int res = 0;
        if (state == 0.0) res = 0;
        else if (state == 1.0) res = 1;
        else {
            static std::mt19937_64 rng(std::random_device{}());
            std::uniform_real_distribution<double> dist(0.0, 1.0);
            res = (dist(rng) < state) ? 1 : 0;
        }
        quantum_register[q] = (double)res;
        return res;
    }

    string pi() {
        return "3.141592653589793238462643383279";
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
    if (func == "q_rand")  return to_string(CoreMath::q_rand(args.empty() ? "q0" : args));
    if (func == "pi")      return CoreMath::pi();

    size_t comma = args.find(',');
    if (comma != string::npos) {
        try {
            if (func == "gcd")   return to_string(CoreMath::gcd(stoll(args.substr(0, comma)), stoll(args.substr(comma + 1))));
            if (func == "pow_e") return CoreMath::pow_e(stod(args.substr(0, comma)), stod(args.substr(comma + 1)));
        } catch (...) { return "Error"; }
    }
    return "CoreMath Error: Unknown token";
}