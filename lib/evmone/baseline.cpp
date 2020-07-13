// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2020 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#include "baseline.hpp"
#include "baseline_table.hpp"
#include "execution_state.hpp"
#include "instructions.hpp"
#include <memory>

namespace evmone
{
namespace
{
struct BaselineExecutionState : ExecutionState
{
    evmc_status_code status = EVMC_SUCCESS;
    size_t output_offset = 0;
    size_t output_size = 0;
};

std::vector<bool> build_jumpdest_map(const uint8_t* code, size_t code_size)
{
    std::vector<bool> m(code_size);
    for (size_t i = 0; i < code_size; ++i)
    {
        const auto op = code[i];
        if (op == OP_JUMPDEST)
            m[i] = true;
        else if (op >= OP_PUSH1 && op <= OP_PUSH32)
            i += static_cast<size_t>(op - OP_PUSH1 + 1);
    }
    return m;
}

const uint8_t* op_jump(BaselineExecutionState& state, const std::vector<bool>& jumpdest_map,
    const uint8_t* code, size_t code_size) noexcept
{
    const auto dst = state.stack.pop();
    if (dst >= jumpdest_map.size() || !jumpdest_map[static_cast<size_t>(dst)])
    {
        state.status = EVMC_BAD_JUMP_DESTINATION;
        return code + code_size - 1;
    }

    return &code[static_cast<size_t>(dst) - 1];
}

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

inline evmc_status_code op_sstore(ExecutionState& state) noexcept
{
    // TODO: Implement static mode violation in analysis.
    if (state.msg->flags & EVMC_STATIC)
        return EVMC_STATIC_MODE_VIOLATION;

    if (state.rev >= EVMC_ISTANBUL)
    {
        if (state.gas_left <= 2300)
            return EVMC_OUT_OF_GAS;
    }

    const auto key = intx::be::store<evmc::bytes32>(state.stack.pop());
    const auto value = intx::be::store<evmc::bytes32>(state.stack.pop());
    auto status = state.host.set_storage(state.msg->destination, key, value);
    int cost = 0;
    switch (status)
    {
    case EVMC_STORAGE_UNCHANGED:
        if (state.rev >= EVMC_ISTANBUL)
            cost = 800;
        else if (state.rev == EVMC_CONSTANTINOPLE)
            cost = 200;
        else
            cost = 5000;
        break;
    case EVMC_STORAGE_MODIFIED:
        cost = 5000;
        break;
    case EVMC_STORAGE_MODIFIED_AGAIN:
        if (state.rev >= EVMC_ISTANBUL)
            cost = 800;
        else if (state.rev == EVMC_CONSTANTINOPLE)
            cost = 200;
        else
            cost = 5000;
        break;
    case EVMC_STORAGE_ADDED:
        cost = 20000;
        break;
    case EVMC_STORAGE_DELETED:
        cost = 5000;
        break;
    }
    if ((state.gas_left -= cost) < 0)
        return EVMC_OUT_OF_GAS;
    return EVMC_SUCCESS;
}


template <evmc_call_kind kind>
evmc_status_code op_call(BaselineExecutionState& state) noexcept
{
    auto gas = state.stack[0];
    const auto dst = intx::be::trunc<evmc::address>(state.stack[1]);
    auto value = state.stack[2];
    auto input_offset = state.stack[3];
    auto input_size = state.stack[4];
    auto output_offset = state.stack[5];
    auto output_size = state.stack[6];

    state.stack.pop();
    state.stack.pop();
    state.stack.pop();
    state.stack.pop();
    state.stack.pop();
    state.stack.pop();
    state.stack[0] = 0;

    if (!check_memory(state, input_offset, input_size))
        return EVMC_OUT_OF_GAS;

    if (!check_memory(state, output_offset, output_size))
        return EVMC_OUT_OF_GAS;


    auto msg = evmc_message{};
    msg.kind = kind;
    msg.flags = state.msg->flags;
    msg.value = intx::be::store<evmc::uint256be>(value);

    auto gas_left = state.gas_left;

    auto cost = 0;
    auto has_value = value != 0;

    if (has_value)
        cost += 9000;

    if constexpr (kind == EVMC_CALL)
    {
        if (has_value && state.msg->flags & EVMC_STATIC)
            return EVMC_STATIC_MODE_VIOLATION;

        if (has_value || state.rev < EVMC_SPURIOUS_DRAGON)
        {
            if (!state.host.account_exists(dst))
                cost += 25000;
        }
    }

    if ((gas_left -= cost) < 0)
        return EVMC_OUT_OF_GAS;

    msg.gas = std::numeric_limits<int64_t>::max();
    if (gas < msg.gas)
        msg.gas = static_cast<int64_t>(gas);

    if (state.rev >= EVMC_TANGERINE_WHISTLE)
        msg.gas = std::min(msg.gas, gas_left - gas_left / 64);
    else if (msg.gas > gas_left)
        return EVMC_OUT_OF_GAS;

    state.return_data.clear();

    state.gas_left -= cost;
    if (state.msg->depth >= 1024)
    {
        if (has_value)
            state.gas_left += 2300;  // Return unused stipend.
        if (state.gas_left < 0)
            return EVMC_OUT_OF_GAS;
        return EVMC_SUCCESS;
    }

    msg.destination = dst;
    msg.sender = state.msg->destination;
    msg.value = intx::be::store<evmc::uint256be>(value);

    if (size_t(input_size) > 0)
    {
        msg.input_data = &state.memory[size_t(input_offset)];
        msg.input_size = size_t(input_size);
    }

    msg.depth = state.msg->depth + 1;

    if (has_value)
    {
        const auto balance =
            intx::be::load<uint256>(state.host.get_balance(state.msg->destination));
        if (balance < value)
        {
            state.gas_left += 2300;  // Return unused stipend.
            if (state.gas_left < 0)
                return EVMC_OUT_OF_GAS;
            return EVMC_SUCCESS;
        }

        msg.gas += 2300;  // Add stipend.
    }

    auto result = state.host.call(msg);
    state.return_data.assign(result.output_data, result.output_size);


    state.stack[0] = result.status_code == EVMC_SUCCESS;

    if (auto copy_size = std::min(size_t(output_size), result.output_size); copy_size > 0)
        std::memcpy(&state.memory[size_t(output_offset)], result.output_data, copy_size);

    auto gas_used = msg.gas - result.gas_left;

    if (has_value)
        gas_used -= 2300;

    if ((state.gas_left -= gas_used) < 0)
        return EVMC_OUT_OF_GAS;
    return EVMC_SUCCESS;
}

evmc_status_code op_delegatecall(ExecutionState& state) noexcept
{
    auto gas = state.stack[0];
    const auto dst = intx::be::trunc<evmc::address>(state.stack[1]);
    auto input_offset = state.stack[2];
    auto input_size = state.stack[3];
    auto output_offset = state.stack[4];
    auto output_size = state.stack[5];

    state.stack.pop();
    state.stack.pop();
    state.stack.pop();
    state.stack.pop();
    state.stack.pop();
    state.stack[0] = 0;

    if (!check_memory(state, input_offset, input_size))
        return EVMC_OUT_OF_GAS;

    if (!check_memory(state, output_offset, output_size))
        return EVMC_OUT_OF_GAS;

    auto msg = evmc_message{};
    msg.kind = EVMC_DELEGATECALL;

    auto gas_left = state.gas_left;

    // TEST: Gas saturation for big gas values.
    msg.gas = std::numeric_limits<int64_t>::max();
    if (gas < msg.gas)
        msg.gas = static_cast<int64_t>(gas);

    if (state.rev >= EVMC_TANGERINE_WHISTLE)
        msg.gas = std::min(msg.gas, gas_left - gas_left / 64);
    else if (msg.gas > gas_left)  // TEST: gas_left vs state.gas_left.
        return EVMC_OUT_OF_GAS;

    if (state.msg->depth >= 1024)
        return EVMC_SUCCESS;

    msg.depth = state.msg->depth + 1;
    msg.flags = state.msg->flags;
    msg.destination = dst;
    msg.sender = state.msg->sender;
    msg.value = state.msg->value;

    if (size_t(input_size) > 0)
    {
        msg.input_data = &state.memory[size_t(input_offset)];
        msg.input_size = size_t(input_size);
    }

    auto result = state.host.call(msg);
    state.return_data.assign(result.output_data, result.output_size);

    state.stack[0] = result.status_code == EVMC_SUCCESS;

    if (const auto copy_size = std::min(size_t(output_size), result.output_size); copy_size > 0)
        std::memcpy(&state.memory[size_t(output_offset)], result.output_data, copy_size);

    auto gas_used = msg.gas - result.gas_left;

    if ((state.gas_left -= gas_used) < 0)
        return EVMC_OUT_OF_GAS;
    return EVMC_SUCCESS;
}

evmc_status_code op_staticcall(BaselineExecutionState& state) noexcept
{
    auto gas = state.stack[0];
    const auto dst = intx::be::trunc<evmc::address>(state.stack[1]);
    auto input_offset = state.stack[2];
    auto input_size = state.stack[3];
    auto output_offset = state.stack[4];
    auto output_size = state.stack[5];

    state.stack.pop();
    state.stack.pop();
    state.stack.pop();
    state.stack.pop();
    state.stack.pop();
    state.stack[0] = 0;

    if (!check_memory(state, input_offset, input_size))
        return EVMC_OUT_OF_GAS;

    if (!check_memory(state, output_offset, output_size))
        return EVMC_OUT_OF_GAS;

    if (state.msg->depth >= 1024)
        return EVMC_SUCCESS;

    auto msg = evmc_message{};
    msg.kind = EVMC_CALL;
    msg.flags |= EVMC_STATIC;

    msg.depth = state.msg->depth + 1;

    auto gas_left = state.gas_left;

    msg.gas = std::numeric_limits<int64_t>::max();
    if (gas < msg.gas)
        msg.gas = static_cast<int64_t>(gas);

    msg.gas = std::min(msg.gas, gas_left - gas_left / 64);

    msg.destination = dst;
    msg.sender = state.msg->destination;

    if (size_t(input_size) > 0)
    {
        msg.input_data = &state.memory[size_t(input_offset)];
        msg.input_size = size_t(input_size);
    }

    auto result = state.host.call(msg);
    state.return_data.assign(result.output_data, result.output_size);
    state.stack[0] = result.status_code == EVMC_SUCCESS;

    if (auto copy_size = std::min(size_t(output_size), result.output_size); copy_size > 0)
        std::memcpy(&state.memory[size_t(output_offset)], result.output_data, copy_size);

    auto gas_used = msg.gas - result.gas_left;

    if ((state.gas_left -= gas_used) < 0)
        return EVMC_OUT_OF_GAS;
    return EVMC_SUCCESS;
}

evmc_status_code op_create(BaselineExecutionState& state) noexcept
{
    if (state.msg->flags & EVMC_STATIC)
        return EVMC_STATIC_MODE_VIOLATION;

    auto endowment = state.stack[0];
    auto init_code_offset = state.stack[1];
    auto init_code_size = state.stack[2];

    state.stack.pop();
    state.stack.pop();
    state.stack[0] = 0;

    if (!check_memory(state, init_code_offset, init_code_size))
        return EVMC_OUT_OF_GAS;

    state.return_data.clear();

    if (state.msg->depth >= 1024)
        return EVMC_SUCCESS;

    if (endowment != 0)
    {
        const auto balance =
            intx::be::load<uint256>(state.host.get_balance(state.msg->destination));
        if (balance < endowment)
            return EVMC_SUCCESS;
    }

    auto msg = evmc_message{};

    msg.gas = state.gas_left;
    if (state.rev >= EVMC_TANGERINE_WHISTLE)
        msg.gas = msg.gas - msg.gas / 64;

    msg.kind = EVMC_CREATE;

    if (size_t(init_code_size) > 0)
    {
        msg.input_data = &state.memory[size_t(init_code_offset)];
        msg.input_size = size_t(init_code_size);
    }

    msg.sender = state.msg->destination;
    msg.depth = state.msg->depth + 1;
    msg.value = intx::be::store<evmc::uint256be>(endowment);

    auto result = state.host.call(msg);
    state.return_data.assign(result.output_data, result.output_size);
    if (result.status_code == EVMC_SUCCESS)
        state.stack[0] = intx::be::load<uint256>(result.create_address);

    if ((state.gas_left -= msg.gas - result.gas_left) < 0)
        return EVMC_OUT_OF_GAS;
    return EVMC_SUCCESS;
}

evmc_status_code op_create2(BaselineExecutionState& state) noexcept
{
    if (state.msg->flags & EVMC_STATIC)
        return EVMC_STATIC_MODE_VIOLATION;

    auto endowment = state.stack[0];
    auto init_code_offset = state.stack[1];
    auto init_code_size = state.stack[2];
    auto salt = state.stack[3];

    state.stack.pop();
    state.stack.pop();
    state.stack.pop();
    state.stack[0] = 0;

    if (!check_memory(state, init_code_offset, init_code_size))
        return EVMC_OUT_OF_GAS;

    auto salt_cost = num_words(static_cast<size_t>(init_code_size)) * 6;
    state.gas_left -= salt_cost;
    if (state.gas_left < 0)
        return EVMC_OUT_OF_GAS;

    state.return_data.clear();

    if (state.msg->depth >= 1024)
        return EVMC_SUCCESS;

    if (endowment != 0)
    {
        const auto balance =
            intx::be::load<uint256>(state.host.get_balance(state.msg->destination));
        if (balance < endowment)
            return EVMC_SUCCESS;
    }

    auto msg = evmc_message{};

    auto gas = state.gas_left;
    msg.gas = gas - gas / 64;

    msg.kind = EVMC_CREATE2;
    if (size_t(init_code_size) > 0)
    {
        msg.input_data = &state.memory[size_t(init_code_offset)];
        msg.input_size = size_t(init_code_size);
    }
    msg.sender = state.msg->destination;
    msg.depth = state.msg->depth + 1;
    msg.create2_salt = intx::be::store<evmc::bytes32>(salt);
    msg.value = intx::be::store<evmc::uint256be>(endowment);

    auto result = state.host.call(msg);
    state.return_data.assign(result.output_data, result.output_size);
    if (result.status_code == EVMC_SUCCESS)
        state.stack[0] = intx::be::load<uint256>(result.create_address);

    if ((state.gas_left -= msg.gas - result.gas_left) < 0)
        return EVMC_OUT_OF_GAS;
    return EVMC_SUCCESS;
}

template <evmc_status_code status_code>
inline void op_return(BaselineExecutionState& state) noexcept
{
    const auto offset = state.stack[0];
    const auto size = state.stack[1];

    if (!check_memory(state, offset, size))
    {
        state.status = EVMC_OUT_OF_GAS;
        return;
    }

    state.output_size = static_cast<size_t>(size);
    if (state.output_size != 0)
        state.output_offset = static_cast<size_t>(offset);
    state.status = status_code;
}

evmc_status_code op_selfdestruct(BaselineExecutionState& state) noexcept
{
    if (state.msg->flags & EVMC_STATIC)
        return EVMC_STATIC_MODE_VIOLATION;

    const auto addr = intx::be::trunc<evmc::address>(state.stack[0]);

    if (state.rev >= EVMC_TANGERINE_WHISTLE)
    {
        if (state.rev == EVMC_TANGERINE_WHISTLE || state.host.get_balance(state.msg->destination))
        {
            // After TANGERINE_WHISTLE apply additional cost of
            // sending value to a non-existing account.
            if (!state.host.account_exists(addr))
            {
                if ((state.gas_left -= 25000) < 0)
                    return EVMC_OUT_OF_GAS;
            }
        }
    }

    state.host.selfdestruct(state.msg->destination, addr);
    return EVMC_SUCCESS;
}

inline evmc_status_code check_requirements(
    const BaselineTraits* traits, BaselineExecutionState& state, uint8_t op) noexcept
{
    const auto metrics = traits[op];

    if (metrics.gas_cost < 0)
        return EVMC_UNDEFINED_INSTRUCTION;

    if ((state.gas_left -= metrics.gas_cost) < 0)
        return EVMC_OUT_OF_GAS;

    const auto cond = state.stack.size() + metrics.stack_check;

    if (cond < 0 || cond > evm_stack::limit)
        return metrics.stack_check < 0 ? EVMC_STACK_UNDERFLOW : EVMC_STACK_OVERFLOW;

    return EVMC_SUCCESS;
}
}  // namespace

evmc_result baseline_execute([[maybe_unused]] evmc_vm* vm, const evmc_host_interface* host,
    evmc_host_context* ctx, evmc_revision rev, const evmc_message* msg, const uint8_t* code,
    size_t code_size) noexcept
{
    const auto& op_tbl = baseline_table[rev];

    const auto jumpdest_map = build_jumpdest_map(code, code_size);

    auto state = std::make_unique<BaselineExecutionState>();
    state->msg = msg;
    state->host = evmc::HostContext{*host, ctx};
    state->gas_left = msg->gas;
    state->rev = rev;

    const auto code_end = code + code_size;
    for (auto pc = code; pc != code_end; ++pc)
    {
        const auto op = *pc;

        const auto status = check_requirements(op_tbl.data(), *state, op);
        if (status != EVMC_SUCCESS)
        {
            state->status = status;
            goto exit;
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
        case OP_EXP:
        {
            const auto status_code = exp(*state);
            if (status_code != EVMC_SUCCESS)
            {
                state->status = status_code;
                goto exit;
            }
            break;
        }
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

        case OP_SHA3:
        {
            const auto status_code = sha3(*state);
            if (status_code != EVMC_SUCCESS)
            {
                state->status = status_code;
                goto exit;
            }
            break;
        }

        case OP_ADDRESS:
            address(*state);
            break;
        case OP_BALANCE:
            balance(*state);
            break;
        case OP_ORIGIN:
            origin(*state);
            break;
        case OP_CALLER:
            caller(*state);
            break;
        case OP_CALLVALUE:
            callvalue(*state);
            break;
        case OP_CALLDATALOAD:
            calldataload(*state);
            break;
        case OP_CALLDATASIZE:
            calldatasize(*state);
            break;
        case OP_CALLDATACOPY:
        {
            const auto status_code = calldatacopy(*state);
            if (status_code != EVMC_SUCCESS)
            {
                state->status = status_code;
                goto exit;
            }
            break;
        }
        case OP_CODESIZE:
            state->stack.push(code_size);
            break;
        case OP_CODECOPY:
        {
            const auto status_code = codecopy(*state, code, code_size);
            if (status_code != EVMC_SUCCESS)
            {
                state->status = status_code;
                goto exit;
            }
            break;
        }
        case OP_GASPRICE:
            gasprice(*state);
            break;
        case OP_EXTCODESIZE:
            extcodesize(*state);
            break;
        case OP_EXTCODECOPY:
        {
            const auto status_code = extcodecopy(*state);
            if (status_code != EVMC_SUCCESS)
            {
                state->status = status_code;
                goto exit;
            }
            break;
        }
        case OP_RETURNDATASIZE:
            returndatasize(*state);
            break;
        case OP_RETURNDATACOPY:
        {
            const auto status_code = returndatacopy(*state);
            if (status_code != EVMC_SUCCESS)
            {
                state->status = status_code;
                goto exit;
            }
            break;
        }
        case OP_EXTCODEHASH:
            extcodehash(*state);
            break;

        case OP_BLOCKHASH:
            blockhash(*state);
            break;
        case OP_COINBASE:
            coinbase(*state);
            break;
        case OP_TIMESTAMP:
            timestamp(*state);
            break;
        case OP_NUMBER:
            number(*state);
            break;
        case OP_DIFFICULTY:
            difficulty(*state);
            break;
        case OP_GASLIMIT:
            gaslimit(*state);
            break;
        case OP_CHAINID:
            chainid(*state);
            break;
        case OP_SELFBALANCE:
            selfbalance(*state);
            break;

        case OP_POP:
            pop(state->stack);
            break;
        case OP_MLOAD:
        {
            const auto status_code = mload(*state);
            if (status_code != EVMC_SUCCESS)
            {
                state->status = status_code;
                goto exit;
            }
            break;
        }
        case OP_MSTORE:
        {
            const auto status_code = mstore(*state);
            if (status_code != EVMC_SUCCESS)
            {
                state->status = status_code;
                goto exit;
            }
            break;
        }
        case OP_MSTORE8:
        {
            const auto status_code = mstore8(*state);
            if (status_code != EVMC_SUCCESS)
            {
                state->status = status_code;
                goto exit;
            }
            break;
        }

        case OP_JUMP:
            pc = op_jump(*state, jumpdest_map, code, code_size);
            break;
        case OP_JUMPI:
            if (state->stack[1] != 0)
                pc = op_jump(*state, jumpdest_map, code, code_size);
            else
                state->stack.pop();
            state->stack.pop();
            break;

        case OP_PC:
            state->stack.push(pc - code);
            break;
        case OP_MSIZE:
            state->stack.push(state->memory.size());
            break;
        case OP_SLOAD:
            sload(*state);
            break;
        case OP_SSTORE:
        {
            const auto status_code = op_sstore(*state);
            if (status_code != EVMC_SUCCESS)
            {
                state->status = status_code;
                goto exit;
            }
            break;
        }
        case OP_GAS:
            state->stack.push(state->gas_left);
            break;
        case OP_JUMPDEST:
            break;

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

        case OP_DUP1:
            dup<OP_DUP1>(state->stack);
            break;
        case OP_DUP2:
            dup<OP_DUP2>(state->stack);
            break;
        case OP_DUP3:
            dup<OP_DUP3>(state->stack);
            break;
        case OP_DUP4:
            dup<OP_DUP4>(state->stack);
            break;
        case OP_DUP5:
            dup<OP_DUP5>(state->stack);
            break;
        case OP_DUP6:
            dup<OP_DUP6>(state->stack);
            break;
        case OP_DUP7:
            dup<OP_DUP7>(state->stack);
            break;
        case OP_DUP8:
            dup<OP_DUP8>(state->stack);
            break;
        case OP_DUP9:
            dup<OP_DUP9>(state->stack);
            break;
        case OP_DUP10:
            dup<OP_DUP10>(state->stack);
            break;
        case OP_DUP11:
            dup<OP_DUP11>(state->stack);
            break;
        case OP_DUP12:
            dup<OP_DUP12>(state->stack);
            break;
        case OP_DUP13:
            dup<OP_DUP13>(state->stack);
            break;
        case OP_DUP14:
            dup<OP_DUP14>(state->stack);
            break;
        case OP_DUP15:
            dup<OP_DUP15>(state->stack);
            break;
        case OP_DUP16:
            dup<OP_DUP16>(state->stack);
            break;

        case OP_SWAP1:
            swap<OP_SWAP1>(state->stack);
            break;
        case OP_SWAP2:
            swap<OP_SWAP2>(state->stack);
            break;
        case OP_SWAP3:
            swap<OP_SWAP3>(state->stack);
            break;
        case OP_SWAP4:
            swap<OP_SWAP4>(state->stack);
            break;
        case OP_SWAP5:
            swap<OP_SWAP5>(state->stack);
            break;
        case OP_SWAP6:
            swap<OP_SWAP6>(state->stack);
            break;
        case OP_SWAP7:
            swap<OP_SWAP7>(state->stack);
            break;
        case OP_SWAP8:
            swap<OP_SWAP8>(state->stack);
            break;
        case OP_SWAP9:
            swap<OP_SWAP9>(state->stack);
            break;
        case OP_SWAP10:
            swap<OP_SWAP10>(state->stack);
            break;
        case OP_SWAP11:
            swap<OP_SWAP11>(state->stack);
            break;
        case OP_SWAP12:
            swap<OP_SWAP12>(state->stack);
            break;
        case OP_SWAP13:
            swap<OP_SWAP13>(state->stack);
            break;
        case OP_SWAP14:
            swap<OP_SWAP14>(state->stack);
            break;
        case OP_SWAP15:
            swap<OP_SWAP15>(state->stack);
            break;
        case OP_SWAP16:
            swap<OP_SWAP16>(state->stack);
            break;

        case OP_LOG0:
        {
            const auto status_code = log(*state, 0);
            if (status_code != EVMC_SUCCESS)
            {
                state->status = status_code;
                goto exit;
            }
            break;
        }
        case OP_LOG1:
        {
            const auto status_code = log(*state, 1);
            if (status_code != EVMC_SUCCESS)
            {
                state->status = status_code;
                goto exit;
            }
            break;
        }
        case OP_LOG2:
        {
            const auto status_code = log(*state, 2);
            if (status_code != EVMC_SUCCESS)
            {
                state->status = status_code;
                goto exit;
            }
            break;
        }
        case OP_LOG3:
        {
            const auto status_code = log(*state, 3);
            if (status_code != EVMC_SUCCESS)
            {
                state->status = status_code;
                goto exit;
            }
            break;
        }
        case OP_LOG4:
        {
            const auto status_code = log(*state, 4);
            if (status_code != EVMC_SUCCESS)
            {
                state->status = status_code;
                goto exit;
            }
            break;
        }

        case OP_CREATE:
        {
            const auto status_code = op_create(*state);
            if (status_code != EVMC_SUCCESS)
            {
                state->status = status_code;
                goto exit;
            }
            break;
        }
        case OP_CALL:
        {
            const auto status_code = op_call<EVMC_CALL>(*state);
            if (status_code != EVMC_SUCCESS)
            {
                state->status = status_code;
                goto exit;
            }
            break;
        }
        case OP_CALLCODE:
        {
            const auto status_code = op_call<EVMC_CALLCODE>(*state);
            if (status_code != EVMC_SUCCESS)
            {
                state->status = status_code;
                goto exit;
            }
            break;
        }
        case OP_RETURN:
            op_return<EVMC_SUCCESS>(*state);
            goto exit;
        case OP_DELEGATECALL:
        {
            const auto status_code = op_delegatecall(*state);
            if (status_code != EVMC_SUCCESS)
            {
                state->status = status_code;
                goto exit;
            }
            break;
        }
        case OP_STATICCALL:
        {
            const auto status_code = op_staticcall(*state);
            if (status_code != EVMC_SUCCESS)
            {
                state->status = status_code;
                goto exit;
            }
            break;
        }
        case OP_CREATE2:
        {
            const auto status_code = op_create2(*state);
            if (status_code != EVMC_SUCCESS)
            {
                state->status = status_code;
                goto exit;
            }
            break;
        }
        case OP_REVERT:
            op_return<EVMC_REVERT>(*state);
            goto exit;
        case OP_INVALID:
            state->status = EVMC_INVALID_INSTRUCTION;
            goto exit;
        case OP_SELFDESTRUCT:
            state->status = op_selfdestruct(*state);
            goto exit;
        default:
            // TODO: Should not happen.
            state->status = EVMC_INTERNAL_ERROR;
            goto exit;
        }
    }

exit:
    const auto gas_left =
        (state->status == EVMC_SUCCESS || state->status == EVMC_REVERT) ? state->gas_left : 0;

    return evmc::make_result(
        state->status, gas_left, &state->memory[state->output_offset], state->output_size);
}
}  // namespace evmone