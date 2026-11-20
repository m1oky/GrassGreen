#pragma once
#ifndef GRASS_NATIVE_CODEGEN_H
#define GRASS_NATIVE_CODEGEN_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

#include "GrassBackendTypes.h"
#include "GrassIR.h"
#include "GrassX86Encoder.h"
#include "GrassObjWriter.h"
#include "GrassSyntax.h"

namespace grass::codegen {

    using TargetArch = grass::backend::TargetArch;
    using TargetABI = grass::backend::TargetABI;
    using CompiledUnit = grass::backend::CompiledUnit;

    struct NativeCodegenOptions {
        TargetArch arch = TargetArch::X86_64;
        TargetABI abi = TargetABI::Win64;
        int opt_level = 2; // 0, 1, 2, 3
        uint64_t base_address = 0x100000; // RQWos baremetal default
        bool emit_ir = false;
        std::string emit_obj_path = "";
        std::string emit_bin_path = "";
        std::string output_exe_path = "";
        bool run_after_build = false;
        bool freestanding = false;
        bool verbose = false;
        bool lang_ru = false;
    };

    struct NativeCodegenResult {
        bool success = false;
        std::string error_message;
        std::string ir_dump_initial;
        std::string ir_dump_optimized;
        CompiledUnit compiled_unit;
        std::string obj_file_created;
        std::string bin_file_created;
        std::string exe_file_created;
    };

    class NativeCompiler {
    public:
        explicit NativeCompiler(const NativeCodegenOptions& options = {});

        // Full compilation pipeline entry point
        NativeCodegenResult compile(const std::vector<GrassToken>& tokens, const std::string& source_path = "");
        NativeCodegenResult compile_source(const std::string& source_code, const std::string& source_path = "");
        NativeCodegenResult compile_file(const std::string& file_path);

        // Individual pipeline stages
        bool lower_ast_to_ir(const std::vector<GrassToken>& tokens,
                             ir::IRModule& module,
                             std::vector<uint8_t>& rodata,
                             std::unordered_map<std::string, uint64_t>& rodata_symbols,
                             std::string& error);

        bool optimize_ir(ir::IRModule& module, int opt_level, std::string& error);

        bool codegen_module(ir::IRModule& module,
                            const std::vector<uint8_t>& rodata,
                            const std::unordered_map<std::string, uint64_t>& rodata_symbols,
                            CompiledUnit& unit,
                            std::string& error);

        bool emit_containers(const CompiledUnit& unit,
                             const std::string& base_name,
                             NativeCodegenResult& res,
                             std::string& error);

        bool link_executable(const std::string& obj_path,
                             const std::string& exe_path,
                             std::string& error);

        static TargetArch parse_arch(const std::string& str);
        static TargetABI parse_abi(const std::string& str);
        static std::string arch_to_string(TargetArch arch);
        static std::string abi_to_string(TargetABI abi);

    private:
        NativeCodegenOptions options_;

        bool ensure_runtime_c_file(std::string& runtime_path, std::string& error);
    };

} // namespace grass::codegen

#endif // GRASS_NATIVE_CODEGEN_H
