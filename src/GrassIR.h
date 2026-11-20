#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <utility>
#include <functional>
#include <optional>
#include <sstream>
#include <algorithm>

#include "GrassBackendTypes.h"

namespace grass::ir {

    using TargetABI = grass::backend::TargetABI;
    using TargetArch = grass::backend::TargetArch;

    // Forward declarations
    class IRType;
    class IRContext;
    class IRInst;
    class BasicBlock;
    class IRFunction;
    class IRModule;
    class IRBuilder;

    using ValueId = uint32_t;
    constexpr ValueId kInvalidValueId = 0;

    // 1. Type System (IRType)
    enum class TypeKind : uint8_t {
        Void,
        I1,
        I8,
        I16,
        I32,
        I64,
        F32,
        F64,
        Ptr,
        Array,
        Struct
    };

    struct StructField {
        std::string name;
        IRType* type = nullptr;
        uint64_t offset = 0;
    };

    class IRType {
    public:
        TypeKind kind = TypeKind::Void;
        uint64_t array_len = 0;
        IRType* element_type = nullptr;
        std::string struct_name;
        std::vector<StructField> struct_fields;
        uint64_t cached_size = 0;
        uint64_t cached_align = 1;

        bool is_void() const { return kind == TypeKind::Void; }
        bool is_bool() const { return kind == TypeKind::I1; }
        bool is_i1() const { return kind == TypeKind::I1; }
        bool is_i8() const { return kind == TypeKind::I8; }
        bool is_i16() const { return kind == TypeKind::I16; }
        bool is_i32() const { return kind == TypeKind::I32; }
        bool is_i64() const { return kind == TypeKind::I64; }
        bool is_integer() const;
        bool is_f32() const { return kind == TypeKind::F32; }
        bool is_f64() const { return kind == TypeKind::F64; }
        bool is_floating_point() const;
        bool is_pointer() const { return kind == TypeKind::Ptr; }
        bool is_array() const { return kind == TypeKind::Array; }
        bool is_struct() const { return kind == TypeKind::Struct; }
        bool is_numeric() const { return is_integer() || is_floating_point(); }

        uint32_t bit_width() const;
        uint64_t size() const { return cached_size; }
        uint64_t alignment() const { return cached_align; }

        int32_t get_field_index(const std::string& field_name) const;
        uint64_t get_field_offset(uint32_t index) const;
        IRType* get_field_type(uint32_t index) const;

        std::string to_string() const;
        bool equals(const IRType* other) const;
    };

    class IRContext {
    private:
        std::vector<std::unique_ptr<IRType>> types_storage_;
        IRType* type_void_ = nullptr;
        IRType* type_i1_ = nullptr;
        IRType* type_i8_ = nullptr;
        IRType* type_i16_ = nullptr;
        IRType* type_i32_ = nullptr;
        IRType* type_i64_ = nullptr;
        IRType* type_f32_ = nullptr;
        IRType* type_f64_ = nullptr;
        IRType* type_ptr_ = nullptr;

    public:
        IRContext();
        ~IRContext() = default;

        IRType* get_void_type();
        IRType* get_i1_type();
        IRType* get_i8_type();
        IRType* get_i16_type();
        IRType* get_i32_type();
        IRType* get_i64_type();
        IRType* get_f32_type();
        IRType* get_f64_type();
        IRType* get_ptr_type(IRType* pointee = nullptr);
        IRType* get_array_type(IRType* elem, uint64_t length);
        IRType* get_struct_type(const std::string& name,
                                const std::vector<IRType*>& field_types,
                                const std::vector<std::string>& field_names = {});
    };

    // 2. Opcodes & Instructions
    enum class IROp : uint8_t {
        // Constants & Moves
        ConstInt,
        ConstFloat,
        Copy,
        Phi,

        // Arithmetic
        Add,
        Sub,
        Mul,
        SDiv,
        UDiv,
        SRem,
        URem,
        Neg,
        FAdd,
        FSub,
        FMul,
        FDiv,
        FNeg,

        // Bitwise & Shifts
        And,
        Or,
        Xor,
        Not,
        Shl,
        LShr,
        AShr,

        // Comparisons
        ICmp,
        FCmp,

        // Type Conversions
        ZExt,
        SExt,
        Trunc,
        SIToFP,
        FPToSI,
        Bitcast,
        PtrToInt,
        IntToPtr,

