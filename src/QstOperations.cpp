#include "QstOperations.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace neoqst {

using namespace neogff;

namespace {

std::string normalizedType(std::string value) {
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
        return ch == 0 || std::isspace(ch) != 0;
    }), value.end());
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

void requireEditableQst(const GffFile& qst) {
    if (!qst.loaded()) throw std::runtime_error("No QST file is loaded.");
    if (qst.isGff4()) throw std::runtime_error("NeoQST requires a GFF3 QST document.");
    if (normalizedType(qst.filetype()) != "QST") {
        throw std::runtime_error("The loaded GFF is not a Jade Empire QST document.");
    }
}

GffList& requireRootList(GffFile& qst, const char* label) {
    requireEditableQst(qst);
    GffField* field = qst.GetFieldByLabel(label);
    if (field == nullptr || field->fieldtype != FIELD_TYPE_LIST) {
        throw std::runtime_error(std::string("The QST does not contain a canonical ") + label + " list.");
    }
    return static_cast<GffList&>(*field);
}

const GffList& requireRootList(const GffFile& qst, const char* label) {
    requireEditableQst(qst);
    const GffField* field = qst.GetFieldByLabel(label);
    if (field == nullptr || field->fieldtype != FIELD_TYPE_LIST) {
        throw std::runtime_error(std::string("The QST does not contain a canonical ") + label + " list.");
    }
    return static_cast<const GffList&>(*field);
}

GffStruct& requireStruct(GffList& list, std::size_t index, const char* role) {
    if (index >= list.count()) {
        throw std::out_of_range(std::string(role) + " index " + std::to_string(index) + " is outside the list.");
    }
    GffStruct* structure = list.GetStruct(index);
    if (structure == nullptr) throw std::runtime_error(std::string("The selected ") + role + " is missing its GFF struct.");
    return *structure;
}

const GffStruct& requireStruct(const GffList& list, std::size_t index, const char* role) {
    if (index >= list.count()) {
        throw std::out_of_range(std::string(role) + " index " + std::to_string(index) + " is outside the list.");
    }
    const GffStruct* structure = list.GetStruct(index);
    if (structure == nullptr) throw std::runtime_error(std::string("The selected ") + role + " is missing its GFF struct.");
    return *structure;
}

std::int32_t checkedFallbackIndex(std::size_t index) {
    if (index > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        throw std::out_of_range("The QST list index cannot be represented as a signed 32-bit identifier.");
    }
    return static_cast<std::int32_t>(index);
}

std::unordered_set<std::int32_t> collectIdentifiers(const GffList& list) {
    std::unordered_set<std::int32_t> identifiers;
    for (std::size_t index = 0; index < list.count(); ++index) {
        const std::int32_t identifier = qstEffectiveIdentifier(requireStruct(list, index, "QST item"), index);
        if (identifier < 0) throw std::runtime_error("QST identifiers must be non-negative.");
        if (!identifiers.insert(identifier).second) {
            throw std::runtime_error("The QST contains duplicate identifier " + std::to_string(identifier) + ".");
        }
    }
    return identifiers;
}

std::int32_t firstAvailableIdentifier(const std::unordered_set<std::int32_t>& identifiers) {
    std::int32_t maximum = -1;
    for (const std::int32_t value : identifiers) maximum = std::max(maximum, value);
    if (maximum < std::numeric_limits<std::int32_t>::max()) {
        const std::int32_t candidate = maximum + 1;
        if (identifiers.find(candidate) == identifiers.end()) return candidate;
    }
    for (std::int32_t candidate = 0; candidate < std::numeric_limits<std::int32_t>::max(); ++candidate) {
        if (identifiers.find(candidate) == identifiers.end()) return candidate;
    }
    throw std::runtime_error("The QST has no available non-negative 32-bit identifiers.");
}

std::optional<std::size_t> nextSelection(std::size_t removedIndex, std::size_t remaining) {
    if (remaining == 0) return std::nullopt;
    return std::min(removedIndex, remaining - 1u);
}

