#include "GrassLangGen.h"
#include <iostream>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <cmath>

namespace GrassLangGen {

    static std::unordered_map<int64_t, Language> g_languages;
    static std::unordered_map<int64_t, Program> g_programs;
    static int64_t g_next_lang_id = 1;
    static int64_t g_next_prog_id = 1;

    static bool is_ident_start(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }

    static bool is_ident_cont(char c) {
        return is_ident_start(c) || (c >= '0' && c <= '9');
    }

    static std::vector<RuleSegment> parse_pattern(const std::string& pat) {
        std::vector<RuleSegment> segs;
        size_t i = 0;
        size_t n = pat.size();

        while (i < n) {
            while (i < n && std::isspace((unsigned char)pat[i])) i++;
            if (i >= n) break;

            if (pat[i] == '{') {
                size_t j = pat.find('}', i);
                if (j != std::string::npos) {
                    std::string tag = pat.substr(i, j - i + 1);
                    if (tag == "{id}") {
                        segs.push_back({ SegKind::ID, tag });
                    } else if (tag == "{num}") {
                        segs.push_back({ SegKind::NUM, tag });
                    } else if (tag == "{str}") {
                        segs.push_back({ SegKind::STR, tag });
                    } else if (tag == "{expr}") {
                        segs.push_back({ SegKind::EXPR, tag });
                    } else if (tag == "{block}") {
                        segs.push_back({ SegKind::BLOCK, tag });
                    } else {
                        segs.push_back({ SegKind::LITERAL, tag });
                    }
                    i = j + 1;
                    continue;
                }
            }

            if (is_ident_start(pat[i])) {
                size_t j = i;
                while (j < n && is_ident_cont(pat[j])) j++;
                segs.push_back({ SegKind::LITERAL, pat.substr(i, j - i) });
                i = j;
            } else {
                // Понктуация (;, =, +, etc.)
                size_t j = i;
                while (j < n && !std::isspace((unsigned char)pat[j]) && pat[j] != '{' && !is_ident_start(pat[j])) {
                    j++;
                }
                segs.push_back({ SegKind::LITERAL, pat.substr(i, j - i) });
                i = j;
            }
        }
        return segs;
    }

    int64_t lang_new(const std::string& name) {
        int64_t id = g_next_lang_id++;
        Language l;
        l.name = name;
        g_languages[id] = l;
        return id;
    }

    bool lang_comment(int64_t lang_id, const std::string& prefix) {
        auto it = g_languages.find(lang_id);
        if (it == g_languages.end()) return false;
        it->second.comment_prefix = prefix;
        return true;
    }

    bool lang_rule(int64_t lang_id, const std::string& rule_name, const std::string& pattern) {
        auto it = g_languages.find(lang_id);
        if (it == g_languages.end()) return false;
        Rule r;
        r.name = rule_name;
        r.pattern = pattern;
        r.segments = parse_pattern(pattern);
        it->second.rules.push_back(r);
        return true;
    }

    static void strip_comments(std::string& src, const std::string& prefix) {
        if (prefix.empty()) return;
        size_t n = src.size();
        size_t plen = prefix.size();
        bool in_str = false;
        char str_char = 0;

        for (size_t i = 0; i < n; ++i) {
            char c = src[i];
            if (!in_str && (c == '"' || c == '\'')) {
                in_str = true;
                str_char = c;
            } else if (in_str && c == str_char) {
                if (i == 0 || src[i - 1] != '\\') {
                    in_str = false;
                }
            } else if (!in_str && i + plen <= n && src.compare(i, plen, prefix) == 0) {
                while (i < n && src[i] != '\n') {
                    src[i] = ' ';
                    i++;
                }
            }
        }
    }