        // Memory Operations
        Alloca,
        Load,
        Store,
        GetElementPtr,
        MemCpy,
        MemSet,

        // Control Flow & Calls
        Jump,
        Branch,
        Call,
        Ret
    };

    enum class ICmpCond : uint8_t {
        EQ,
        NE,
        SLT,
        SLE,
        SGT,
        SGE,
        ULT,
        ULE,
        UGT,
        UGE
    };

    enum class FCmpCond : uint8_t {
        OEQ,
        ONE,
        OLT,
        OLE,
        OGT,
        OGE
    };

    std::string to_string(IROp op);
    std::string to_string(ICmpCond cond);
    std::string to_string(FCmpCond cond);

    struct PhiIncoming {
        BasicBlock* block = nullptr;
        ValueId value = kInvalidValueId;
    };

    class IRInst {
    public:
        IROp op = IROp::Copy;
        IRType* type = nullptr;
        ValueId result_id = kInvalidValueId;
        BasicBlock* parent = nullptr;

        std::vector<ValueId> operands;

        int64_t int_imm = 0;
        double float_imm = 0.0;
        ICmpCond icmp_cond = ICmpCond::EQ;
        FCmpCond fcmp_cond = FCmpCond::OEQ;

        BasicBlock* true_target = nullptr;
        BasicBlock* false_target = nullptr;

        std::string callee_name;
        bool is_indirect_call = false;

        std::vector<PhiIncoming> phi_incoming;

        IRType* allocated_type = nullptr;
        uint32_t alloca_count = 1;
        uint32_t alloca_align = 0;

        int32_t gep_field_index = -1;

        bool is_terminator() const;
        bool is_binary_arithmetic() const;
        bool is_bitwise() const;
        bool is_comparison() const;
        bool is_cast() const;
        bool is_memory() const;
        bool has_side_effects() const;
        bool is_commutative() const;

        void add_phi_incoming(BasicBlock* block, ValueId val);
        void remove_phi_incoming(BasicBlock* block);
        ValueId get_phi_incoming_value(BasicBlock* block) const;

        void replace_operand(ValueId old_val, ValueId new_val);
        bool uses_value(ValueId val) const;

        std::string to_string() const;
    };

    // 3. Values and Def-Use Chains
    struct IRValue {
        ValueId id = kInvalidValueId;
        IRType* type = nullptr;
        std::string name;
        IRInst* def_inst = nullptr;
        std::vector<IRInst*> users;

        bool is_argument = false;
        uint32_t arg_index = 0;
    };

    // 4. BasicBlock
    class BasicBlock {
    public:
        std::string name;
        IRFunction* parent = nullptr;
        std::vector<std::unique_ptr<IRInst>> instructions;
        std::vector<BasicBlock*> preds;
        std::vector<BasicBlock*> succs;

        explicit BasicBlock(std::string name, IRFunction* parent = nullptr);

        IRInst* get_terminator() const;
        bool has_terminator() const;

        void push_back(std::unique_ptr<IRInst> inst);
        void push_front(std::unique_ptr<IRInst> inst);
        std::vector<std::unique_ptr<IRInst>>::iterator insert(
            std::vector<std::unique_ptr<IRInst>>::iterator pos,
            std::unique_ptr<IRInst> inst);
        bool erase(IRInst* inst);
        void erase(size_t index);

        void add_pred(BasicBlock* pred);
        void remove_pred(BasicBlock* pred);
        void add_succ(BasicBlock* succ);
        void remove_succ(BasicBlock* succ);

        std::string dump() const;
    };

    // 5. IRFunction
    class IRFunction {
    public:
        std::string name;
        IRType* return_type = nullptr;
        std::vector<IRValue> arguments;
        std::vector<std::unique_ptr<BasicBlock>> blocks;
        IRModule* parent = nullptr;

        std::unordered_map<ValueId, IRValue> values;
        ValueId next_value_id = 1;

        IRFunction(std::string name, IRType* ret_type, IRModule* parent = nullptr);

        ValueId allocate_value_id();
        ValueId create_value(IRType* type, const std::string& val_name = "", IRInst* def = nullptr);
        IRValue& get_value(ValueId id);
        const IRValue& get_value(ValueId id) const;
        bool has_value(ValueId id) const;

