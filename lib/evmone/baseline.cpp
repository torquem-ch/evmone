// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2020 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#include "baseline.hpp"
#include "analysis.hpp"
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

    for (size_t pc = 0; pc != code_size; ++pc)
    {
        const auto op = code[pc];
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
        }
    }

exit:
    return result;
}
}  // namespace evmone