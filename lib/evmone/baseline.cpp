// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2020 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#include "baseline.hpp"
#include "analysis.hpp"
#include "instructions.hpp"
#include <memory>

namespace evmone
{
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
        }
    }

exit:
    return result;
}
}  // namespace evmone