// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "common/types.h"

class UCP;

// Activities / UDS (Universal Data System) definitions, as packed in a
// title's uds00.ucp: enum_definition.json, events_definition.json,
// stats_definition.json and stats_extraction.json.
struct UdsEnum {
    s64 id = 0;
    std::string group;     // "definitionGroup"
    std::string source_id; // "sourceId"
    std::vector<std::string> values;
};

struct UdsEventProperty {
    std::string path; // "$._trophy_id"
    std::string data_type;
};

struct UdsEvent {
    std::string name;
    std::string type;
    std::string group;
    std::vector<UdsEventProperty> properties;
};

struct UdsStat {
    s64 id = 0;
    std::string name;
    std::string source_id;
    std::string group;
    std::string data_type;
    std::string aggregation;
    std::string enum_source_id; // when the stat is a set of enum values
};

struct UdsRule {
    s64 id = 0;
    std::string source_id;
    std::string group;
    std::string event;     // condition.eventName
    std::string condition; // "$._outcome == completed", empty when unconditional
    std::string input;     // action.input
    s64 output_stat_id = 0;
    std::string output_stat_name;
};

struct UdsDefinitions {
    std::string np_comm_id;
    s64 configuration_id = 0;
    s64 enum_revision = 0;
    s64 stats_revision = 0;
    s64 extraction_revision = 0;
    std::vector<UdsEnum> enums;
    std::vector<UdsEvent> events;
    std::vector<UdsStat> stats;
    std::vector<UdsRule> rules;
    std::vector<std::string> problems; // files that were missing or failed to parse

    [[nodiscard]] bool IsEmpty() const {
        return enums.empty() && events.empty() && stats.empty() && rules.empty();
    }

    // Each returns false when the JSON does not parse.
    bool ParseEnums(std::string_view json_text);
    bool ParseEvents(std::string_view json_text);
    bool ParseStats(std::string_view json_text);
    bool ParseRules(std::string_view json_text);

    // Reads whichever of the four definition files the container holds.
    bool LoadFromUcp(const UCP& ucp);
};
