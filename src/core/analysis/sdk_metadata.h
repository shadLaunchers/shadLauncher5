// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Core::Analysis {

enum class AbiType { U32, U64, I32, I64, F32, F64, Ptr, Void };

struct FunctionSignature {
    std::string_view name;
    AbiType return_type = AbiType::Void;
    std::vector<AbiType> params;
    bool variadic = false;
};

const std::vector<FunctionSignature>& SeedSignatures();
const FunctionSignature* FindSignature(std::string_view function_name);

std::string_view AbiTypeName(AbiType type);
std::string FormatPrototype(const FunctionSignature& sig);
std::string InferCategory(std::string_view library);

} // namespace Core::Analysis