    static bool match_rule(const Rule& rule, const std::string& src, size_t pos,
                           size_t& new_pos, std::vector<std::string>& out_args) {
        size_t p = pos;
        size_t n = src.size();
        out_args.clear();

        for (size_t s_idx = 0; s_idx < rule.segments.size(); ++s_idx) {
            const auto& seg = rule.segments[s_idx];

            while (p < n && std::isspace((unsigned char)src[p])) p++;

            if (seg.kind == SegKind::LITERAL) {
                size_t l_len = seg.text.size();
                if (p + l_len > n || src.compare(p, l_len, seg.text) != 0) {
                    return false;
                }
                if (is_ident_start(seg.text[0])) {
                    if (p + l_len < n && is_ident_cont(src[p + l_len])) {
                        return false;
                    }
                }
                p += l_len;
            } else if (seg.kind == SegKind::ID) {
                if (p >= n || !is_ident_start(src[p])) return false;
                size_t start = p;
                while (p < n && is_ident_cont(src[p])) p++;
                out_args.push_back(src.substr(start, p - start));
            } else if (seg.kind == SegKind::NUM) {
                if (p >= n) return false;
                size_t start = p;
                if (src[p] == '-' || src[p] == '+') p++;
                if (p >= n || (!std::isdigit((unsigned char)src[p]) && src[p] != '.')) return false;
                bool has_dot = false;
                while (p < n) {
                    if (std::isdigit((unsigned char)src[p])) p++;
                    else if (src[p] == '.' && !has_dot) { has_dot = true; p++; }
                    else break;
                }
                out_args.push_back(src.substr(start, p - start));
            } else if (seg.kind == SegKind::STR) {
                if (p >= n || (src[p] != '"' && src[p] != '\'')) return false;
                char q = src[p++];
                size_t start = p;
                std::string s_val;
                bool closed = false;
                while (p < n) {
                    if (src[p] == '\\' && p + 1 < n) {
                        char esc = src[p + 1];
                        if (esc == 'n') s_val += '\n';
                        else if (esc == 't') s_val += '\t';
                        else if (esc == 'r') s_val += '\r';
                        else s_val += esc;
                        p += 2;
                    } else if (src[p] == q) {
                        p++;
                        closed = true;
                        break;
                    } else {
                        s_val += src[p++];
                    }
                }
                if (!closed) return false;
                out_args.push_back(s_val);
            } else if (seg.kind == SegKind::BLOCK) {
                if (p >= n || src[p] != '{') return false;
                p++;
                size_t start = p;
                int depth = 1;
                bool in_s = false;
                char sq = 0;
                while (p < n && depth > 0) {
                    char c = src[p];
                    if (!in_s && (c == '"' || c == '\'')) {
                        in_s = true;
                        sq = c;
                    } else if (in_s && c == sq) {
                        if (p > 0 && src[p - 1] != '\\') in_s = false;
                    } else if (!in_s) {
                        if (c == '{') depth++;
                        else if (c == '}') depth--;
                    }
                    if (depth > 0) p++;
                }
                if (depth != 0) return false;
                out_args.push_back(src.substr(start, p - start));
                p++; // Пропуск '}'
            } else if (seg.kind == SegKind::EXPR) {
                // Находим следующий разделитель из последующего сегмента правила
                std::string term = ";";
                if (s_idx + 1 < rule.segments.size() && rule.segments[s_idx + 1].kind == SegKind::LITERAL) {
                    term = rule.segments[s_idx + 1].text;
                }

                size_t start = p;
                int paren_depth = 0;
                int brace_depth = 0;
                bool in_s = false;
                char sq = 0;
                bool found = false;

                while (p < n) {
                    char c = src[p];
                    if (!in_s && (c == '"' || c == '\'')) {
                        in_s = true;
                        sq = c;
                    } else if (in_s && c == sq) {
                        if (p > 0 && src[p - 1] != '\\') in_s = false;
                    } else if (!in_s) {
                        if (c == '(' || c == '[') paren_depth++;
                        else if (c == ')' || c == ']') paren_depth--;
                        else if (c == '{') brace_depth++;
                        else if (c == '}') brace_depth--;
                        else if (paren_depth == 0 && brace_depth == 0) {
                            if (term == "{" && c == '{') {
                                found = true;
                                break;
                            }
                            if (p + term.size() <= n && src.compare(p, term.size(), term) == 0) {
                                found = true;
                                break;
                            }
                        }
                    }
                    p++;
                }

                if (!found && p < n) return false;
                std::string raw_expr = src.substr(start, p - start);
                // Trim
                size_t e_start = raw_expr.find_first_not_of(" \t\r\n");
                size_t e_end = raw_expr.find_last_not_of(" \t\r\n");
                if (e_start != std::string::npos && e_end != std::string::npos) {
                    raw_expr = raw_expr.substr(e_start, e_end - e_start + 1);
                } else {
                    raw_expr.clear();
                }
                out_args.push_back(raw_expr);
            }
        }

        new_pos = p;
        return true;
    }