GffList* taskIndexList(GffStruct& group) {
    GffField* field = group.GetFieldByLabel("TaskIndexList");
    if (field == nullptr) return nullptr;
    if (field->fieldtype != FIELD_TYPE_LIST) {
        throw std::runtime_error("A QST task group has a TaskIndexList field with the wrong GFF type.");
    }
    return &static_cast<GffList&>(*field);
}

const GffList* taskIndexList(const GffStruct& group) {
    const GffField* field = group.GetFieldByLabel("TaskIndexList");
    if (field == nullptr) return nullptr;
    if (field->fieldtype != FIELD_TYPE_LIST) {
        throw std::runtime_error("A QST task group has a TaskIndexList field with the wrong GFF type.");
    }
    return &static_cast<const GffList&>(*field);
}

std::int32_t requireTaskReference(const GffStruct& item) {
    const GffField* field = item.GetFieldByLabel("Task");
    if (field == nullptr || field->fieldtype != FIELD_TYPE_INT) {
        throw std::runtime_error("A QST TaskIndexList item does not contain an INT Task field.");
    }
    return static_cast<const GffIntField&>(*field).value;
}

void setIdentifier(GffStruct& structure, std::int32_t value) {
    setQstInt(structure, "Identifier", value);
}

} // namespace

void initializeNewQst(GffFile& qst, const std::filesystem::path& filename) {
    qst.NewFile("QST ", filename);
    GffStruct* root = qst.root();
    if (root == nullptr) throw std::runtime_error("Unable to create the QST root struct.");
    root->AddField(std::make_unique<GffJadeStringRefField>("QuestDescription", 4u, 0xFFFFFFFFu));
    root->AddField(std::make_unique<GffJadeStringRefField>("QuestName", 4u, 0xFFFFFFFFu));
    root->AddField(std::make_unique<GffList>("TaskList"));
    root->AddField(std::make_unique<GffList>("TaskGroupList"));
    qst.dirty(true);
}

void validateQst(const GffFile& qst) {
    requireEditableQst(qst);
    (void)collectIdentifiers(requireQstTaskList(qst));
    (void)collectIdentifiers(requireQstTaskGroupList(qst));

    const GffList& taskList = requireQstTaskList(qst);
    const GffList& groupList = requireQstTaskGroupList(qst);
    for (std::size_t index = 0; index < taskList.count(); ++index) {
        const GffStruct& task = requireStruct(taskList, index, "task");
        const auto next = qstInt(task, "NextTaskGroup");
        if (next && *next >= 0 && static_cast<std::size_t>(*next) >= groupList.count()) {
            throw std::runtime_error("Task " + std::to_string(qstEffectiveIdentifier(task, index)) +
                                     " references missing task-group index " + std::to_string(*next) + ".");
        }
    }
    for (std::size_t index = 0; index < groupList.count(); ++index) {
        for (const std::int32_t taskIndex : qstGroupTaskIndices(qst, index)) {
            if (taskIndex < 0 || static_cast<std::size_t>(taskIndex) >= taskList.count()) {
                throw std::runtime_error("Task group " +
                                         std::to_string(qstEffectiveIdentifier(requireStruct(groupList, index, "task group"), index)) +
                                         " references missing task-list index " + std::to_string(taskIndex) + ".");
            }
        }
    }
}

GffList& requireQstTaskList(GffFile& qst) { return requireRootList(qst, "TaskList"); }
const GffList& requireQstTaskList(const GffFile& qst) { return requireRootList(qst, "TaskList"); }
GffList& requireQstTaskGroupList(GffFile& qst) { return requireRootList(qst, "TaskGroupList"); }
const GffList& requireQstTaskGroupList(const GffFile& qst) { return requireRootList(qst, "TaskGroupList"); }

