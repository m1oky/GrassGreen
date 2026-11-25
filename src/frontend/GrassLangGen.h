#ifndef GRASS_LANGGEN_H
#define GRASS_LANGGEN_H

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "frontend/GrassSyntax.h"

namespace GrassLangGen {

    enum class SegKind {
        LITERAL,
        ID,
        NUM,
        STR,
        EXPR,
        BLOCK
    };

    struct RuleSegment {
        SegKind kind;
        std::string text;
    };

    struct Rule {
        std::string name;
        std::string pattern;
        std::vector<RuleSegment> segments;
    };

    struct Instruction {
        std::string cmd;
        std::vector<std::string> args;
        int line = 1;
    };

    struct Program {
        std::vector<Instruction> instructions;
    };

    struct Language {
        std::string name;
        std::string comment_prefix;
        std::vector<Rule> rules;
        std::unordered_map<std::string, GrassValue> vars;
    };

    // Управление языком
    int64_t lang_new(const std::string& name);
    bool lang_comment(int64_t lang_id, const std::string& prefix);
    bool lang_rule(int64_t lang_id, const std::string& rule_name, const std::string& pattern);

    // Парсинг и инспекция AST
    int64_t lang_parse(int64_t lang_id, const std::string& source_code);
    int64_t lang_count(int64_t prog_id);
    std::string lang_cmd(int64_t prog_id, int64_t index);
    std::string lang_arg(int64_t prog_id, int64_t index, int64_t arg_index);

    // Вычислитель выражений и переменные
    GrassValue lang_eval(int64_t lang_id, const std::string& expr_str);
    void lang_set_var(int64_t lang_id, const std::string& name, const GrassValue& val);
    GrassValue lang_get_var(int64_t lang_id, const std::string& name);
}

#endif // GRASS_LANGGEN_H