    int64_t lang_parse(int64_t lang_id, const std::string& source_code) {
        auto it = g_languages.find(lang_id);
        if (it == g_languages.end()) return 0;
        const auto& lang = it->second;

        std::string src = source_code;
        strip_comments(src, lang.comment_prefix);

        Program prog;
        size_t p = 0;
        size_t n = src.size();
        int current_line = 1;

        while (p < n) {
            while (p < n && std::isspace((unsigned char)src[p])) {
                if (src[p] == '\n') current_line++;
                p++;
            }
            if (p >= n) break;

            bool matched = false;
            for (const auto& r : lang.rules) {
                size_t next_pos = p;
                std::vector<std::string> args;
                if (match_rule(r, src, p, next_pos, args)) {
                    Instruction inst;
                    inst.cmd = r.name;
                    inst.args = args;
                    inst.line = current_line;
                    prog.instructions.push_back(inst);

                    // Подсчитываем строки внутри совпавшего участка
                    for (size_t k = p; k < next_pos; ++k) {
                        if (src[k] == '\n') current_line++;
                    }
                    p = next_pos;
                    matched = true;
                    break;
                }
            }

            if (!matched) {
                // Извлекаем строку с ошибкой для вывода
                size_t err_end = p;
                while (err_end < n && src[err_end] != '\n' && src[err_end] != ';') err_end++;
                std::string err_line = src.substr(p, err_end - p);
                std::cerr << "[LangGen Error]: Строка " << current_line
                          << ": Неизвестная инструкция или синтаксическая ошибка: '" << err_line << "'\n";
                return 0;
            }
        }

        int64_t prog_id = g_next_prog_id++;
        g_programs[prog_id] = prog;
        return prog_id;
    }

    int64_t lang_count(int64_t prog_id) {
        auto it = g_programs.find(prog_id);
        if (it == g_programs.end()) return 0;
        return (int64_t)it->second.instructions.size();
    }

    std::string lang_cmd(int64_t prog_id, int64_t index) {
        auto it = g_programs.find(prog_id);
        if (it == g_programs.end()) return "";
        if (index < 0 || (size_t)index >= it->second.instructions.size()) return "";
        return it->second.instructions[index].cmd;
    }

    std::string lang_arg(int64_t prog_id, int64_t index, int64_t arg_index) {
        auto it = g_programs.find(prog_id);
        if (it == g_programs.end()) return "";
        if (index < 0 || (size_t)index >= it->second.instructions.size()) return "";
        const auto& args = it->second.instructions[index].args;
        if (arg_index < 0 || (size_t)arg_index >= args.size()) return "";
        return args[arg_index];
    }

    void lang_set_var(int64_t lang_id, const std::string& name, const GrassValue& val) {
        auto it = g_languages.find(lang_id);
        if (it != g_languages.end()) {
            it->second.vars[name] = val;
        }
    }

