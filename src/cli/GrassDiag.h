#ifndef GRASS_DIAG_H
#define GRASS_DIAG_H

#include <string>
#include <vector>
#include <cstdint>
#include "frontend/GrassSyntax.h"

void PrintVersion(bool lang_ru);
void PrintHelp(bool lang_ru);
void DisassembleBytecode(const std::vector<uint8_t>& code, bool lang_ru);
void DumpAST(const std::vector<GrassToken>& tokens, const std::string& filename, bool lang_ru);

#endif // GRASS_DIAG_H
