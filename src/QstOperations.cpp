#include "QstOperations.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
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

void validateIdentifierValue(std::int32_t value, const char* role) {
    if (value < 0 || value > kMaxQstIdentifier) {
        throw std::runtime_error(std::string(role) + " must be between 0 and " +
                                 std::to_string(kMaxQstIdentifier) +
                                 "; Jade stores it as a signed 16-bit value.");
    }
}

void validateBooleanField(const GffStruct& structure, const char* label, const char* role) {
    const auto value = qstInt(structure, label);
    if (value && *value != 0 && *value != 1) {
        throw std::runtime_error(std::string(role) + " field " + label + " must be 0 or 1.");
    }
}

void validateResRefText(const std::string& value, const char* role) {
    if (value.size() > 16u) {
        throw std::runtime_error(std::string(role) + " cannot exceed 16 bytes.");
    }
    for (const unsigned char ch : value) {
        if (!(std::isalnum(ch) != 0 || ch == '_')) {
            throw std::runtime_error(std::string(role) +
                                     " may contain ASCII letters, digits, and underscores only.");
        }
    }
}

std::unordered_set<std::int32_t> collectIdentifiers(const GffList& list, const char* role) {
    std::unordered_set<std::int32_t> identifiers;
    for (std::size_t index = 0; index < list.count(); ++index) {
        const std::int32_t identifier = qstEffectiveIdentifier(requireStruct(list, index, role));
        validateIdentifierValue(identifier, role);
        if (!identifiers.insert(identifier).second) {
            throw std::runtime_error(std::string("The QST contains duplicate ") + role +
                                     " identifier " + std::to_string(identifier) +
                                     ". Jade resolves identifiers by the first match, so identifiers must be unique.");
        }
    }
    return identifiers;
}

std::int32_t firstAvailableIdentifier(const std::unordered_set<std::int32_t>& identifiers) {
    for (std::int32_t candidate = 0; candidate <= kMaxQstIdentifier; ++candidate) {
        if (identifiers.find(candidate) == identifiers.end()) return candidate;
    }
    throw std::runtime_error("The QST has no available signed 16-bit identifiers.");
}

std::optional<std::size_t> nextSelection(std::size_t removedIndex, std::size_t remaining) {
    if (remaining == 0) return std::nullopt;
    return std::min(removedIndex, remaining - 1u);
}

void setIdentifier(GffStruct& structure, std::int32_t value) {
    validateIdentifierValue(value, "QST identifier");
    setQstInt(structure, "Identifier", value);
}

void validateKnownRootFields(const GffStruct& root) {
    validateBooleanField(root, "QuestType", "Quest");
    validateBooleanField(root, "QuestActive", "Quest");
    validateBooleanField(root, "QuestComplete", "Quest");
    validateBooleanField(root, "QuestUpdated", "Quest");

    if (const auto resref = qstResRef(root, "QuestResRef")) {
        validateResRefText(*resref, "Quest resource reference");
    }
    (void)qstDword(root, "TimeHi");
    (void)qstDword(root, "TimeLo");
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
    const GffStruct* root = qst.root();
    if (root == nullptr) throw std::runtime_error("The QST root struct is missing.");
    validateKnownRootFields(*root);

    const GffList& taskList = requireQstTaskList(qst);
    const GffList& groupList = requireQstTaskGroupList(qst);
    if (taskList.count() > kMaxQstTasks) {
        throw std::runtime_error("The QST contains more than 128 tasks. Jade stores task-list references as signed bytes.");
    }
    if (groupList.count() > kMaxQstTaskGroups) {
        throw std::runtime_error("The QST contains more than 128 task groups. Jade stores group-list references as signed bytes.");
    }

    (void)collectIdentifiers(taskList, "task");
    (void)collectIdentifiers(groupList, "task-group");

    for (std::size_t index = 0; index < taskList.count(); ++index) {
        const GffStruct& task = requireStruct(taskList, index, "task");
        validateBooleanField(task, "Complete", "Task");
        validateBooleanField(task, "NotifyActive", "Task");
        validateBooleanField(task, "NotifyComplete", "Task");

        const auto next = qstInt(task, "NextTaskGroup");
        if (next) {
            const bool sentinel = *next == kQstNoNextGroup || *next == kQstCompleteQuest;
            const bool groupIndex = *next >= 0 && *next <= 127 &&
                                    static_cast<std::size_t>(*next) < groupList.count();
            if (!sentinel && !groupIndex) {
                throw std::runtime_error("Task " + std::to_string(qstEffectiveIdentifier(task)) +
                                         " has invalid NextTaskGroup " + std::to_string(*next) +
                                         ". Use -1, -2, or an existing group-list index from 0 through 127.");
            }
        }
    }

    std::unordered_map<std::int32_t, std::size_t> taskOwner;
    for (std::size_t groupIndex = 0; groupIndex < groupList.count(); ++groupIndex) {
        const GffStruct& group = requireStruct(groupList, groupIndex, "task group");
        validateBooleanField(group, "Active", "Task group");
        validateBooleanField(group, "ANDGroup", "Task group");
        if (const auto script = qstResRef(group, "OnComplete")) {
            validateResRefText(*script, "Task-group OnComplete script");
        }

        std::unordered_set<std::int32_t> inThisGroup;
        for (const std::int32_t taskIndex : qstGroupTaskIndices(qst, groupIndex)) {
            if (taskIndex < 0 || taskIndex > 127 ||
                static_cast<std::size_t>(taskIndex) >= taskList.count()) {
                throw std::runtime_error("Task group " +
                                         std::to_string(qstEffectiveIdentifier(group)) +
                                         " references invalid task-list index " +
                                         std::to_string(taskIndex) + ".");
            }
            if (!inThisGroup.insert(taskIndex).second) {
                throw std::runtime_error("Task-list index " + std::to_string(taskIndex) +
                                         " is repeated in task group " +
                                         std::to_string(qstEffectiveIdentifier(group)) + ".");
            }
            const auto [it, inserted] = taskOwner.emplace(taskIndex, groupIndex);
            if (!inserted && it->second != groupIndex) {
                throw std::runtime_error("Task-list index " + std::to_string(taskIndex) +
                                         " belongs to multiple task groups. Jade stores only one owning group pointer per task.");
            }
        }
    }
}