    GrassValue lang_get_var(int64_t lang_id, const std::string& name) {
        auto it = g_languages.find(lang_id);
        if (it != g_languages.end()) {
            auto vit = it->second.vars.find(name);
            if (vit != it->second.vars.end()) return vit->second;
        }
        return GrassValue::Num(0.0);
    }

    // Встроенный вычислитель выражений (рекурсивный спуск)
    class ExprParser {
        std::string s;
        size_t p = 0;
        const std::unordered_map<std::string, GrassValue>& vars;

        void skip_ws() {
            while (p < s.size() && std::isspace((unsigned char)s[p])) p++;
        }

        GrassValue parse_primary() {
            skip_ws();
            if (p >= s.size()) return GrassValue::Num(0.0);

            if (s[p] == '(') {
                p++;
                GrassValue val = parse_expr();
                skip_ws();
                if (p < s.size() && s[p] == ')') p++;
                return val;
            }

            if (s[p] == '"' || s[p] == '\'') {
                char q = s[p++];
                std::string str;
                while (p < s.size() && s[p] != q) {
                    if (s[p] == '\\' && p + 1 < s.size()) {
                        p++;
                        if (s[p] == 'n') str += '\n';
                        else if (s[p] == 't') str += '\t';
                        else str += s[p];
                    } else {
                        str += s[p];
                    }
                    p++;
                }
                if (p < s.size() && s[p] == q) p++;
                return GrassValue::Str(str);
            }

            if (std::isdigit((unsigned char)s[p]) || s[p] == '.') {
                size_t start = p;
                bool has_dot = false;
                while (p < s.size()) {
                    if (std::isdigit((unsigned char)s[p])) p++;
                    else if (s[p] == '.' && !has_dot) { has_dot = true; p++; }
                    else break;
                }
                double num = std::strtod(s.substr(start, p - start).c_str(), nullptr);
                return GrassValue::Num(num);
            }

            if (is_ident_start(s[p])) {
                size_t start = p;
                while (p < s.size() && is_ident_cont(s[p])) p++;
                std::string id = s.substr(start, p - start);
                if (id == "true") return GrassValue::Bool(true);
                if (id == "false") return GrassValue::Bool(false);
                if (id == "nil") return GrassValue::Nil();

                auto it = vars.find(id);
                if (it != vars.end()) return it->second;
                return GrassValue::Num(0.0);
            }

            if (s[p] == '-') {
                p++;
                GrassValue v = parse_primary();
                return GrassValue::Num(-v.toNumber());
            }

            if (s[p] == '!' || s[p] == '~') {
                char op = s[p++];
                GrassValue v = parse_primary();
                if (op == '!') return GrassValue::Bool(!v.isTruthy());
                return GrassValue::Num((double)(~(long long)v.toNumber()));
            }

            return GrassValue::Num(0.0);
        }

        GrassValue parse_factor() {
            GrassValue left = parse_primary();
            while (true) {
                skip_ws();
                if (p >= s.size()) break;
                char c = s[p];
                if (c == '*' || c == '/' || c == '%') {
                    p++;
                    GrassValue right = parse_primary();
                    if (c == '*') left = GrassValue::Num(left.toNumber() * right.toNumber());
                    else if (c == '/') {
                        double r = right.toNumber();
                        left = GrassValue::Num(r != 0.0 ? left.toNumber() / r : 0.0);
                    } else {
                        long long r = (long long)right.toNumber();
                        left = GrassValue::Num(r != 0 ? (long long)left.toNumber() % r : 0);
                    }
                } else {
                    break;
                }
            }
            return left;
        }

        GrassValue parse_term() {
            GrassValue left = parse_factor();
            while (true) {
                skip_ws();
                if (p >= s.size()) break;
                char c = s[p];
                if (c == '+' || c == '-') {
                    p++;
                    GrassValue right = parse_factor();
                    if (c == '+') {
                        if (left.is_str() || right.is_str()) {
                            left = GrassValue::Str(left.toString() + right.toString());
                        } else {
                            left = GrassValue::Num(left.toNumber() + right.toNumber());
                        }
                    } else {
                        left = GrassValue::Num(left.toNumber() - right.toNumber());
                    }
                } else {
                    break;
                }
            }
            return left;
        }

