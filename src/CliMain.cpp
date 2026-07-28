#include "QstOperations.hpp"
#include <gff/GffJson.hpp>
#include <gff/GffTypeNames.hpp>
#include <gff/GffXml.hpp>
#include "TabularData.hpp"
#include "TslPatcher.hpp"
#include <neotlk/TlkLookup.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace neogff;
using namespace neoqst;

namespace {

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string pathToUtf8(const std::filesystem::path& path) {
#if defined(__cpp_lib_char8_t)
    const std::u8string value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
#else
    return path.u8string();
#endif
}

std::string readTextFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("Unable to open input file: " + pathToUtf8(file));
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void writeTextFile(const std::filesystem::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Unable to open output file: " + pathToUtf8(file));
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) throw std::runtime_error("Unable to write output file: " + pathToUtf8(file));
}

std::string joinPath(const std::string& parent, const std::string& child) {
    if (parent.empty()) return child;
    return parent + "\\" + child;
}

void appendFieldRows(neotabular::Table& table, const GffField& field, const std::string& path);

void appendStructRows(neotabular::Table& table, const GffStruct& structure, const std::string& path) {
    for (const auto& field : structure.allFields()) {
        if (field) appendFieldRows(table, *field, joinPath(path, field->GetLabel()));
    }
}

void appendFieldRows(neotabular::Table& table, const GffField& field, const std::string& path) {
    table.rows.push_back({path,
                          field.GetLabel(),
                          gffFieldTypeName(field.fieldtype),
                          gffFieldTypeEditable(field.fieldtype) ? "yes" : "no",
                          field.GetString()});
    if (field.fieldtype == FIELD_TYPE_STRUCT) {
        appendStructRows(table, static_cast<const GffStruct&>(field), path);
    } else if (field.fieldtype == FIELD_TYPE_LIST) {
        const auto& list = static_cast<const GffList&>(field);
        for (std::size_t index = 0; index < list.count(); ++index) {
            const GffStruct* item = list.GetStruct(index);
            if (item == nullptr) continue;
            const std::string itemPath = joinPath(path, std::to_string(index));
            table.rows.push_back({itemPath, "[" + std::to_string(index) + "]", "Struct", "no", item->GetString()});
            appendStructRows(table, *item, itemPath);
        }
    }
}

neotabular::Table qstTable(const GffFile& qst) {
    neotabular::Table table;
    table.columns = {"Path", "Label", "Type", "Editable", "Value"};
    if (const GffStruct* root = qst.root()) appendStructRows(table, *root, "");
    return table;
}

void printTable(const neotabular::Table& table) {
    for (std::size_t column = 0; column < table.columns.size(); ++column) {
        if (column) std::cout << '\t';
        std::cout << table.columns[column];
    }
    std::cout << '\n';
    for (const auto& row : table.rows) {
        for (std::size_t column = 0; column < table.columns.size(); ++column) {
            if (column) std::cout << '\t';
            if (column < row.size()) std::cout << row[column];
        }
        std::cout << '\n';
    }
}

const GffField* field(const GffStruct& structure, const char* label) {
    return structure.GetFieldByLabel(label);
}

std::string intText(const GffStruct& structure, const char* label) {
    const auto value = qstInt(structure, label);
    return value ? std::to_string(*value) : std::string("-");
}

std::string jadeText(const GffStruct& structure, const char* label, const neotlk::TlkLookup* tlk) {
    const GffField* value = field(structure, label);
    if (value == nullptr || value->fieldtype != FIELD_TYPE_JADE_STRREF) return {};
    const auto& jade = static_cast<const GffJadeStringRefField&>(*value);
    if (jade.strref == 0xFFFFFFFFu) return "<unset>";
    if (tlk == nullptr) return "<StrRef " + std::to_string(jade.strref) + ">";
    return tlk->resolve(jade.strref).value_or("<missing StrRef " + std::to_string(jade.strref) + ">");
}

std::string jadeRef(const GffStruct& structure, const char* label) {
    const auto value = qstJadeStringRef(structure, label);
    return value ? std::to_string(*value) : std::string("-1");
}

