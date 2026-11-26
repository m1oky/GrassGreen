#pragma once
#include "native/GrassNativeCodegen.h"
#include "native/GrassNativeDebug.h"

namespace grass::codegen {

    backend::Reg phys_reg_to_backend(ir::PhysReg pr, TargetArch arch);
    backend::CondCode icmp_to_cond_code(ir::ICmpCond cond);

    struct X86FuncCodegen {
        const NativeCodegenOptions& options;
        const ir::IRModule& module;
        const std::unordered_map<std::string, uint64_t>& rodata_symbols;
        const backend::ABISpec& abi_spec;
        const std::vector<backend::Reg>& arg_regs;
        backend::GrassX86Encoder& encoder;
        const ir::IRFunction* fn = nullptr;
        const ir::AllocationResult& alloc_res;
        const std::unordered_map<ir::ValueId, int32_t>& alloca_offsets;
        int32_t spill_base_offset = 0;
        int32_t phi_scratch_base = 0;
        std::unordered_map<ir::BasicBlock*, backend::LabelId>& bb_labels;
        backend::LabelId epilogue_label = 0;

        backend::Reg get_loc_reg(ir::ValueId val) const;
        int32_t get_spill_offset(ir::ValueId val) const;
        backend::Reg to_vec_reg(backend::Reg xmm_r, ir::IRType* vtype) const;
        backend::Reg to_xmm_reg(backend::Reg r) const;
        backend::Reg load_val(ir::ValueId val, backend::Reg temp_reg);
        void store_val(ir::ValueId val, backend::Reg src_reg);
        void load_binary_fp(ir::ValueId v0, ir::ValueId v1, backend::Reg& out_lhs, backend::Reg& out_rhs);
        void load_binary_gpr(ir::ValueId v0, ir::ValueId v1, backend::Reg& out_lhs, backend::Reg& out_rhs);
        void emit_phi_copies(ir::BasicBlock* from_bb, ir::BasicBlock* to_bb);

        bool emit_arith_mem(const ir::BasicBlock* bb, const ir::IRInst* inst);
        bool emit_ctrl_fp_vec(const ir::BasicBlock* bb, const ir::IRInst* inst);
    };

} // namespace grass::codegen
