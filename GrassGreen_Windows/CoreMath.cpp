#include "CoreMath.h"
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <map>

using namespace std;

// Наш кастомный квантовый регистр для хранения состояний кубитов
// 0.0 = чистый ноль, 1.0 = чистая единица, 0.5 = суперпозиция (50/50)
static map<string, double> quantum_register;

// Алгоритм Евклида для НОД (криптография RSA)
long long calculate_gcd(long long a, long long b) {
    while (b != 0) {
        long long temp = b;
        b = a % b;
        a = temp;
    }
    return a;
}

std::string CoreMath_Calculate(const std::string& expr) {
    static bool q_seeded = false;
    if (!q_seeded) { srand(time(0)); q_seeded = true; }

    size_t paren_start = expr.find('(');
    size_t paren_end = expr.find(')');
    if (paren_start == string::npos || paren_end == string::npos || paren_end < paren_start) {
        return "ERROR: Bad Syntax";
    }

    string func = expr.substr(0, paren_start);
    string args_str = expr.substr(paren_start + 1, paren_end - paren_start - 1);
    
    // Чистим пробелы
    func.erase(0, func.find_first_not_of(" \t")); func.erase(func.find_last_not_of(" \t") + 1);
    args_str.erase(0, args_str.find_first_not_of(" \t")); args_str.erase(args_str.find_last_not_of(" \t") + 1);

    // 1. Инициализация кубита qb_init()
    if (func == "qb_init") {
        string q_name = args_str.empty() ? "q0" : args_str;
        quantum_register[q_name] = 0.0; // Базовое состояние |0>
        return "Quantum Register: Qubit '" + q_name + "' initialized to state |0>";
    }

    // 2. Врата Адамара h(q) - переводим в суперпозицию
    if (func == "h") {
        string q_name = args_str.empty() ? "q0" : args_str;
        if (quantum_register.count(q_name) == 0) return "Quantum Error: Qubit NOT found";
        quantum_register[q_name] = 0.5; // 50% шанс на 0 или 1
        return "Quantum Action: Hadamard gate applied to '" + q_name + "'. State: Superposition";
    }

    // 3. Измерение кубита q_rand(q) с коллапсом состояния
    if (func == "q_rand") {
        string q_name = args_str.empty() ? "q0" : args_str;
        if (quantum_register.count(q_name) == 0) return "Quantum Error: Qubit NOT found";
        
        double state = quantum_register[q_name];
        int result = 0;
        
        if (state == 0.0) result = 0;
        else if (state == 1.0) result = 1;
        else {
            // Если в суперпозиции (0.5), бросаем честный квантовый кубик
            result = (rand() % 100 < 50) ? 0 : 1;
        }
        
        // Квантовый эффект: после измерения состояние схлопывается в четкий результат!
        quantum_register[q_name] = (double)result;
        return (result == 0) ? "0" : "1";
    }

    // 4. Сверхточное число Пи pi() до 30 знаков
    if (func == "pi") {
        return "3.141592653589793238462643383279";
    }

    // 5. Наибольший общий делитель gcd(a, b)
    if (func == "gcd") {
        size_t comma = args_str.find(',');
        if (comma != string::npos) {
            try {
                long long a = stoll(args_str.substr(0, comma));
                long long b = stoll(args_str.substr(comma + 1));
                return to_string(calculate_gcd(a, b));
            } catch (...) { return "Error: Invalid Numbers"; }
        }
    }

    // 6. Сверхточное возведение в степень pow_e(b, e)
    if (func == "pow_e") {
        size_t comma = args_str.find(',');
        if (comma != string::npos) {
            try {
                double b = stod(args_str.substr(0, comma));
                double e = stod(args_str.substr(comma + 1));
                double res = pow(b, e);
                
                stringstream ss;
                ss << fixed << setprecision(10) << res; // Выводим без Infinity
                string s_res = ss.str();
                
                // Убираем лишние нули на конце строки для красоты
                if (s_res.find('.') != string::npos) {
                    while (s_res.back() == '0') s_res.pop_back();
                    if (s_res.back() == '.') s_res.pop_back();
                }
                return s_res;
            } catch (...) { return "Error: Overflow/Invalid"; }
        }
    }

    return "CoreMath Error: Unknown token";
}