void printQst(const GffFile& qst, const neotlk::TlkLookup* tlk) {
    validateQst(qst);
    const GffStruct* root = qst.root();
    if (root == nullptr) throw std::runtime_error("The QST root struct is missing.");

    std::cout << "QuestName\t" << jadeRef(*root, "QuestName") << '\t'
              << jadeText(*root, "QuestName", tlk) << '\n';
    std::cout << "QuestDescription\t" << jadeRef(*root, "QuestDescription") << '\t'
              << jadeText(*root, "QuestDescription", tlk) << "\n\nTasks\n";

    const GffList& tasks = requireQstTaskList(qst);
    for (std::size_t index = 0; index < tasks.count(); ++index) {
        const GffStruct* task = tasks.GetStruct(index);
        if (task == nullptr) continue;
        std::cout << index << "\tID=" << qstEffectiveIdentifier(*task)
                  << "\tNextGroup=" << intText(*task, "NextTaskGroup")
                  << "\tNotifyActive=" << intText(*task, "NotifyActive")
                  << "\tNotifyComplete=" << intText(*task, "NotifyComplete")
                  << "\tName=" << jadeText(*task, "TaskName", tlk) << '\n';
        std::cout << "  Summary\t" << jadeText(*task, "TaskSummary", tlk) << '\n';
        if (field(*task, "QuestSummaryPre")) {
            std::cout << "  QuestSummaryPre\t" << jadeText(*task, "QuestSummaryPre", tlk) << '\n';
        }
        if (field(*task, "QuestSummaryPost")) {
            std::cout << "  QuestSummaryPost\t" << jadeText(*task, "QuestSummaryPost", tlk) << '\n';
        }
    }

    std::cout << "\nTask groups\n";
    const GffList& groups = requireQstTaskGroupList(qst);
    for (std::size_t index = 0; index < groups.count(); ++index) {
        const GffStruct* group = groups.GetStruct(index);
        if (group == nullptr) continue;
        std::cout << index << "\tID=" << qstEffectiveIdentifier(*group) << "\tTaskIndices=";
        const auto taskIndices = qstGroupTaskIndices(qst, index);
        for (std::size_t item = 0; item < taskIndices.size(); ++item) {
            if (item) std::cout << ',';
            std::cout << taskIndices[item];
        }
        std::cout << '\n';
    }
}

void loadImportedQst(GffFile& qst, const std::filesystem::path& input, std::string format) {
    format = lowerAscii(std::move(format));
    if (format.empty() || format == "auto") {
        format = lowerAscii(pathToUtf8(input.extension()));
        if (!format.empty() && format.front() == '.') format.erase(format.begin());
    }
    if (format == "qst" || format == "qst2" || format == "gff" || format == "native" || format.empty()) {
        qst.LoadFile(input);
    } else if (format == "xml") {
        LoadGffXml(qst, readTextFile(input));
    } else if (format == "json") {
        LoadGffXml(qst, gffJsonToXml(readTextFile(input)));
    } else {
        throw std::runtime_error("NeoQST imports native QST/QST2, XML, or JSON documents.");
    }
    validateQst(qst);
}

struct PatchOptions {
    bool package = true;
    bool allowUnsupported = false;
    std::string filename;
    std::string modifiedFormat = "auto";
};

PatchOptions parsePatchOptions(int argc, char** argv, int begin, const std::filesystem::path& original) {
    PatchOptions options;
    options.filename = neotsl::basenameForPatch(original);
    for (int index = begin; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--package") options.package = true;
        else if (argument == "--fragment") options.package = false;
        else if (argument == "--allow-unsupported") options.allowUnsupported = true;
        else if (argument == "--filename") {
            if (++index >= argc) throw std::runtime_error("--filename requires a value.");
            options.filename = argv[index];
        } else if (argument == "--modified-format" || argument == "--input-format") {
            if (++index >= argc) throw std::runtime_error(argument + " requires a value.");
            options.modifiedFormat = argv[index];
        } else {
            throw std::runtime_error("Unknown patcher option: " + argument);
        }
    }
    return options;
}

int diffPatcher(const std::filesystem::path& originalPath,
                const std::filesystem::path& modifiedPath,
                const std::filesystem::path& output,
                const PatchOptions& options) {
    GffFile original;
    original.LoadFile(originalPath);
    validateQst(original);
    GffFile modified;
    loadImportedQst(modified, modifiedPath, options.modifiedFormat);
    if (original.version() != modified.version()) throw std::runtime_error("The QST versions do not match.");
    auto project = neotsl::diffGffFlatTable(qstTable(original), qstTable(modified), options.filename,
                                           options.package, originalPath);
    if (!options.allowUnsupported) neotsl::throwIfUnsupported(project);
    else neotsl::printReport(project);
    if (options.package) neotsl::writePackage(project, output, true);
    else neotsl::writeFragment(project, output);
    return 0;
}

int info(const std::filesystem::path& input) {
    GffFile qst;
    qst.LoadFile(input);
    validateQst(qst);
    std::cout << "type=" << qst.filetype() << '\n'
              << "version=" << qst.version() << '\n'
              << "tasks=" << requireQstTaskList(qst).count() << '\n'
              << "task_groups=" << requireQstTaskGroupList(qst).count() << '\n';
    return 0;
}

