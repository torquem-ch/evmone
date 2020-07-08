// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2020 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#include "baseline.hpp"
#include "analysis.hpp"
#include "instructions.hpp"
#include <memory>

namespace evmone
{
namespace
{
template <size_t Len>
const uint8_t* load_push(
    ExecutionState& state, const uint8_t* code, const uint8_t* code_end) noexcept
{
    // TODO: Also last full push can be ignored.
    if (code + Len > code_end)  // Trimmed push data can be ignored.
        return code_end - 1;

    uint8_t buffer[Len];
    std::memcpy(buffer, code, Len);
    state.stack.push(intx::be::load<intx::uint256>(buffer));
    return code + Len - 1;
}
}  // namespace

evmc_result baseline_execute([[maybe_unused]] evmc_vm* vm, const evmc_host_interface* host,
    evmc_host_context* ctx, evmc_revision rev, const evmc_message* msg, const uint8_t* code,
    size_t code_size) noexcept
{
    const auto op_tbl = get_op_table(rev);

    auto state = std::make_unique<ExecutionState>();
    state->msg = msg;
    state->host = evmc::HostContext{*host, ctx};
    state->gas_left = msg->gas;
    state->rev = rev;

    evmc_result result{};

    const auto code_end = code + code_size;
    for (auto pc = code; pc != code_end; ++pc)
    {
        const auto op = *pc;
        const auto metrics = op_tbl[op];

        if ((state->gas_left -= metrics.gas_cost) < 0)
        {
            result.status_code = EVMC_OUT_OF_GAS;
            break;
        }

        if (state->stack.size() < metrics.stack_req)
        {
            result.status_code = EVMC_STACK_UNDERFLOW;
            break;
        }

        if (state->stack.size() + metrics.stack_change > evm_stack::limit)
        {
            result.status_code = EVMC_STACK_OVERFLOW;
            break;
        }

        switch (op)
        {
        case OP_STOP:
            goto exit;
        case OP_ADD:
            add(state->stack);
            break;
        case OP_MUL:
            mul(state->stack);
            break;
        case OP_SUB:
            sub(state->stack);
            break;
        case OP_DIV:
            div(state->stack);
            break;
        case OP_SDIV:
            sdiv(state->stack);
            break;
        case OP_MOD:
            mod(state->stack);
            break;
        case OP_SMOD:
            smod(state->stack);
            break;
        case OP_ADDMOD:
            addmod(state->stack);
            break;
        case OP_MULMOD:
            mulmod(state->stack);
            break;
        case OP_SIGNEXTEND:
            signextend(state->stack);
            break;

        case OP_LT:
            lt(state->stack);
            break;
        case OP_GT:
            gt(state->stack);
            break;
        case OP_SLT:
            slt(state->stack);
            break;
        case OP_SGT:
            sgt(state->stack);
            break;
        case OP_EQ:
            eq(state->stack);
            break;
        case OP_ISZERO:
            iszero(state->stack);
            break;
        case OP_AND:
            and_(state->stack);
            break;
        case OP_OR:
            or_(state->stack);
            break;
        case OP_XOR:
            xor_(state->stack);
            break;
        case OP_NOT:
            not_(state->stack);
            break;
        case OP_BYTE:
            byte(state->stack);
            break;
        case OP_SHL:
            shl(state->stack);
            break;
        case OP_SHR:
            shr(state->stack);
            break;
        case OP_SAR:
            sar(state->stack);
            break;

        case OP_MLOAD:
        {
            const auto status_code = mload(*state);
            if (status_code != EVMC_SUCCESS)
            {
                result.status_code = status_code;
                goto exit;
            }
            break;
        }
        case OP_MSTORE:
        {
            const auto status_code = mstore(*state);
            if (status_code != EVMC_SUCCESS)
            {
                result.status_code = status_code;
                goto exit;
            }
            break;
        }
        case OP_MSTORE8:
        {
            const auto status_code = mstore8(*state);
            if (status_code != EVMC_SUCCESS)
            {
                result.status_code = status_code;
                goto exit;
            }
            break;
        }
        case OP_PUSH1:
            pc = load_push<1>(*state, pc + 1, code_end);
            break;
        case OP_PUSH2:
            pc = load_push<2>(*state, pc + 1, code_end);
            break;
        case OP_PUSH3:
            pc = load_push<3>(*state, pc + 1, code_end);
            break;
        case OP_PUSH4:
            pc = load_push<4>(*state, pc + 1, code_end);
            break;
        case OP_PUSH5:
            pc = load_push<5>(*state, pc + 1, code_end);
            break;
        case OP_PUSH6:
            pc = load_push<6>(*state, pc + 1, code_end);
            break;
        case OP_PUSH7:
            pc = load_push<7>(*state, pc + 1, code_end);
            break;
        case OP_PUSH8:
            pc = load_push<8>(*state, pc + 1, code_end);
            break;
        case OP_PUSH9:
            pc = load_push<9>(*state, pc + 1, code_end);
            break;
        case OP_PUSH10:
            pc = load_push<10>(*state, pc + 1, code_end);
            break;
        case OP_PUSH11:
            pc = load_push<11>(*state, pc + 1, code_end);
            break;
        case OP_PUSH12:
            pc = load_push<12>(*state, pc + 1, code_end);
            break;
        case OP_PUSH13:
            pc = load_push<13>(*state, pc + 1, code_end);
            break;
        case OP_PUSH14:
            pc = load_push<14>(*state, pc + 1, code_end);
            break;
        case OP_PUSH15:
            pc = load_push<15>(*state, pc + 1, code_end);
            break;
        case OP_PUSH16:
            pc = load_push<16>(*state, pc + 1, code_end);
            break;
        case OP_PUSH17:
            pc = load_push<17>(*state, pc + 1, code_end);
            break;
        case OP_PUSH18:
            pc = load_push<18>(*state, pc + 1, code_end);
            break;
        case OP_PUSH19:
            pc = load_push<19>(*state, pc + 1, code_end);
            break;
        case OP_PUSH20:
            pc = load_push<20>(*state, pc + 1, code_end);
            break;
        case OP_PUSH21:
            pc = load_push<21>(*state, pc + 1, code_end);
            break;
        case OP_PUSH22:
            pc = load_push<22>(*state, pc + 1, code_end);
            break;
        case OP_PUSH23:
            pc = load_push<23>(*state, pc + 1, code_end);
            break;
        case OP_PUSH24:
            pc = load_push<24>(*state, pc + 1, code_end);
            break;
        case OP_PUSH25:
            pc = load_push<25>(*state, pc + 1, code_end);
            break;
        case OP_PUSH26:
            pc = load_push<26>(*state, pc + 1, code_end);
            break;
        case OP_PUSH27:
            pc = load_push<27>(*state, pc + 1, code_end);
            break;
        case OP_PUSH28:
            pc = load_push<28>(*state, pc + 1, code_end);
            break;
        case OP_PUSH29:
            pc = load_push<29>(*state, pc + 1, code_end);
            break;
        case OP_PUSH30:
            pc = load_push<30>(*state, pc + 1, code_end);
            break;
        case OP_PUSH31:
            pc = load_push<31>(*state, pc + 1, code_end);
            break;
        case OP_PUSH32:
            pc = load_push<32>(*state, pc + 1, code_end);
            break;
        }
    }

exit:
    return result;
}
}  // namespace evmone