        GrassValue parse_shift() {
            GrassValue left = parse_term();
            while (true) {
                skip_ws();
                if (p + 1 < s.size()) {
                    if (s[p] == '<' && s[p + 1] == '<') {
                        p += 2;
                        GrassValue right = parse_term();
                        left = GrassValue::Num((double)((uint64_t)left.toNumber() << (int)right.toNumber()));
                        continue;
                    }
                    if (s[p] == '>' && s[p + 1] == '>') {
                        p += 2;
                        GrassValue right = parse_term();
                        left = GrassValue::Num((double)((uint64_t)left.toNumber() >> (int)right.toNumber()));
                        continue;
                    }
                }
                break;
            }
            return left;
        }

        GrassValue parse_cmp() {
            GrassValue left = parse_shift();
            while (true) {
                skip_ws();
                if (p >= s.size()) break;
                if (p + 1 < s.size() && s.substr(p, 2) == "==") {
                    p += 2;
                    GrassValue r = parse_shift();
                    left = GrassValue::Bool(left.toString() == r.toString());
                } else if (p + 1 < s.size() && s.substr(p, 2) == "!=") {
                    p += 2;
                    GrassValue r = parse_shift();
                    left = GrassValue::Bool(left.toString() != r.toString());
                } else if (p + 1 < s.size() && s.substr(p, 2) == "<=") {
                    p += 2;
                    GrassValue r = parse_shift();
                    left = GrassValue::Bool(left.toNumber() <= r.toNumber());
                } else if (p + 1 < s.size() && s.substr(p, 2) == ">=") {
                    p += 2;
                    GrassValue r = parse_shift();
                    left = GrassValue::Bool(left.toNumber() >= r.toNumber());
                } else if (s[p] == '<') {
                    p++;
                    GrassValue r = parse_shift();
                    left = GrassValue::Bool(left.toNumber() < r.toNumber());
                } else if (s[p] == '>') {
                    p++;
                    GrassValue r = parse_shift();
                    left = GrassValue::Bool(left.toNumber() > r.toNumber());
                } else {
                    break;
                }
            }
            return left;
        }

        GrassValue parse_bitwise() {
            GrassValue left = parse_cmp();
            while (true) {
                skip_ws();
                if (p >= s.size()) break;
                char c = s[p];
                if (c == '&' && (p + 1 >= s.size() || s[p + 1] != '&')) {
                    p++;
                    GrassValue r = parse_cmp();
                    left = GrassValue::Num((double)((long long)left.toNumber() & (long long)r.toNumber()));
                } else if (c == '|' && (p + 1 >= s.size() || s[p + 1] != '|')) {
                    p++;
                    GrassValue r = parse_cmp();
                    left = GrassValue::Num((double)((long long)left.toNumber() | (long long)r.toNumber()));
                } else if (c == '^') {
                    p++;
                    GrassValue r = parse_cmp();
                    left = GrassValue::Num((double)((long long)left.toNumber() ^ (long long)r.toNumber()));
                } else {
                    break;
                }
            }
            return left;
        }

    public:
        ExprParser(const std::string& str, const std::unordered_map<std::string, GrassValue>& v)
            : s(str), vars(v) {}

        GrassValue parse_expr() {
            skip_ws();
            if (s.empty()) return GrassValue::Num(0.0);
            return parse_bitwise();
        }
    };

    GrassValue lang_eval(int64_t lang_id, const std::string& expr_str) {
        auto it = g_languages.find(lang_id);
        if (it == g_languages.end()) return GrassValue::Num(0.0);
        ExprParser parser(expr_str, it->second.vars);
        return parser.parse_expr();
    }
}
