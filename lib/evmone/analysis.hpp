// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2019-2020 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "execution_state.hpp"
#include "limits.hpp"
#include <absl/container/flat_hash_map.h>
#include <evmc/evmc.hpp>
#include <evmc/instructions.h>
#include <evmc/utils.h>
#include <intx/intx.hpp>
#include <array>
#include <cstdint>
#include <vector>

namespace evmone
{
struct instruction;

struct block_info
{
    /// The total base gas cost of all instructions in the block.
    /// This cannot overflow, see the static_assert() below.
    uint32_t gas_cost = 0;

    static_assert(
        max_code_size * max_instruction_base_cost < std::numeric_limits<decltype(gas_cost)>::max(),
        "Potential block_info::gas_cost overflow");

    /// The stack height required to execute the block.
    /// This MAY overflow.
    int16_t stack_req = 0;

    /// The maximum stack height growth relative to the stack height at block start.
    /// This cannot overflow, see the static_assert() below.
    int16_t stack_max_growth = 0;

    static_assert(max_code_size * max_instruction_stack_increase <
                      std::numeric_limits<decltype(stack_max_growth)>::max(),
        "Potential block_info::stack_max_growth overflow");
};
static_assert(sizeof(block_info) == 8);

struct code_analysis;

struct execution_state : ExecutionState
{
    /// The gas cost of the current block.
    ///
    /// This is only needed to correctly calculate the "current gas left" value.
    uint32_t current_block_cost = 0;

    /// Pointer to code analysis.
    const code_analysis* analysis = nullptr;

    execution_state() = default;

    execution_state(const evmc_message& message, evmc_revision revision,
        const evmc_host_interface& host_interface, evmc_host_context* host_ctx,
        const uint8_t* code_ptr, size_t code_size, const code_analysis& a) noexcept
      : ExecutionState{message, revision, host_interface, host_ctx, code_ptr, code_size},
        analysis{&a}
    {}

    /// Terminates the execution with the given status code.
    const instruction* exit(evmc_status_code status_code) noexcept
    {
        status = status_code;
        return nullptr;
    }

    /// Resets the contents of the execution_state so that it could be reused.
    void reset(const evmc_message& message, evmc_revision revision,
        const evmc_host_interface& host_interface, evmc_host_context* host_ctx,
        const uint8_t* code_ptr, size_t code_size, const code_analysis& a) noexcept
    {
        ExecutionState::reset(message, revision, host_interface, host_ctx, code_ptr, code_size);
        current_block_cost = 0;
        analysis = &a;
    }
};

union instruction_argument
{
    int64_t number;
    const intx::uint256* push_value;
    uint64_t small_push_value;
    block_info block{};
};
static_assert(
    sizeof(instruction_argument) == sizeof(uint64_t), "Incorrect size of instruction_argument");

/// The pointer to function implementing an instruction execution.
using instruction_exec_fn = const instruction* (*)(const instruction*, execution_state&);

/// The evmone intrinsic opcodes.
///
/// These intrinsic instructions may be injected to the code in the analysis phase.
/// They contain additional and required logic to be executed by the interpreter.
enum intrinsic_opcodes
{
    /// The BEGINBLOCK instruction.
    ///
    /// This instruction is defined as alias for JUMPDEST and replaces all JUMPDEST instructions.
    /// It is also injected at beginning of basic blocks not being the valid jump destination.
    /// It checks basic block execution requirements and terminates execution if they are not met.
    OPX_BEGINBLOCK = OP_JUMPDEST
};

struct op_table_entry
{
    instruction_exec_fn fn;
    int16_t gas_cost;
    int8_t stack_req;
    int8_t stack_change;
};

using op_table = std::array<op_table_entry, 256>;

struct instruction
{
    instruction_exec_fn fn = nullptr;
    instruction_argument arg;

    explicit constexpr instruction(instruction_exec_fn f) noexcept : fn{f}, arg{} {}
};

struct code_analysis
{
    std::vector<instruction> instrs;

    /// Storage for large push values.
    std::vector<intx::uint256> push_values;

    /// Keys are the offsets of JUMPDESTs in the original code.
    /// They are what JUMP/JUMPI receives as an argument.
    ///
    /// Values are the indexes of the instructions in the generated instruction table
    /// that match the offsets.
    absl::flat_hash_map<int32_t, int32_t> jumpdest;
};

inline int find_jumpdest(const code_analysis& analysis, int offset) noexcept
{
    const auto it = analysis.jumpdest.find(offset);
    return it == analysis.jumpdest.end() ? -1 : it->second;
}

EVMC_EXPORT code_analysis analyze(
    evmc_revision rev, const uint8_t* code, size_t code_size) noexcept;

EVMC_EXPORT const op_table& get_op_table(evmc_revision rev) noexcept;

}  // namespace evmone
