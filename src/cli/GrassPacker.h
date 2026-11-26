#ifndef GRASS_PACKER_H
#define GRASS_PACKER_H

#include <string>
#include <vector>
#include <cstdint>

bool CheckAndRunEmbeddedBytecode();
bool BuildStandaloneExe(const std::string& output_exe, const std::vector<uint8_t>& bytecode);

#endif // GRASS_PACKER_H
