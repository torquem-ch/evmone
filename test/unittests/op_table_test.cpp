// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2019 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#include <evmc/instructions.h>
#include <evmone/analysis.hpp>
#include <evmone/baseline_table.hpp>
#include <gtest/gtest.h>
#include <test/utils/bytecode.hpp>

TEST(op_table, compare_with_evmc_instruction_tables)
{
    for (int r = EVMC_FRONTIER; r <= EVMC_MAX_REVISION; ++r)
    {
        const auto rev = static_cast<evmc_revision>(r);
        const auto& evmone_tbl = evmone::get_op_table(rev);
        const auto* evmc_tbl = evmc_get_instruction_metrics_table(rev);

        for (size_t i = 0; i < evmone_tbl.size(); ++i)
        {
            const auto& metrics = evmone_tbl[i];
            const auto& ref_metrics = evmc_tbl[i];

            const auto case_descr = [rev](size_t opcode) {
                auto case_descr_str = std::ostringstream{};
                case_descr_str << "opcode " << to_name(evmc_opcode(opcode), rev);
                case_descr_str << " on revision " << rev;
                return case_descr_str.str();
            };

            EXPECT_EQ(metrics.gas_cost, ref_metrics.gas_cost) << case_descr(i);
            EXPECT_EQ(metrics.stack_req, ref_metrics.stack_height_required) << case_descr(i);
            EXPECT_EQ(metrics.stack_change, ref_metrics.stack_height_change) << case_descr(i);
        }
    }
}

TEST(op_table, compare_baseline_gas_costs_with_evmc_instruction_tables)
{
    for (int r = EVMC_FRONTIER; r <= EVMC_MAX_REVISION; ++r)
    {
        const auto rev = static_cast<evmc_revision>(r);
        const auto& evmone_tbl = evmone::baseline_table[rev];
        const auto* evmc_tbl = evmc_get_instruction_metrics_table(rev);
        const auto* evmc_names = evmc_get_instruction_names_table(rev);

        for (size_t i = 0; i < evmone_tbl.size(); ++i)
        {
            const auto& metrics = evmone_tbl[i];
            const auto& ref_metrics = evmc_tbl[i];
            const auto undefined = evmc_names[i] == nullptr;

            const auto case_descr = [rev](size_t opcode) {
                auto case_descr_str = std::ostringstream{};
                case_descr_str << "opcode " << to_name(evmc_opcode(opcode), rev);
                case_descr_str << " on revision " << rev;
                return case_descr_str.str();
            };

            if (undefined)
            {
                EXPECT_EQ(metrics.gas_cost, -1) << case_descr(i);
            }
            else
            {
                EXPECT_EQ(metrics.gas_cost, ref_metrics.gas_cost) << case_descr(i);
            }
        }
    }
}

TEST(op_table, compare_baseline_stack_traits_with_evmc_instruction_tables)
{
    constexpr auto rev = EVMC_MAX_REVISION;
    const auto* evmc_tbl = evmc_get_instruction_metrics_table(rev);

    for (size_t i = 0; i < 256; ++i)
    {
        const auto case_descr = [](size_t opcode) {
            auto case_descr_str = std::ostringstream{};
            case_descr_str << "opcode " << to_name(evmc_opcode(opcode), rev);
            case_descr_str << " on revision " << rev;
            return case_descr_str.str();
        };

        EXPECT_EQ(evmone::stack_traits[i].required, evmc_tbl[i].stack_height_required)
            << case_descr(i);

        EXPECT_EQ(evmone::stack_traits[i].change, evmc_tbl[i].stack_height_change) << case_descr(i);
    }
}
