#include "QstOperations.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace neogff;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

GffStruct& item(GffList& list, std::size_t index) {
    GffStruct* value = list.GetStruct(index);
    if (value == nullptr) throw std::runtime_error("Missing test struct.");
    return *value;
}

void run() {
    GffFile qst;
    neoqst::initializeNewQst(qst);
    require(qst.filetype() == "QST ", "New file did not use the QST signature.");
    require(neoqst::requireQstTaskList(qst).count() == 0, "New QST task list was not empty.");
    require(neoqst::requireQstTaskGroupList(qst).count() == 0, "New QST group list was not empty.");

    const auto task0 = neoqst::appendQstTask(qst);
    const auto task1 = neoqst::appendQstTask(qst);
    require(task0.identifier == 0 && task1.identifier == 1, "Automatic task IDs were incorrect.");
    require(task0.structTypeId == 0 && task1.structTypeId == 0,
            "QST task structs must use type ID 0.");

    const auto group0 = neoqst::appendQstTaskGroup(qst);
    const auto group1 = neoqst::appendQstTaskGroup(qst);
    require(group0.identifier == 0 && group1.identifier == 1, "Automatic group IDs were incorrect.");
    require(group0.structTypeId == 0 && group1.structTypeId == 0,
            "QST task-group structs must use type ID 0.");

    neoqst::replaceQstGroupTaskIndices(qst, group0.index, {0, 1});
    require(neoqst::qstGroupTaskIndices(qst, group0.index) == std::vector<std::int32_t>({0, 1}),
            "Task references were not written.");

    GffStruct& firstTask = item(neoqst::requireQstTaskList(qst), task0.index);
    neoqst::setQstInt(firstTask, "NextTaskGroup", 1);
    neoqst::setQstJadeStringRef(firstTask, "TaskName", 4u, 123u);
    neoqst::setQstJadeStringRef(firstTask, "QuestSummaryPre", 4u, 456u, true);
    neoqst::setQstJadeStringRef(firstTask, "QuestSummaryPost", 4u, std::nullopt, true);
    require(firstTask.GetFieldByLabel("QuestSummaryPost") == nullptr,
            "Clearing an absent optional summary should be a no-op.");
    require(neoqst::qstJadeStringRef(firstTask, "TaskName") == 123u, "TaskName StrRef was not stored.");

    neoqst::changeQstTaskIdentifier(qst, task0.index, 10);
    require(neoqst::qstGroupTaskIndices(qst, group0.index) == std::vector<std::int32_t>({0, 1}),
            "Changing a task ID unexpectedly changed task-list references.");

    neoqst::changeQstGroupIdentifier(qst, group1.index, 5);
    require(neoqst::qstInt(firstTask, "NextTaskGroup") == 1,
            "Changing a group ID unexpectedly changed the group-list index reference.");

    const auto removedTask = neoqst::deleteQstTask(qst, task0.index);
    require(removedTask.identifier == 10 && removedTask.repairedReferences == 2,
            "Deleting a task did not report repaired references.");
    require(neoqst::qstGroupTaskIndices(qst, 0) == std::vector<std::int32_t>({0}),
            "Deleting a task did not remove group references.");

    GffStruct& remainingTask = item(neoqst::requireQstTaskList(qst), 0);
    neoqst::setQstInt(remainingTask, "NextTaskGroup", 1);
    const auto removedGroup = neoqst::deleteQstTaskGroup(qst, 1);
    require(removedGroup.identifier == 5 && removedGroup.repairedReferences == 1,
            "Deleting a group did not repair task transitions.");
    require(neoqst::qstInt(remainingTask, "NextTaskGroup") == -1,
            "Deleting a group did not reset NextTaskGroup.");

    GffStruct* root = qst.root();
    require(root != nullptr, "QST root is missing.");
    neoqst::setQstJadeStringRef(*root, "QuestName", 4u, 999u);
    neoqst::setQstJadeStringRef(*root, "QuestDescription", 4u, 1000u);
    neoqst::validateQst(qst);

    const std::filesystem::path output =
        std::filesystem::temp_directory_path() / "neoqst-operation-tests.qst2";
    qst.SaveFile(output);

    GffFile reopened;
    reopened.LoadFile(output);
    neoqst::validateQst(reopened);
    require(reopened.filetype() == "QST ", ".qst2 save changed the internal QST type.");
    require(neoqst::requireQstTaskList(reopened).count() == 1, "Saved task count is wrong.");
    require(neoqst::requireQstTaskGroupList(reopened).count() == 1, "Saved group count is wrong.");
    require(neoqst::qstJadeStringRef(*reopened.root(), "QuestName") == 999u,
            "Quest metadata did not round-trip.");
    std::filesystem::remove(output);
}

} // namespace

int main() {
    try {
        run();
        std::cout << "NeoQST operation tests passed.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "NeoQST operation tests failed: " << ex.what() << '\n';
        return 1;
    }
}