        void add_argument(const std::string& arg_name, IRType* type);
        BasicBlock* create_block(const std::string& blk_name = "");
        BasicBlock* get_entry_block() const;

        void replace_all_uses_with(ValueId old_val, ValueId new_val);
        void erase_inst(IRInst* inst);
        void erase_block(BasicBlock* bb);

        void rebuild_cfg();
        void rebuild_def_use();

        bool verify(std::string& error_out) const;
        std::string dump() const;
    };

    // 6. IRModule
    class IRModule {
    public:
        std::string name;
        IRContext context;
        TargetABI abi = TargetABI::Win64;
        TargetArch arch = TargetArch::X86_64;
        std::vector<std::unique_ptr<IRFunction>> functions;

        explicit IRModule(std::string name = "module");

        IRFunction* create_function(const std::string& fn_name, IRType* ret_type);
        IRFunction* get_function(const std::string& fn_name) const;

        bool verify(std::string& error_out) const;
        std::string dump() const;
    };

    // 7. IRBuilder
    class IRBuilder {
    private:
        IRFunction* function_ = nullptr;
        BasicBlock* insert_block_ = nullptr;
        size_t insert_index_ = 0;
        bool at_end_ = true;

        IRInst* insert_instruction(std::unique_ptr<IRInst> inst);

    public:
        IRBuilder() = default;
        explicit IRBuilder(IRFunction* fn);

        void set_function(IRFunction* fn);
        IRFunction* get_function() const { return function_; }
        void set_insert_point(BasicBlock* bb);
        void set_insert_point(BasicBlock* bb, size_t index);
        BasicBlock* get_insert_block() const { return insert_block_; }

        BasicBlock* create_block(const std::string& name = "");

        // Constants & Moves
        ValueId create_const_int(IRType* type, int64_t val);
        ValueId create_const_i1(bool val);
        ValueId create_const_i8(int8_t val);
        ValueId create_const_i16(int16_t val);
        ValueId create_const_i32(int32_t val);
        ValueId create_const_i64(int64_t val);
        ValueId create_const_f32(float val);
        ValueId create_const_f64(double val);
        ValueId create_copy(ValueId src, const std::string& name = "");
        ValueId create_phi(IRType* type, const std::string& name = "");
        void add_phi_incoming(ValueId phi_val, BasicBlock* block, ValueId val);

        // Integer Arithmetic
        ValueId create_add(ValueId lhs, ValueId rhs, const std::string& name = "");
        ValueId create_sub(ValueId lhs, ValueId rhs, const std::string& name = "");
        ValueId create_mul(ValueId lhs, ValueId rhs, const std::string& name = "");
        ValueId create_sdiv(ValueId lhs, ValueId rhs, const std::string& name = "");
        ValueId create_udiv(ValueId lhs, ValueId rhs, const std::string& name = "");
        ValueId create_srem(ValueId lhs, ValueId rhs, const std::string& name = "");
        ValueId create_urem(ValueId lhs, ValueId rhs, const std::string& name = "");
        ValueId create_neg(ValueId operand, const std::string& name = "");

        // Floating-Point Arithmetic
        ValueId create_fadd(ValueId lhs, ValueId rhs, const std::string& name = "");
        ValueId create_fsub(ValueId lhs, ValueId rhs, const std::string& name = "");
        ValueId create_fmul(ValueId lhs, ValueId rhs, const std::string& name = "");
        ValueId create_fdiv(ValueId lhs, ValueId rhs, const std::string& name = "");
        ValueId create_fneg(ValueId operand, const std::string& name = "");

        // Bitwise & Shifts
        ValueId create_and(ValueId lhs, ValueId rhs, const std::string& name = "");
        ValueId create_or(ValueId lhs, ValueId rhs, const std::string& name = "");
        ValueId create_xor(ValueId lhs, ValueId rhs, const std::string& name = "");
        ValueId create_not(ValueId operand, const std::string& name = "");
        ValueId create_shl(ValueId lhs, ValueId rhs, const std::string& name = "");
        ValueId create_lshr(ValueId lhs, ValueId rhs, const std::string& name = "");
        ValueId create_ashr(ValueId lhs, ValueId rhs, const std::string& name = "");