int roundTrip(const std::filesystem::path& input, const std::filesystem::path& output) {
    GffFile qst;
    qst.LoadFile(input);
    validateQst(qst);
    qst.SaveFile(output);
    return 0;
}

int exportDocument(const std::filesystem::path& input,
                   const std::string& format,
                   const std::filesystem::path& output) {
    GffFile qst;
    qst.LoadFile(input);
    validateQst(qst);
    const std::string normalized = lowerAscii(format);
    if (normalized == "xml") writeTextFile(output, ToGffXml(qst));
    else if (normalized == "json") writeTextFile(output, gffXmlToJson(ToGffXml(qst)));
    else throw std::runtime_error("NeoQST exports XML or JSON.");
    return 0;
}

int importDocument(const std::filesystem::path& input,
                   const std::filesystem::path& output,
                   const std::string& format) {
    GffFile qst;
    loadImportedQst(qst, input, format);
    qst.SaveFile(output);
    return 0;
}

void usage() {
    std::cout
        << "NeoQST Jade Empire quest editor CLI\n\n"
        << "Usage:\n"
        << "  neoqst-cli <quest.qst|quest.qst2> [dialog.tlk]\n"
        << "  neoqst-cli --info <quest.qst|quest.qst2>\n"
        << "  neoqst-cli --new <output.qst|output.qst2>\n"
        << "  neoqst-cli --roundtrip <input.qst|qst2> <output.qst|qst2>\n"
        << "  neoqst-cli --search <quest.qst|qst2> <term>\n"
        << "  neoqst-cli --export <quest.qst|qst2> <xml|json> <output>\n"
        << "  neoqst-cli --import <input.xml|json|qst|qst2> <output.qst|qst2> <xml|json|native|auto>\n"
        << "  neoqst-cli --diff-tslpatcher <original.qst> <modified-input> <output-dir|fragment.ini>"
           " [--modified-format xml|json|qst|qst2|gff|native|auto] [--package|--fragment]"
           " [--filename name] [--allow-unsupported]\n\n"
        << "QST and QST2 are filename-extension aliases for the same QST V3.2 GFF payload.\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc >= 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h" ||
                          std::string(argv[1]) == "help")) {
            usage();
            return 0;
        }
        if (argc >= 2 && std::string(argv[1]) == "--info") {
            if (argc != 3) { usage(); return 2; }
            return info(argv[2]);
        }
        if (argc >= 2 && std::string(argv[1]) == "--new") {
            if (argc != 3) { usage(); return 2; }
            GffFile qst;
            initializeNewQst(qst, argv[2]);
            qst.SaveFile(argv[2]);
            return 0;
        }
        if (argc >= 2 && std::string(argv[1]) == "--roundtrip") {
            if (argc != 4) { usage(); return 2; }
            return roundTrip(argv[2], argv[3]);
        }
        if (argc >= 2 && std::string(argv[1]) == "--search") {
            if (argc != 4) { usage(); return 2; }
            GffFile qst;
            qst.LoadFile(argv[2]);
            validateQst(qst);
            printTable(neotabular::filterRows(qstTable(qst), argv[3]));
            return 0;
        }
        if (argc >= 2 && std::string(argv[1]) == "--export") {
            if (argc != 5) { usage(); return 2; }
            return exportDocument(argv[2], argv[3], argv[4]);
        }
        if (argc >= 2 && std::string(argv[1]) == "--import") {
            if (argc != 5) { usage(); return 2; }
            return importDocument(argv[2], argv[3], argv[4]);
        }
        if (argc >= 2 && (std::string(argv[1]) == "--diff-tslpatcher" ||
                          std::string(argv[1]) == "diff-tslpatcher")) {
            if (argc < 5) { usage(); return 2; }
            return diffPatcher(argv[2], argv[3], argv[4], parsePatchOptions(argc, argv, 5, argv[2]));
        }
        if (argc < 2 || argc > 3) {
            usage();
            return argc < 2 ? 1 : 2;
        }

        GffFile qst;
        qst.LoadFile(argv[1]);
        validateQst(qst);
        std::unique_ptr<neotlk::TlkLookup> tlk;
        if (argc == 3) {
            tlk = std::make_unique<neotlk::TlkLookup>();
            tlk->load(std::filesystem::path(argv[2]));
        }
        printQst(qst, tlk.get());
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "NeoQST error: " << ex.what() << '\n';
        return 1;
    }
}