GffList& requireQstTaskList(GffFile& qst) { return requireRootList(qst, "TaskList"); }
const GffList& requireQstTaskList(const GffFile& qst) { return requireRootList(qst, "TaskList"); }
GffList& requireQstTaskGroupList(GffFile& qst) { return requireRootList(qst, "TaskGroupList"); }
const GffList& requireQstTaskGroupList(const GffFile& qst) { return requireRootList(qst, "TaskGroupList"); }

std::int32_t qstEffectiveIdentifier(const GffStruct& structure) {
    const GffField* field = structure.GetFieldByLabel("Identifier");
    if (field == nullptr) return 0;
    if (field->fieldtype != FIELD_TYPE_INT) throw std::runtime_error("A QST Identifier field has the wrong GFF type.");
    return static_cast<const GffIntField&>(*field).value;
}

std::int32_t suggestQstTaskIdentifier(const GffFile& qst) {
    return firstAvailableIdentifier(collectIdentifiers(requireQstTaskList(qst), "task"));
}

std::int32_t suggestQstGroupIdentifier(const GffFile& qst) {
    return firstAvailableIdentifier(collectIdentifiers(requireQstTaskGroupList(qst), "task-group"));
}

void changeQstTaskIdentifier(GffFile& qst, std::size_t taskIndex, std::int32_t newIdentifier) {
    validateIdentifierValue(newIdentifier, "Task identifier");
    GffList& tasks = requireQstTaskList(qst);
    GffStruct& task = requireStruct(tasks, taskIndex, "task");
    const std::int32_t oldIdentifier = qstEffectiveIdentifier(task);
    if (oldIdentifier == newIdentifier) return;
    const auto identifiers = collectIdentifiers(tasks, "task");
    if (identifiers.find(newIdentifier) != identifiers.end()) {
        throw std::runtime_error("Task identifier " + std::to_string(newIdentifier) + " is already used.");
    }
    setIdentifier(task, newIdentifier);
    qst.dirty(true);
}

void changeQstGroupIdentifier(GffFile& qst, std::size_t groupIndex, std::int32_t newIdentifier) {
    validateIdentifierValue(newIdentifier, "Task-group identifier");
    GffList& groups = requireQstTaskGroupList(qst);
    GffStruct& group = requireStruct(groups, groupIndex, "task group");
    const std::int32_t oldIdentifier = qstEffectiveIdentifier(group);
    if (oldIdentifier == newIdentifier) return;
    const auto identifiers = collectIdentifiers(groups, "task-group");
    if (identifiers.find(newIdentifier) != identifiers.end()) {
        throw std::runtime_error("Task-group identifier " + std::to_string(newIdentifier) + " is already used.");
    }
    setIdentifier(group, newIdentifier);
    qst.dirty(true);
}

QstInsertResult appendQstTask(GffFile& qst, std::optional<std::int32_t> requestedIdentifier) {
    GffList& tasks = requireQstTaskList(qst);
    if (tasks.count() >= kMaxQstTasks) {
        throw std::runtime_error("A Jade QST cannot contain more than 128 tasks.");
    }
    const auto existing = collectIdentifiers(tasks, "task");
    const std::int32_t identifier = requestedIdentifier.value_or(firstAvailableIdentifier(existing));
    validateIdentifierValue(identifier, "Task identifier");
    if (existing.find(identifier) != existing.end()) {
        throw std::runtime_error("Task identifier " + std::to_string(identifier) + " is already used.");
    }

    const std::size_t index = tasks.count();
    auto task = std::make_unique<GffStruct>();
    task->typeid_ = 0u;
    task->AddField(std::make_unique<GffJadeStringRefField>("TaskName", 4u, 0xFFFFFFFFu));
    task->AddField(std::make_unique<GffJadeStringRefField>("TaskSummary", 4u, 0xFFFFFFFFu));
    task->AddField(std::make_unique<GffIntField>("Identifier", identifier));
    task->AddField(std::make_unique<GffIntField>("NextTaskGroup", kQstNoNextGroup));
    task->AddField(std::make_unique<GffIntField>("NotifyActive", 1));
    task->AddField(std::make_unique<GffIntField>("NotifyComplete", 1));
    tasks.AddStruct(std::move(task));
    qst.dirty(true);
    return QstInsertResult{index, identifier, 0u};
}