        // Comparisons
        ValueId create_icmp(ICmpCond cond, ValueId lhs, ValueId rhs, const std::string& name = "");
        ValueId create_icmp_eq(ValueId lhs, ValueId rhs, const std::string& name = "");
        ValueId create_icmp_ne(ValueId lhs, ValueId rhs, const std::string& name = "");
        ValueId create_icmp_slt(ValueId lhs, ValueId rhs, const std::string& name = "");
        ValueId create_icmp_sle(ValueId lhs, ValueId rhs, const std::string& name = "");
        ValueId create_icmp_sgt(ValueId lhs, ValueId rhs, const std::string& name = "");
        ValueId create_icmp_sge(ValueId lhs, ValueId rhs, const std::string& name = "");
        ValueId create_icmp_ult(ValueId lhs, ValueId rhs, const std::string& name = "");
        ValueId create_icmp_ule(ValueId lhs, ValueId rhs, const std::string& name = "");
        ValueId create_icmp_ugt(ValueId lhs, ValueId rhs, const std::string& name = "");
        ValueId create_icmp_uge(ValueId lhs, ValueId rhs, const std::string& name = "");
        ValueId create_fcmp(FCmpCond cond, ValueId lhs, ValueId rhs, const std::string& name = "");

        // Casts
        ValueId create_zext(ValueId val, IRType* dest_type, const std::string& name = "");
        ValueId create_sext(ValueId val, IRType* dest_type, const std::string& name = "");
        ValueId create_trunc(ValueId val, IRType* dest_type, const std::string& name = "");
        ValueId create_sitofp(ValueId val, IRType* dest_type, const std::string& name = "");
        ValueId create_fptosi(ValueId val, IRType* dest_type, const std::string& name = "");
        ValueId create_bitcast(ValueId val, IRType* dest_type, const std::string& name = "");
        ValueId create_ptrtoint(ValueId val, IRType* dest_type, const std::string& name = "");
        ValueId create_inttoptr(ValueId val, IRType* dest_type, const std::string& name = "");

        // Memory Operations
        ValueId create_alloca(IRType* elem_type, uint32_t count = 1, uint32_t align = 0, const std::string& name = "");
        ValueId create_load(ValueId ptr, const std::string& name = "");
        IRInst* create_store(ValueId val, ValueId ptr);
        ValueId create_gep(ValueId base_ptr, const std::vector<ValueId>& indices, IRType* result_elem_type = nullptr, const std::string& name = "");
        ValueId create_gep_struct(ValueId base_ptr, uint32_t field_index, const std::string& name = "");
        IRInst* create_memcpy(ValueId dst, ValueId src, ValueId size_bytes);
        IRInst* create_memset(ValueId dst, ValueId val_byte, ValueId size_bytes);

        // Control Flow & Calls
        IRInst* create_jump(BasicBlock* target);
        IRInst* create_branch(ValueId cond, BasicBlock* true_target, BasicBlock* false_target);
        ValueId create_call(const std::string& func_name, IRType* ret_type, const std::vector<ValueId>& args, const std::string& name = "");
        ValueId create_indirect_call(ValueId func_ptr, IRType* ret_type, const std::vector<ValueId>& args, const std::string& name = "");
        IRInst* create_ret(ValueId val);
        IRInst* create_ret_void();
    };

    // 8. Dominator Tree & SSA Mem2Reg
    class DominatorTree {
    public:
        IRFunction* function = nullptr;
        std::unordered_map<BasicBlock*, BasicBlock*> idom;
        std::unordered_map<BasicBlock*, std::vector<BasicBlock*>> dom_children;
        std::unordered_map<BasicBlock*, std::set<BasicBlock*>> dominance_frontiers;
        std::unordered_map<BasicBlock*, size_t> rpo_number;
        std::vector<BasicBlock*> rpo_blocks;

        void build(IRFunction& fn);
        bool dominates(BasicBlock* a, BasicBlock* b) const;
        const std::set<BasicBlock*>& get_frontier(BasicBlock* b) const;
        BasicBlock* get_idom(BasicBlock* b) const;
        const std::vector<BasicBlock*>& get_children(BasicBlock* b) const;
    };

    class Mem2RegPass {
    public:
        bool run_on_function(IRFunction& fn);
    };

    // 9. Optimization Passes
    class IRPass {
    public:
        virtual ~IRPass() = default;
        virtual std::string name() const = 0;
        virtual bool run_on_function(IRFunction& fn) = 0;
    };