std::int32_t qstEffectiveIdentifier(const GffStruct& structure, std::size_t fallbackIndex) {
    const GffField* field = structure.GetFieldByLabel("Identifier");
    if (field == nullptr) return checkedFallbackIndex(fallbackIndex);
    if (field->fieldtype != FIELD_TYPE_INT) throw std::runtime_error("A QST Identifier field has the wrong GFF type.");
    return static_cast<const GffIntField&>(*field).value;
}

std::int32_t suggestQstTaskIdentifier(const GffFile& qst) {
    return firstAvailableIdentifier(collectIdentifiers(requireQstTaskList(qst)));
}

std::int32_t suggestQstGroupIdentifier(const GffFile& qst) {
    return firstAvailableIdentifier(collectIdentifiers(requireQstTaskGroupList(qst)));
}

void changeQstTaskIdentifier(GffFile& qst, std::size_t taskIndex, std::int32_t newIdentifier) {
    if (newIdentifier < 0) throw std::invalid_argument("QST task identifiers must be non-negative.");
    GffList& tasks = requireQstTaskList(qst);
    GffStruct& task = requireStruct(tasks, taskIndex, "task");
    const std::int32_t oldIdentifier = qstEffectiveIdentifier(task, taskIndex);
    if (oldIdentifier == newIdentifier) return;
    const auto identifiers = collectIdentifiers(tasks);
    if (identifiers.find(newIdentifier) != identifiers.end()) {
        throw std::runtime_error("Task identifier " + std::to_string(newIdentifier) + " is already used.");
    }
    setIdentifier(task, newIdentifier);
    qst.dirty(true);
}

void changeQstGroupIdentifier(GffFile& qst, std::size_t groupIndex, std::int32_t newIdentifier) {
    if (newIdentifier < 0) throw std::invalid_argument("QST task-group identifiers must be non-negative.");
    GffList& groups = requireQstTaskGroupList(qst);
    GffStruct& group = requireStruct(groups, groupIndex, "task group");
    const std::int32_t oldIdentifier = qstEffectiveIdentifier(group, groupIndex);
    if (oldIdentifier == newIdentifier) return;
    const auto identifiers = collectIdentifiers(groups);
    if (identifiers.find(newIdentifier) != identifiers.end()) {
        throw std::runtime_error("Task-group identifier " + std::to_string(newIdentifier) + " is already used.");
    }
    setIdentifier(group, newIdentifier);
    qst.dirty(true);
}

QstInsertResult appendQstTask(GffFile& qst, std::optional<std::int32_t> requestedIdentifier) {
    GffList& tasks = requireQstTaskList(qst);
    const auto existing = collectIdentifiers(tasks);
    const std::int32_t identifier = requestedIdentifier.value_or(firstAvailableIdentifier(existing));
    if (identifier < 0) throw std::invalid_argument("QST task identifiers must be non-negative.");
    if (existing.find(identifier) != existing.end()) {
        throw std::runtime_error("Task identifier " + std::to_string(identifier) + " is already used.");
    }
    const std::size_t index = tasks.count();
    auto task = std::make_unique<GffStruct>();
    task->typeid_ = 0u;
    task->AddField(std::make_unique<GffJadeStringRefField>("TaskName", 4u, 0xFFFFFFFFu));
    task->AddField(std::make_unique<GffJadeStringRefField>("TaskSummary", 4u, 0xFFFFFFFFu));
    task->AddField(std::make_unique<GffIntField>("Identifier", identifier));
    task->AddField(std::make_unique<GffIntField>("NextTaskGroup", -1));
    task->AddField(std::make_unique<GffIntField>("NotifyActive", 1));
    task->AddField(std::make_unique<GffIntField>("NotifyComplete", 1));
    tasks.AddStruct(std::move(task));
    qst.dirty(true);
    return QstInsertResult{index, identifier, 0u};
}

