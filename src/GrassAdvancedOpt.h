#pragma once

#include "GrassIR.h"

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <memory>

namespace grass::opt {

    struct AdvancedOptOptions {
        int inline_threshold = 30;
        int unroll_factor = 4;
        bool enable_inlining = true;
        bool enable_licm = true;
        bool enable_unrolling = true;
        bool enable_strength_reduction = true;
        int max_inline_iterations = 4;
        int max_opt_iterations = 4;
    };

    class GrassAdvancedOpt {
    public:
        using Options = AdvancedOptOptions;

        // Unified entry point
        static bool optimize(ir::IRModule& module, int opt_level = 2);
        static bool optimize(ir::IRModule& module, const Options& options);

        // Individual passes on IRModule / IRFunction
        static bool run_inlining(ir::IRModule& module, int threshold = 30);
        static bool run_licm(ir::IRFunction& fn);
        static bool run_licm(ir::IRModule& module);
        static bool run_loop_unrolling(ir::IRFunction& fn, int factor = 4);
        static bool run_loop_unrolling(ir::IRModule& module, int factor = 4);
        static bool run_strength_reduction(ir::IRFunction& fn);
        static bool run_strength_reduction(ir::IRModule& module);

    private:
        // Internal helper methods for inlining
        static size_t count_instructions(const ir::IRFunction& fn);
        static bool can_inline_function(const ir::IRFunction& callee, int threshold,
                                       const std::unordered_set<std::string>& recursive_funcs);
        static bool inline_call_site(ir::IRFunction& caller, ir::BasicBlock* call_bb,
                                     size_t call_idx, ir::IRFunction& callee);
        static std::unordered_set<std::string> find_recursive_functions(const ir::IRModule& module);

        // Internal helper methods for LICM
        struct NaturalLoop {
            ir::BasicBlock* header = nullptr;
            std::set<ir::BasicBlock*> blocks;
            std::vector<ir::BasicBlock*> latches;
        };

        static std::vector<NaturalLoop> detect_natural_loops(ir::IRFunction& fn);
        static ir::BasicBlock* get_or_create_preheader(ir::IRFunction& fn, const NaturalLoop& loop);
        static bool is_hoistable_instruction(const ir::IRInst* inst);

        // Internal helper methods for Loop Unrolling
        struct LoopInductionInfo {
            ir::IRInst* phi_inst = nullptr;
            ir::IRInst* step_inst = nullptr;
            ir::IRInst* cmp_inst = nullptr;
            int64_t init_val = 0;
            int64_t step_val = 0;
            int64_t limit_val = 0;
            uint64_t trip_count = 0;
            bool is_valid = false;
        };

        static LoopInductionInfo analyze_induction_variable(const ir::IRFunction& fn,
                                                            const NaturalLoop& loop,
                                                            ir::BasicBlock* preheader);
        static bool unroll_loop(ir::IRFunction& fn, const NaturalLoop& loop,
                                const LoopInductionInfo& info, int factor);
    };

} // namespace grass::opt

namespace grass::ir {
    using GrassAdvancedOpt = grass::opt::GrassAdvancedOpt;
    using AdvancedOptOptions = grass::opt::AdvancedOptOptions;
} // namespace grass::ir

namespace grass {
    using GrassAdvancedOpt = grass::opt::GrassAdvancedOpt;
    using AdvancedOptOptions = grass::opt::AdvancedOptOptions;
} // namespace grass
