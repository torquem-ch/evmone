// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2020 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "instructions.hpp"

namespace evmone
{
constexpr int num_revisions{EVMC_MAX_REVISION + 1};

using GasCostTable = std::array<int16_t, 256>;

template <evmc_revision Revision>
constexpr GasCostTable get_gas_cost_table() noexcept;

#define ZERO 0
#define BASE 2
#define VERYLOW 3
#define LOW 5
#define MID 8
#define HIGH 10

template <>
constexpr GasCostTable get_gas_cost_table<EVMC_FRONTIER>() noexcept
{
    GasCostTable table{};
    for (auto& e : table)
        e = -1;  // Init undefined instructions with negative value.

    table[OP_STOP] = ZERO;
    table[OP_ADD] = VERYLOW;
    table[OP_MUL] = LOW;
    table[OP_SUB] = VERYLOW;
    table[OP_DIV] = LOW;
    table[OP_SDIV] = LOW;
    table[OP_MOD] = LOW;
    table[OP_SMOD] = LOW;
    table[OP_ADDMOD] = MID;
    table[OP_MULMOD] = MID;
    table[OP_EXP] = HIGH;
    table[OP_SIGNEXTEND] = LOW;
    table[OP_LT] = VERYLOW;
    table[OP_GT] = VERYLOW;
    table[OP_SLT] = VERYLOW;
    table[OP_SGT] = VERYLOW;
    table[OP_EQ] = VERYLOW;
    table[OP_ISZERO] = VERYLOW;
    table[OP_AND] = VERYLOW;
    table[OP_OR] = VERYLOW;
    table[OP_XOR] = VERYLOW;
    table[OP_NOT] = VERYLOW;
    table[OP_BYTE] = VERYLOW;
    table[OP_SHA3] = 30;
    table[OP_ADDRESS] = BASE;
    table[OP_BALANCE] = 20;
    table[OP_ORIGIN] = BASE;
    table[OP_CALLER] = BASE;
    table[OP_CALLVALUE] = BASE;
    table[OP_CALLDATALOAD] = VERYLOW;
    table[OP_CALLDATASIZE] = BASE;
    table[OP_CALLDATACOPY] = VERYLOW;
    table[OP_CODESIZE] = BASE;
    table[OP_CODECOPY] = VERYLOW;
    table[OP_GASPRICE] = BASE;
    table[OP_EXTCODESIZE] = 20;
    table[OP_EXTCODECOPY] = 20;
    table[OP_BLOCKHASH] = 20;
    table[OP_COINBASE] = BASE;
    table[OP_TIMESTAMP] = BASE;
    table[OP_NUMBER] = BASE;
    table[OP_DIFFICULTY] = BASE;
    table[OP_GASLIMIT] = BASE;
    table[OP_POP] = BASE;
    table[OP_MLOAD] = VERYLOW;
    table[OP_MSTORE] = VERYLOW;
    table[OP_MSTORE8] = VERYLOW;
    table[OP_SLOAD] = 50;
    table[OP_SSTORE] = 0;
    table[OP_JUMP] = MID;
    table[OP_JUMPI] = HIGH;
    table[OP_PC] = BASE;
    table[OP_MSIZE] = BASE;
    table[OP_GAS] = BASE;
    table[OP_JUMPDEST] = 1;

    for (size_t op = OP_PUSH1; op <= OP_PUSH32; ++op)
        table[op] = VERYLOW;

    for (size_t op = OP_DUP1; op <= OP_DUP16; ++op)
        table[op] = VERYLOW;

    for (size_t op = OP_SWAP1; op <= OP_SWAP16; ++op)
        table[op] = VERYLOW;

    for (size_t op = OP_LOG0; op <= OP_LOG4; ++op)
        table[op] = static_cast<int16_t>((op - OP_LOG0 + 1) * 375);

    table[OP_CREATE] = 32000;
    table[OP_CALL] = 40;
    table[OP_CALLCODE] = 40;
    table[OP_RETURN] = ZERO;
    table[OP_INVALID] = ZERO;
    table[OP_SELFDESTRUCT] = ZERO;

    return table;
}

template <>
constexpr GasCostTable get_gas_cost_table<EVMC_HOMESTEAD>() noexcept
{
    auto table = get_gas_cost_table<EVMC_FRONTIER>();
    table[OP_DELEGATECALL] = 40;
    return table;
}

template <>
constexpr GasCostTable get_gas_cost_table<EVMC_TANGERINE_WHISTLE>() noexcept
{
    auto table = get_gas_cost_table<EVMC_HOMESTEAD>();
    table[OP_BALANCE] = 400;
    table[OP_EXTCODESIZE] = 700;
    table[OP_EXTCODECOPY] = 700;
    table[OP_SLOAD] = 200;
    table[OP_CALL] = 700;
    table[OP_CALLCODE] = 700;
    table[OP_DELEGATECALL] = 700;
    table[OP_SELFDESTRUCT] = 5000;
    return table;
}

template <>
constexpr GasCostTable get_gas_cost_table<EVMC_SPURIOUS_DRAGON>() noexcept
{
    auto table = get_gas_cost_table<EVMC_TANGERINE_WHISTLE>();
    return table;
}

template <>
constexpr GasCostTable get_gas_cost_table<EVMC_BYZANTIUM>() noexcept
{
    auto table = get_gas_cost_table<EVMC_SPURIOUS_DRAGON>();
    table[OP_RETURNDATASIZE] = BASE;
    table[OP_RETURNDATACOPY] = VERYLOW;
    table[OP_STATICCALL] = 700;
    table[OP_REVERT] = ZERO;
    return table;
}

template <>
constexpr GasCostTable get_gas_cost_table<EVMC_CONSTANTINOPLE>() noexcept
{
    auto table = get_gas_cost_table<EVMC_BYZANTIUM>();
    table[OP_SHL] = VERYLOW;
    table[OP_SHR] = VERYLOW;
    table[OP_SAR] = VERYLOW;
    table[OP_EXTCODEHASH] = 400;
    table[OP_CREATE2] = 32000;
    return table;
}

template <>
constexpr GasCostTable get_gas_cost_table<EVMC_ISTANBUL>() noexcept
{
    auto table = get_gas_cost_table<EVMC_CONSTANTINOPLE>();
    table[OP_BALANCE] = 700;
    table[OP_CHAINID] = BASE;
    table[OP_EXTCODEHASH] = 700;
    table[OP_SELFBALANCE] = LOW;
    table[OP_SLOAD] = 800;
    return table;
}

std::array<GasCostTable, num_revisions> gas_costs = {
    get_gas_cost_table<EVMC_FRONTIER>(),
    get_gas_cost_table<EVMC_HOMESTEAD>(),
    get_gas_cost_table<EVMC_TANGERINE_WHISTLE>(),
    get_gas_cost_table<EVMC_SPURIOUS_DRAGON>(),
    get_gas_cost_table<EVMC_BYZANTIUM>(),
    get_gas_cost_table<EVMC_CONSTANTINOPLE>(),
    get_gas_cost_table<EVMC_CONSTANTINOPLE>(),
    get_gas_cost_table<EVMC_ISTANBUL>(),
    get_gas_cost_table<EVMC_ISTANBUL>(),
};


struct StackTraits
{
    int8_t required;
    int8_t change;
};

constexpr std::array<StackTraits, 256> get_stack_traits() noexcept
{
    std::array<StackTraits, 256> table{};


    table[OP_STOP] = {0, 0};
    table[OP_ADD] = {2, -1};
    table[OP_MUL] = {2, -1};
    table[OP_SUB] = {2, -1};
    table[OP_DIV] = {2, -1};
    table[OP_SDIV] = {2, -1};
    table[OP_MOD] = {2, -1};
    table[OP_SMOD] = {2, -1};
    table[OP_ADDMOD] = {3, -2};
    table[OP_MULMOD] = {3, -2};
    table[OP_EXP] = {2, -1};
    table[OP_SIGNEXTEND] = {2, -1};
    table[OP_LT] = {2, -1};
    table[OP_GT] = {2, -1};
    table[OP_SLT] = {2, -1};
    table[OP_SGT] = {2, -1};
    table[OP_EQ] = {2, -1};
    table[OP_ISZERO] = {1, 0};
    table[OP_AND] = {2, -1};
    table[OP_OR] = {2, -1};
    table[OP_XOR] = {2, -1};
    table[OP_NOT] = {1, 0};
    table[OP_BYTE] = {2, -1};
    table[OP_SHL] = {2, -1};
    table[OP_SHR] = {2, -1};
    table[OP_SAR] = {2, -1};
    table[OP_SHA3] = {2, -1};
    table[OP_ADDRESS] = {0, 1};
    table[OP_BALANCE] = {1, 0};
    table[OP_ORIGIN] = {0, 1};
    table[OP_CALLER] = {0, 1};
    table[OP_CALLVALUE] = {0, 1};
    table[OP_CALLDATALOAD] = {1, 0};
    table[OP_CALLDATASIZE] = {0, 1};
    table[OP_CALLDATACOPY] = {3, -3};
    table[OP_CODESIZE] = {0, 1};
    table[OP_CODECOPY] = {3, -3};
    table[OP_GASPRICE] = {0, 1};
    table[OP_EXTCODESIZE] = {1, 0};
    table[OP_EXTCODECOPY] = {4, -4};
    table[OP_RETURNDATASIZE] = {0, 1};
    table[OP_RETURNDATACOPY] = {3, -3};
    table[OP_EXTCODEHASH] = {1, 0};
    table[OP_BLOCKHASH] = {1, 0};
    table[OP_COINBASE] = {0, 1};
    table[OP_TIMESTAMP] = {0, 1};
    table[OP_NUMBER] = {0, 1};
    table[OP_DIFFICULTY] = {0, 1};
    table[OP_GASLIMIT] = {0, 1};
    table[OP_CHAINID] = {0, 1};
    table[OP_SELFBALANCE] = {0, 1};
    table[OP_POP] = {1, -1};
    table[OP_MLOAD] = {1, 0};
    table[OP_MSTORE] = {2, -2};
    table[OP_MSTORE8] = {2, -2};
    table[OP_SLOAD] = {1, 0};
    table[OP_SSTORE] = {2, -2};
    table[OP_JUMP] = {1, -1};
    table[OP_JUMPI] = {2, -2};
    table[OP_PC] = {0, 1};
    table[OP_MSIZE] = {0, 1};
    table[OP_GAS] = {0, 1};
    table[OP_JUMPDEST] = {0, 0};

    for (size_t op = OP_PUSH1; op <= OP_PUSH32; ++op)
        table[op] = {0, 1};

    for (size_t op = OP_DUP1; op <= OP_DUP16; ++op)
        table[op] = {static_cast<int8_t>(op - OP_DUP1 + 1), 1};

    for (size_t op = OP_SWAP1; op <= OP_SWAP16; ++op)
        table[op] = {static_cast<int8_t>(op - OP_SWAP1 + 2), 0};

    for (size_t op = OP_LOG0; op <= OP_LOG4; ++op)
        table[op] = {
            static_cast<int8_t>(op - OP_LOG0 + 2), static_cast<int8_t>(-(op - OP_LOG0 + 2))};

    table[OP_CREATE] = {3, -2};
    table[OP_CALL] = {7, -6};
    table[OP_CALLCODE] = {7, -6};
    table[OP_RETURN] = {2, -2};
    table[OP_DELEGATECALL] = {6, -5};
    table[OP_CREATE2] = {4, -3};
    table[OP_STATICCALL] = {6, -5};
    table[OP_REVERT] = {2, -2};
    table[OP_INVALID] = {0, 0};
    table[OP_SELFDESTRUCT] = {1, -1};

    return table;
}

constexpr auto stack_traits = get_stack_traits();

struct BaselineTraits
{
    int16_t gas_cost;
    int8_t stack_check;
};

template <evmc_revision Revision>
constexpr std::array<BaselineTraits, 256> get_baseline_table() noexcept
{
    std::array<BaselineTraits, 256> table;

    for (size_t i = 0; i < table.size(); ++i)
    {
        const auto st = stack_traits[i];
        const auto stack_check = static_cast<int8_t>(st.change <= 0 ? -st.required : st.change);
        table[i] = {gas_costs[Revision][i], stack_check};
    }
    return table;
}

std::array<std::array<BaselineTraits, 256>, num_revisions> baseline_table = {
    get_baseline_table<EVMC_FRONTIER>(),
    get_baseline_table<EVMC_HOMESTEAD>(),
    get_baseline_table<EVMC_TANGERINE_WHISTLE>(),
    get_baseline_table<EVMC_SPURIOUS_DRAGON>(),
    get_baseline_table<EVMC_BYZANTIUM>(),
    get_baseline_table<EVMC_CONSTANTINOPLE>(),
    get_baseline_table<EVMC_PETERSBURG>(),
    get_baseline_table<EVMC_ISTANBUL>(),
    get_baseline_table<EVMC_BERLIN>(),
};

}  // namespace evmone