QstDeleteResult deleteQstTask(GffFile& qst, std::size_t taskIndex) {
    GffList& tasks = requireQstTaskList(qst);
    const std::int32_t identifier = qstEffectiveIdentifier(requireStruct(tasks, taskIndex, "task"), taskIndex);
    std::size_t repaired = 0;
    GffList& groups = requireQstTaskGroupList(qst);
    for (std::size_t groupIndex = 0; groupIndex < groups.count(); ++groupIndex) {
        GffList* refs = taskIndexList(requireStruct(groups, groupIndex, "task group"));
        if (refs == nullptr) continue;
        for (std::size_t refIndex = refs->count(); refIndex > 0; --refIndex) {
            GffStruct& reference = requireStruct(*refs, refIndex - 1, "task reference");
            GffField* field = reference.GetFieldByLabel("Task");
            if (field == nullptr || field->fieldtype != FIELD_TYPE_INT) {
                throw std::runtime_error("A QST task reference has the wrong GFF type.");
            }
            auto& value = static_cast<GffIntField&>(*field).value;
            if (value == static_cast<std::int32_t>(taskIndex)) {
                refs->DeleteStruct(static_cast<UInt32>(refIndex - 1));
                ++repaired;
            } else if (value > static_cast<std::int32_t>(taskIndex)) {
                --value;
                ++repaired;
            }
        }
    }
    tasks.DeleteStruct(static_cast<UInt32>(taskIndex));
    qst.dirty(true);
    return QstDeleteResult{nextSelection(taskIndex, tasks.count()), identifier, repaired};
}

QstInsertResult appendQstTaskGroup(GffFile& qst, std::optional<std::int32_t> requestedIdentifier) {
    GffList& groups = requireQstTaskGroupList(qst);
    const auto existing = collectIdentifiers(groups);
    const std::int32_t identifier = requestedIdentifier.value_or(firstAvailableIdentifier(existing));
    if (identifier < 0) throw std::invalid_argument("QST task-group identifiers must be non-negative.");
    if (existing.find(identifier) != existing.end()) {
        throw std::runtime_error("Task-group identifier " + std::to_string(identifier) + " is already used.");
    }
    const std::size_t index = groups.count();
    auto group = std::make_unique<GffStruct>();
    group->typeid_ = 0u;
    group->AddField(std::make_unique<GffIntField>("Identifier", identifier));
    group->AddField(std::make_unique<GffList>("TaskIndexList"));
    groups.AddStruct(std::move(group));
    qst.dirty(true);
    return QstInsertResult{index, identifier, 0u};
}

QstDeleteResult deleteQstTaskGroup(GffFile& qst, std::size_t groupIndex) {
    GffList& groups = requireQstTaskGroupList(qst);
    const std::int32_t identifier = qstEffectiveIdentifier(requireStruct(groups, groupIndex, "task group"), groupIndex);
    std::size_t repaired = 0;
    GffList& tasks = requireQstTaskList(qst);
    for (std::size_t taskIndex = 0; taskIndex < tasks.count(); ++taskIndex) {
        GffStruct& task = requireStruct(tasks, taskIndex, "task");
        GffField* next = task.GetFieldByLabel("NextTaskGroup");
        if (next == nullptr) continue;
        if (next->fieldtype != FIELD_TYPE_INT) {
            throw std::runtime_error("A QST NextTaskGroup field has the wrong GFF type.");
        }
        auto& value = static_cast<GffIntField&>(*next).value;
        if (value == static_cast<std::int32_t>(groupIndex)) {
            value = -1;
            ++repaired;
        } else if (value > static_cast<std::int32_t>(groupIndex)) {
            --value;
            ++repaired;
        }
    }
    groups.DeleteStruct(static_cast<UInt32>(groupIndex));
    qst.dirty(true);
    return QstDeleteResult{nextSelection(groupIndex, groups.count()), identifier, repaired};
}

std::vector<std::int32_t> qstGroupTaskIndices(const GffFile& qst, std::size_t groupIndex) {
    const GffStruct& group = requireStruct(requireQstTaskGroupList(qst), groupIndex, "task group");
    const GffList* refs = taskIndexList(group);
    std::vector<std::int32_t> result;
    if (refs == nullptr) return result;
    result.reserve(refs->count());
    for (std::size_t index = 0; index < refs->count(); ++index) {
        result.push_back(requireTaskReference(requireStruct(*refs, index, "task reference")));
    }
    return result;
}

