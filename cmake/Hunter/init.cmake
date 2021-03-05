# evmone: Fast Ethereum Virtual Machine implementation
# Copyright 2018-2020 The evmone Authors.
# SPDX-License-Identifier: Apache-2.0

set(HUNTER_CONFIGURATION_TYPES Release CACHE STRING "Build type of Hunter packages")

include(HunterGate)

HunterGate(
    URL https://github.com/cpp-pm/hunter/archive/v0.23.288.tar.gz
    SHA1 6c9b2bc606d86ae31f96a62fc68f0a593024815b
    LOCAL
)
