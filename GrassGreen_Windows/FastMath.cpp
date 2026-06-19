#include "FastMath.h"
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iostream>

using namespace std;

// Быстрый парсер математических команд
double FastMath_Calculate(const string& expr) {
    static bool rand_seeded = false;
    if (!rand_seeded) { srand(time(0)); rand_seeded = true; }

    size_t paren_start = expr.find('(');
    size_t paren_end = expr.find(')');
    
    if (paren_start == string::npos || paren_end == string::npos || paren_end < paren_start) {
        return 0.0;
    }

    string func = expr.substr(0, paren_start);
    string args_str = expr.substr(paren_start + 1, paren_end - paren_start - 1);
    
    func.erase(0, func.find_first_not_of(" \t")); 
    func.erase(func.find_last_not_of(" \t") + 1);

    if (func == "discor" || func == "sin" || func == "cos" || func == "abs" || func == "round") {
        double x = 0.0;
        try { x = stod(args_str); } catch(...) { return 0.0; }

        if (func == "discor") return sqrt(x);
        if (func == "sin")    return sin(x);
        if (func == "cos")    return cos(x);
        if (func == "abs")    return abs(x);
        if (func == "round")  return round(x);
    }

    size_t comma1 = args_str.find(',');
    if (comma1 != string::npos && func != "lerp") {
        double arg1 = 0.0, arg2 = 0.0;
        try {
            arg1 = stod(args_str.substr(0, comma1));
            arg2 = stod(args_str.substr(comma1 + 1));
        } catch(...) { return 0.0; }

        if (func == "min")  return (arg1 < arg2) ? arg1 : arg2;
        if (func == "max")  return (arg1 > arg2) ? arg1 : arg2;
        if (func == "rand") {
            int i_min = (int)arg1;
            int i_max = (int)arg2;
            if (i_max <= i_min) return arg1;
            return i_min + (rand() % (i_max - i_min + 1));
        }
    }

    if (func == "lerp") {
        size_t comma2 = args_str.find(',', comma1 + 1);
        if (comma1 != string::npos && comma2 != string::npos) {
            double start = 0.0, end = 0.0, t = 0.0;
            try {
                start = stod(args_str.substr(0, comma1));
                end = stod(args_str.substr(comma1 + 1, comma2 - comma1 - 1));
                t = stod(args_str.substr(comma2 + 1));
            } catch(...) { return 0.0; }
            return start + t * (end - start);
        }
    }

    return 0.0; // Неизвестная функция
}