void replaceQstGroupTaskIndices(GffFile& qst,
                               std::size_t groupIndex,
                               const std::vector<std::int32_t>& taskIndices) {
    const std::size_t taskCount = requireQstTaskList(qst).count();
    std::unordered_set<std::int32_t> seen;
    for (const std::int32_t taskIndex : taskIndices) {
        if (taskIndex < 0 || static_cast<std::size_t>(taskIndex) >= taskCount) {
            throw std::runtime_error("Task-list index " + std::to_string(taskIndex) + " does not exist.");
        }
        if (!seen.insert(taskIndex).second) {
            throw std::runtime_error("Task-list index " + std::to_string(taskIndex) + " is repeated in the group.");
        }
    }

    GffStruct& group = requireStruct(requireQstTaskGroupList(qst), groupIndex, "task group");
    GffList* refs = taskIndexList(group);
    if (refs == nullptr) {
        auto list = std::make_unique<GffList>("TaskIndexList");
        refs = list.get();
        group.AddField(std::move(list));
    }
    while (refs->count() != 0) refs->DeleteStruct(static_cast<UInt32>(refs->count() - 1));
    for (std::size_t index = 0; index < taskIndices.size(); ++index) {
        auto item = std::make_unique<GffStruct>();
        item->typeid_ = 0u;
        item->AddField(std::make_unique<GffIntField>("Task", taskIndices[index]));
        refs->AddStruct(std::move(item));
    }
    qst.dirty(true);
}

void setQstJadeStringRef(GffStruct& structure,
                         const std::string& label,
                         UInt32 subtype,
                         std::optional<UInt32> strref,
                         bool removeWhenUnset) {
    if (!strref && removeWhenUnset) {
        if (structure.GetFieldByLabel(label) != nullptr) structure.DeleteField(label);
        return;
    }
    const UInt32 value = strref.value_or(0xFFFFFFFFu);
    GffField* field = structure.GetFieldByLabel(label);
    if (field == nullptr) {
        structure.AddField(std::make_unique<GffJadeStringRefField>(label, subtype, value));
        return;
    }
    if (field->fieldtype != FIELD_TYPE_JADE_STRREF) {
        throw std::runtime_error("QST field " + label + " has the wrong GFF type.");
    }
    auto& jade = static_cast<GffJadeStringRefField&>(*field);
    jade.stringType = subtype;
    jade.strref = value;
}

std::optional<UInt32> qstJadeStringRef(const GffStruct& structure, const std::string& label) {
    const GffField* field = structure.GetFieldByLabel(label);
    if (field == nullptr) return std::nullopt;
    if (field->fieldtype != FIELD_TYPE_JADE_STRREF) {
        throw std::runtime_error("QST field " + label + " has the wrong GFF type.");
    }
    const UInt32 value = static_cast<const GffJadeStringRefField&>(*field).strref;
    return value == 0xFFFFFFFFu ? std::nullopt : std::optional<UInt32>{value};
}

std::optional<std::int32_t> qstInt(const GffStruct& structure, const std::string& label) {
    const GffField* field = structure.GetFieldByLabel(label);
    if (field == nullptr) return std::nullopt;
    if (field->fieldtype != FIELD_TYPE_INT) {
        throw std::runtime_error("QST field " + label + " has the wrong GFF type.");
    }
    return static_cast<const GffIntField&>(*field).value;
}

void setQstInt(GffStruct& structure, const std::string& label, std::int32_t value) {
    GffField* field = structure.GetFieldByLabel(label);
    if (field == nullptr) {
        structure.AddField(std::make_unique<GffIntField>(label, value));
        return;
    }
    if (field->fieldtype != FIELD_TYPE_INT) {
        throw std::runtime_error("QST field " + label + " has the wrong GFF type.");
    }
    static_cast<GffIntField&>(*field).value = value;
}

} // namespace neoqst