    class ConstantFoldingPass : public IRPass {
    public:
        std::string name() const override { return "ConstantFolding"; }
        bool run_on_function(IRFunction& fn) override;
    };

    class AlgebraicSimplificationPass : public IRPass {
    public:
        std::string name() const override { return "AlgebraicSimplification"; }
        bool run_on_function(IRFunction& fn) override;
    };

    class StrengthReductionPass : public IRPass {
    public:
        std::string name() const override { return "StrengthReduction"; }
        bool run_on_function(IRFunction& fn) override;
    };

    class CommonSubexpressionEliminationPass : public IRPass {
    public:
        std::string name() const override { return "CommonSubexpressionElimination"; }
        bool run_on_function(IRFunction& fn) override;
    };

    class DeadCodeEliminationPass : public IRPass {
    public:
        std::string name() const override { return "DeadCodeElimination"; }
        bool run_on_function(IRFunction& fn) override;
    };

    class CFGSimplifyPass : public IRPass {
    public:
        std::string name() const override { return "CFGSimplify"; }
        bool run_on_function(IRFunction& fn) override;
    };

    class PassManager {
    public:
        std::vector<std::unique_ptr<IRPass>> passes;

        void add_pass(std::unique_ptr<IRPass> pass);
        template <typename T, typename... Args>
        void add(Args&&... args) {
            passes.push_back(std::make_unique<T>(std::forward<Args>(args)...));
        }

        void add_standard_optimizations();
        bool run(IRFunction& fn);
        bool run(IRModule& module);
    };

    // 10. Liveness Analysis & Linear Scan Register Allocator
    enum class RegClass : uint8_t {
        GPR,
        XMM
    };

    enum class PhysReg : int16_t {
        None = -1,
        // GPRs
        RAX = 0, RCX, RDX, RBX, RSP, RBP, RSI, RDI,
        R8, R9, R10, R11, R12, R13, R14, R15,

        // XMMs
        XMM0, XMM1, XMM2, XMM3, XMM4, XMM5, XMM6, XMM7,
        XMM8, XMM9, XMM10, XMM11, XMM12, XMM13, XMM14, XMM15,
        Count
    };

    std::string to_string(PhysReg reg);

    struct LiveInterval {
        ValueId val = kInvalidValueId;
        IRType* type = nullptr;
        int start = 0;
        int end = 0;
        bool is_spilled = false;
        PhysReg reg = PhysReg::None;
        int32_t stack_slot_offset = 0;
        bool crosses_call = false;
    };

    struct ParallelCopy {
        enum class LocKind { Reg, Stack };
        struct Location {
            LocKind kind = LocKind::Reg;
            PhysReg reg = PhysReg::None;
            int32_t stack_offset = 0;
            IRType* type = nullptr;

            bool operator==(const Location& o) const {
                return kind == o.kind && reg == o.reg && stack_offset == o.stack_offset;
            }
        };

        Location src;
        Location dst;
    };

    struct AllocationResult {
        TargetABI abi = TargetABI::Win64;
        std::unordered_map<ValueId, LiveInterval> intervals;
        std::set<PhysReg> used_callee_saved;
        uint32_t spill_count = 0;
        uint32_t stack_frame_size = 0;
        std::vector<ParallelCopy> phi_copies;
    };

    class LivenessAnalysis {
    public:
        IRFunction* function = nullptr;
        std::unordered_map<BasicBlock*, std::set<ValueId>> live_in;
        std::unordered_map<BasicBlock*, std::set<ValueId>> live_out;
        std::unordered_map<BasicBlock*, std::set<ValueId>> def_set;
        std::unordered_map<BasicBlock*, std::set<ValueId>> use_set;

        std::unordered_map<ValueId, LiveInterval> intervals;
        std::unordered_map<const IRInst*, int> inst_indices;
        std::vector<BasicBlock*> linear_blocks;

        void run(IRFunction& fn);
    };

    class LinearScanAllocator {
    public:
        TargetABI abi = TargetABI::Win64;

        explicit LinearScanAllocator(TargetABI target_abi = TargetABI::Win64)
            : abi(target_abi) {}

        AllocationResult allocate(IRFunction& fn);
    };

} // namespace grass::ir
