#pragma once

#include <gff/GFFFile.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace neoqst {

using neogff::GffFile;
using neogff::GffList;
using neogff::GffStruct;
using neogff::UInt32;

inline constexpr std::size_t kMaxQstTasks = 128;
inline constexpr std::size_t kMaxQstTaskGroups = 128;
inline constexpr std::int32_t kMaxQstIdentifier = 32767;
inline constexpr std::int32_t kQstNoNextGroup = -1;
inline constexpr std::int32_t kQstCompleteQuest = -2;

struct QstInsertResult {
    std::size_t index = 0;
    std::int32_t identifier = 0;
    UInt32 structTypeId = 0;
};

struct QstDeleteResult {
    std::optional<std::size_t> nextSelection;
    std::int32_t identifier = 0;
    std::size_t repairedReferences = 0;
};

void initializeNewQst(GffFile& qst, const std::filesystem::path& filename = {});
void validateQst(const GffFile& qst);

GffList& requireQstTaskList(GffFile& qst);
const GffList& requireQstTaskList(const GffFile& qst);
GffList& requireQstTaskGroupList(GffFile& qst);
const GffList& requireQstTaskGroupList(const GffFile& qst);

// Jade initializes a missing Identifier to zero.
std::int32_t qstEffectiveIdentifier(const GffStruct& structure);
std::int32_t suggestQstTaskIdentifier(const GffFile& qst);
std::int32_t suggestQstGroupIdentifier(const GffFile& qst);

void changeQstTaskIdentifier(GffFile& qst, std::size_t taskIndex, std::int32_t newIdentifier);
void changeQstGroupIdentifier(GffFile& qst, std::size_t groupIndex, std::int32_t newIdentifier);

QstInsertResult appendQstTask(GffFile& qst,
                              std::optional<std::int32_t> requestedIdentifier = std::nullopt);
QstDeleteResult deleteQstTask(GffFile& qst, std::size_t taskIndex);

QstInsertResult appendQstTaskGroup(GffFile& qst,
                                   std::optional<std::int32_t> requestedIdentifier = std::nullopt);
QstDeleteResult deleteQstTaskGroup(GffFile& qst, std::size_t groupIndex);

std::vector<std::int32_t> qstGroupTaskIndices(const GffFile& qst, std::size_t groupIndex);
void replaceQstGroupTaskIndices(GffFile& qst,
                                std::size_t groupIndex,
                                const std::vector<std::int32_t>& taskIndices);

void setQstJadeStringRef(GffStruct& structure,
                         const std::string& label,
                         UInt32 subtype,
                         std::optional<UInt32> strref,
                         bool removeWhenUnset = false);

std::optional<UInt32> qstJadeStringRef(const GffStruct& structure, const std::string& label);
std::optional<std::int32_t> qstInt(const GffStruct& structure, const std::string& label);
void setQstInt(GffStruct& structure, const std::string& label, std::int32_t value);
void setQstOptionalInt(GffStruct& structure,
                       const std::string& label,
                       std::optional<std::int32_t> value);

std::optional<UInt32> qstDword(const GffStruct& structure, const std::string& label);
void setQstDword(GffStruct& structure, const std::string& label, UInt32 value);
void setQstOptionalDword(GffStruct& structure,
                         const std::string& label,
                         std::optional<UInt32> value);

std::optional<std::string> qstResRef(const GffStruct& structure, const std::string& label);
void setQstOptionalResRef(GffStruct& structure,
                          const std::string& label,
                          const std::optional<std::string>& value);

} // namespace neoqst