QstDeleteResult deleteQstTask(GffFile& qst, std::size_t taskIndex) {
    GffList& tasks = requireQstTaskList(qst);
    const std::int32_t identifier = qstEffectiveIdentifier(requireStruct(tasks, taskIndex, "task"));
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
    if (groups.count() >= kMaxQstTaskGroups) {
        throw std::runtime_error("A Jade QST cannot contain more than 128 task groups.");
    }
    const auto existing = collectIdentifiers(groups, "task-group");
    const std::int32_t identifier = requestedIdentifier.value_or(firstAvailableIdentifier(existing));
    validateIdentifierValue(identifier, "Task-group identifier");
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
    const std::int32_t identifier = qstEffectiveIdentifier(requireStruct(groups, groupIndex, "task group"));
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
            value = kQstNoNextGroup;
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
        if (taskIndex < 0 || taskIndex > 127 || static_cast<std::size_t>(taskIndex) >= taskCount) {
            throw std::runtime_error("Task-list index " + std::to_string(taskIndex) +
                                     " does not exist or cannot be stored in Jade's signed-byte reference.");
        }
        if (!seen.insert(taskIndex).second) {
            throw std::runtime_error("Task-list index " + std::to_string(taskIndex) + " is repeated in the group.");
        }
    }

    const GffList& groupsRead = requireQstTaskGroupList(qst);
    for (std::size_t otherGroup = 0; otherGroup < groupsRead.count(); ++otherGroup) {
        if (otherGroup == groupIndex) continue;
        for (const std::int32_t existingIndex : qstGroupTaskIndices(qst, otherGroup)) {
            if (seen.find(existingIndex) != seen.end()) {
                throw std::runtime_error("Task-list index " + std::to_string(existingIndex) +
                                         " already belongs to task group at list index " +
                                         std::to_string(otherGroup) + ". Jade stores one owning group pointer per task.");
            }
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
    for (const std::int32_t taskIndex : taskIndices) {
        auto item = std::make_unique<GffStruct>();
        item->typeid_ = 0u;
        item->AddField(std::make_unique<GffIntField>("Task", taskIndex));
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

void setQstOptionalInt(GffStruct& structure,
                       const std::string& label,
                       std::optional<std::int32_t> value) {
    if (!value) {
        if (structure.GetFieldByLabel(label) != nullptr) structure.DeleteField(label);
        return;
    }
    setQstInt(structure, label, *value);
}

std::optional<UInt32> qstDword(const GffStruct& structure, const std::string& label) {
    const GffField* field = structure.GetFieldByLabel(label);
    if (field == nullptr) return std::nullopt;
    if (field->fieldtype != FIELD_TYPE_DWORD) {
        throw std::runtime_error("QST field " + label + " has the wrong GFF type.");
    }
    return static_cast<const GffUInt32Field&>(*field).value;
}

void setQstDword(GffStruct& structure, const std::string& label, UInt32 value) {
    GffField* field = structure.GetFieldByLabel(label);
    if (field == nullptr) {
        structure.AddField(std::make_unique<GffUInt32Field>(label, value));
        return;
    }
    if (field->fieldtype != FIELD_TYPE_DWORD) {
        throw std::runtime_error("QST field " + label + " has the wrong GFF type.");
    }
    static_cast<GffUInt32Field&>(*field).value = value;
}

void setQstOptionalDword(GffStruct& structure,
                         const std::string& label,
                         std::optional<UInt32> value) {
    if (!value) {
        if (structure.GetFieldByLabel(label) != nullptr) structure.DeleteField(label);
        return;
    }
    setQstDword(structure, label, *value);
}

std::optional<std::string> qstResRef(const GffStruct& structure, const std::string& label) {
    const GffField* field = structure.GetFieldByLabel(label);
    if (field == nullptr) return std::nullopt;
    if (field->fieldtype != FIELD_TYPE_RESREF) {
        throw std::runtime_error("QST field " + label + " has the wrong GFF type.");
    }
    return static_cast<const GffResRefField&>(*field).GetString();
}

void setQstOptionalResRef(GffStruct& structure,
                          const std::string& label,
                          const std::optional<std::string>& value) {
    if (!value || value->empty()) {
        if (structure.GetFieldByLabel(label) != nullptr) structure.DeleteField(label);
        return;
    }
    validateResRefText(*value, ("QST field " + label).c_str());
    GffField* field = structure.GetFieldByLabel(label);
    if (field == nullptr) {
        structure.AddField(std::make_unique<GffResRefField>(label, *value));
        return;
    }
    if (field->fieldtype != FIELD_TYPE_RESREF) {
        throw std::runtime_error("QST field " + label + " has the wrong GFF type.");
    }
    static_cast<GffResRefField&>(*field).SetString(*value);
}

} // namespace neoqst
