// =============================================================================
//  GrassGreen 4.2.0 — Sovereign C++ Installer Payload Generator
//  Генерирует payload_data.h напрямую из C++ без использования Python
// =============================================================================

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <iomanip>

namespace fs = std::filesystem;

static std::vector<unsigned char> ReadBinaryFile(const fs::path& p) {
    if (!fs::exists(p)) return {};
    std::ifstream file(p, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> buffer(size);
    if (file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return buffer;
    }
    return {};
}

static std::string ReadTextFile(const fs::path& p) {
    if (!fs::exists(p)) return "";
    std::ifstream file(p, std::ios::binary);
    if (!file.is_open()) return "";
    std::string str((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return str;
}

static void WriteByteArray(std::ofstream& out, const std::string& varName, const std::vector<unsigned char>& data) {
    out << "const size_t " << varName << "_SIZE = " << data.size() << ";\n";
    out << "const unsigned char " << varName << "[] = {\n";
    for (size_t i = 0; i < data.size(); ++i) {
        if (i % 16 == 0) out << "    ";
        out << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
        if (i + 1 < data.size()) out << ",";
        if (i % 16 == 15 || i + 1 == data.size()) out << "\n";
        else out << " ";
    }
    out << std::dec << "};\n\n";
}

static std::string EscapeString(const std::string& input) {
    std::string res;
    for (char c : input) {
        if (c == '\\') res += "\\\\";
        else if (c == '"') res += "\\\"";
        else if (c == '\n') res += "\\n";
        else if (c == '\r') continue;
        else res += c;
    }
    return res;
}

int main() {
    fs::path installerDir = fs::current_path();
    fs::path rootDir = installerDir.parent_path();

    // Check if running from root or installer dir
    if (fs::exists(installerDir / "setup.cpp")) {
        // running inside installer/
    } else if (fs::exists(installerDir / "installer" / "setup.cpp")) {
        rootDir = installerDir;
        installerDir = rootDir / "installer";
    }

    fs::path grgExePath = rootDir / "bin" / "grg.exe";
    fs::path grassIcoPath = installerDir / "grass.ico";
    fs::path gdbIcoPath = installerDir / "gdb.ico";
    fs::path logoSvgPath = rootDir / "logo.svg";
    fs::path gdbSvgPath = rootDir / "gdb_logo.svg";
    fs::path testGrassPath = rootDir / "examples" / "io_test.grass";
    fs::path vsixPath = rootDir / "bin" / "grassgreen-4.2.2.vsix";
    if (!fs::exists(vsixPath)) vsixPath = rootDir / ".vscode" / "extension" / "grassgreen-4.2.2.vsix";
    fs::path outHPath = installerDir / "payload_data.h";

    std::cout << "[GrassGreen Payload Builder] Reading binary assets...\n";

    auto grgBytes = ReadBinaryFile(grgExePath);
    if (grgBytes.empty()) {
        std::cerr << "[Error] Cannot read " << grgExePath.string() << "!\n";
        return 1;
    }

    auto grassIcoBytes = ReadBinaryFile(grassIcoPath);
    auto gdbIcoBytes = ReadBinaryFile(gdbIcoPath);
    auto vsixBytes = ReadBinaryFile(vsixPath);
    auto logoSvgText = ReadTextFile(logoSvgPath);
    auto gdbSvgText = ReadTextFile(gdbSvgPath);
    auto testCode = ReadTextFile(testGrassPath);

    std::ofstream out(outHPath, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "[Error] Cannot open output file " << outHPath.string() << "!\n";
        return 1;
    }

    out << "// =============================================================================\n";
    out << "//  Auto-generated payload for GrassGreen 4.2.0 setup.exe\n";
    out << "//  Generated directly by sovereign C++ tool (make_payload.cpp)\n";
    out << "// =============================================================================\n";
    out << "#ifndef GRASS_PAYLOAD_DATA_H\n";
    out << "#define GRASS_PAYLOAD_DATA_H\n\n";
    out << "#include <stddef.h>\n\n";

    WriteByteArray(out, "G_PAYLOAD_GRG_EXE", grgBytes);
    WriteByteArray(out, "G_PAYLOAD_GRASS_ICO", grassIcoBytes);
    WriteByteArray(out, "G_PAYLOAD_GDB_ICO", gdbIcoBytes);
    WriteByteArray(out, "G_PAYLOAD_VSIX", vsixBytes);

    out << "const char* const G_PAYLOAD_GRG_CMD = \"@echo off\\r\\n\\\"%~dp0grg.exe\\\" %*\\r\\n\";\n";
    out << "const char* const G_PAYLOAD_GRASS_CMD = \"@echo off\\r\\n\\\"%~dp0grg.exe\\\" %*\\r\\n\";\n";
    out << "const char* const G_PAYLOAD_GG_CMD = \"@echo off\\r\\n\\\"%~dp0grg.exe\\\" %*\\r\\n\";\n\n";

    out << "const char* const G_PAYLOAD_LOGO_SVG = \"" << EscapeString(logoSvgText) << "\";\n";
    out << "const char* const G_PAYLOAD_GDB_LOGO_SVG = \"" << EscapeString(gdbSvgText) << "\";\n\n";
    out << "const char* const G_PAYLOAD_IO_TEST = \"" << EscapeString(testCode) << "\";\n\n";

    out << "#endif // GRASS_PAYLOAD_DATA_H\n";
    out.close();

    std::cout << "[GrassGreen Payload Builder] Generated " << outHPath.string() 
              << " (" << fs::file_size(outHPath) << " bytes) successfully.\n";
    return 0;
}
