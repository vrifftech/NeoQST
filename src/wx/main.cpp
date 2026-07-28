#include "QstOperations.hpp"
#include <gff/GffJson.hpp>
#include <gff/GffXml.hpp>
#include "NeoGameDirectoryMenu.hpp"
#include "NeoSettings.hpp"
#include "NeoWxUi.hpp"
#include <neotlk/TlkLookup.hpp>

#include "neoqst_icon.xpm"

#include <wx/aboutdlg.h>
#include <wx/checkbox.h>
#include <wx/checklst.h>
#include <wx/choice.h>
#include <wx/filedlg.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/spinctrl.h>
#include <wx/textctrl.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <unordered_set>
#include <vector>

using namespace neogff;
using namespace neoqst;

namespace {

constexpr const char* kAppName = "NeoQST";
constexpr const char* kQstWildcard =
    "Jade Empire quest files (*.qst;*.qst2)|*.qst;*.qst2|QST files (*.qst)|*.qst|QST2 files (*.qst2)|*.qst2|All files (*.*)|*.*";
constexpr const char* kTlkWildcard = "TLK files (*.tlk)|*.tlk|All files (*.*)|*.*";
constexpr const char* kXmlWildcard = "XML files (*.xml)|*.xml|All files (*.*)|*.*";
constexpr const char* kJsonWildcard = "JSON files (*.json)|*.json|All files (*.*)|*.*";
constexpr UInt32 kUnsetStrRef = 0xFFFFFFFFu;

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Unable to open input file: " + neosettings::pathToUtf8(path));
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void writeTextFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Unable to open output file: " + neosettings::pathToUtf8(path));
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) throw std::runtime_error("Unable to write output file: " + neosettings::pathToUtf8(path));
}

std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch) != 0; });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) { return std::isspace(ch) != 0; }).base();
    if (first >= last) return {};
    return std::string(first, last);
}

std::optional<UInt32> parseStrRef(const wxTextCtrl& control, const char* fieldName) {
    const std::string text = trim(wxui::toStd(control.GetValue()));
    if (text.empty() || text == "-1") return std::nullopt;
    std::size_t consumed = 0;
    const unsigned long long value = std::stoull(text, &consumed, 10);
    if (consumed != text.size() || value > std::numeric_limits<UInt32>::max()) {
        throw std::runtime_error(std::string(fieldName) + " must be -1 or an unsigned 32-bit StrRef.");
    }
    return static_cast<UInt32>(value);
}

UInt32 parseUInt32(const wxTextCtrl& control, const char* fieldName) {
    const std::string text = trim(wxui::toStd(control.GetValue()));
    if (text.empty()) throw std::runtime_error(std::string(fieldName) + " is required.");
    std::size_t consumed = 0;
    const unsigned long long value = std::stoull(text, &consumed, 10);
    if (consumed != text.size() || value > std::numeric_limits<UInt32>::max()) {
        throw std::runtime_error(std::string(fieldName) + " must be an unsigned 32-bit integer.");
    }
    return static_cast<UInt32>(value);
}

std::optional<std::string> parseOptionalResRef(const wxTextCtrl& control, const char* fieldName) {
    const std::string value = trim(wxui::toStd(control.GetValue()));
    if (value.empty()) return std::nullopt;
    if (value.size() > 16u) {
        throw std::runtime_error(std::string(fieldName) + " cannot exceed 16 characters.");
    }
    for (const unsigned char ch : value) {
        if (!(std::isalnum(ch) != 0 || ch == '_')) {
            throw std::runtime_error(std::string(fieldName) +
                                     " may contain letters, digits, and underscores only.");
        }
    }
    return value;
}

std::string strRefText(std::optional<UInt32> value) {
    return value ? std::to_string(*value) : std::string("-1");
}

const GffStruct& listStruct(const GffList& list, std::size_t index, const char* role) {
    if (index >= list.count()) throw std::out_of_range(std::string(role) + " index is outside the list.");
    const GffStruct* value = list.GetStruct(index);
    if (value == nullptr) throw std::runtime_error(std::string("Missing ") + role + " struct.");
    return *value;
}

GffStruct& listStruct(GffList& list, std::size_t index, const char* role) {
    if (index >= list.count()) throw std::out_of_range(std::string(role) + " index is outside the list.");
    GffStruct* value = list.GetStruct(index);
    if (value == nullptr) throw std::runtime_error(std::string("Missing ") + role + " struct.");
    return *value;
}

enum : int {
    ID_New = wxID_HIGHEST + 1,
    ID_Open,
    ID_LoadTlk,
    ID_Save,
    ID_SaveAs,
    ID_ImportXml,
    ID_ImportJson,
    ID_ExportXml,
    ID_ExportJson,
    ID_ApplyQuest,
    ID_AddTask,
    ID_DeleteTask,
    ID_ApplyTask,
    ID_AddGroup,
    ID_DeleteGroup,
    ID_ApplyGroup,
    ID_DarkMode,
};

class NeoQSTFrame final : public wxFrame {
public:
    NeoQSTFrame()
        : wxFrame(nullptr, wxID_ANY, "NeoQST", wxDefaultPosition, wxDefaultSize),
          settings_(kAppName) {
        SetIcon(wxIcon(neoqst_icon_xpm));
        SetMinSize(FromDIP(wxSize(900, 620)));
        buildMenus();
        buildLayout();
        SetInitialSize(FromDIP(wxSize(1240, 820)));
        bindEvents();
        darkMode_ = settings_.darkMode();
        if (darkModeItem_) darkModeItem_->Check(darkMode_);
        wxui::applyTheme(this, darkMode_);
        if (!settings_.restoreWindowPlacement(*this)) Centre();
        createNew(false);
        tryLoadCachedTlk();
        updateAll();
    }

    void openFromCommandLine(const std::filesystem::path& path) {
        if (!path.empty()) openFile(path);
    }

private:
    void buildMenus() {
        auto* menuBar = new wxMenuBar();
        auto* file = new wxMenu();
        file->Append(ID_New, "&New QST\tCtrl+N");
        file->Append(ID_Open, "&Open...\tCtrl+O");
        gameDirectoryMenu_ = neogames::appendOpenGameDirectoryMenu(
            *this,
            *file,
            neogames::OpenGameFileDialog{[this](const std::filesystem::path& directory) {
                openDialog(directory);
            }},
            std::vector<std::string>{"jade"});
        file->Append(ID_LoadTlk, "Load &TLK...");
        file->AppendSeparator();
        file->Append(ID_Save, "&Save\tCtrl+S");
        file->Append(ID_SaveAs, "Save &As...\tCtrl+Shift+S");
        file->AppendSeparator();
        auto* importMenu = new wxMenu();
        importMenu->Append(ID_ImportXml, "Import XML...");
        importMenu->Append(ID_ImportJson, "Import JSON...");
        file->AppendSubMenu(importMenu, "&Import");
        auto* exportMenu = new wxMenu();
        exportMenu->Append(ID_ExportXml, "Export XML...");
        exportMenu->Append(ID_ExportJson, "Export JSON...");
        file->AppendSubMenu(exportMenu, "&Export");
        file->AppendSeparator();
        file->Append(wxID_EXIT, "E&xit");
        menuBar->Append(file, "&File");

        auto* view = new wxMenu();
        darkModeItem_ = view->AppendCheckItem(ID_DarkMode, "&Dark mode");
        menuBar->Append(view, "&View");

        auto* help = new wxMenu();
        help->Append(wxID_ABOUT, "&About NeoQST");
        menuBar->Append(help, "&Help");
        SetMenuBar(menuBar);
    }

    wxTextCtrl* addTextField(wxWindow* parent,
                             wxSizer& sizer,
                             const wxString& label,
                             long style = 0,
                             int proportion = 1) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* caption = new wxStaticText(parent, wxID_ANY, label);
        caption->SetMinSize(FromDIP(wxSize(145, -1)));
        row->Add(caption, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        auto* control = new wxTextCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, style);
        row->Add(control, proportion, wxEXPAND);
        sizer.Add(row, proportion == 0 ? 0 : 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));
        return control;
    }

    wxSpinCtrl* addSpinField(wxWindow* parent,
                             wxSizer& sizer,
                             const wxString& label,
                             int minimum,
                             int maximum) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* caption = new wxStaticText(parent, wxID_ANY, label);
        caption->SetMinSize(FromDIP(wxSize(145, -1)));
        row->Add(caption, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        auto* control = new wxSpinCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                       wxSP_ARROW_KEYS, minimum, maximum, minimum);
        row->Add(control, 1, wxEXPAND);
        sizer.Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));
        return control;
    }

    void buildLayout() {
        CreateStatusBar(1);
        auto* root = new wxPanel(this);
        auto* outer = new wxBoxSizer(wxVERTICAL);

        auto* fileBox = new wxStaticBoxSizer(wxVERTICAL, root, "Document");
        auto* fileRow = new wxBoxSizer(wxHORIZONTAL);
        fileRow->Add(new wxStaticText(root, wxID_ANY, "QST file:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        filePath_ = new wxTextCtrl(root, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
        fileRow->Add(filePath_, 1, wxEXPAND | wxRIGHT, FromDIP(6));
        fileRow->Add(new wxButton(root, ID_Open, "Open..."), 0, wxRIGHT, FromDIP(4));
        fileRow->Add(new wxButton(root, ID_Save, "Save"), 0, wxRIGHT, FromDIP(4));
        fileRow->Add(new wxButton(root, ID_SaveAs, "Save As..."), 0);
        fileBox->Add(fileRow, 0, wxEXPAND | wxALL, FromDIP(6));

        auto* tlkRow = new wxBoxSizer(wxHORIZONTAL);
        tlkRow->Add(new wxStaticText(root, wxID_ANY, "Jade TLK:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        tlkPath_ = new wxTextCtrl(root, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
        tlkRow->Add(tlkPath_, 1, wxEXPAND | wxRIGHT, FromDIP(6));
        tlkRow->Add(new wxButton(root, ID_LoadTlk, "Load TLK..."), 0);
        fileBox->Add(tlkRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
        outer->Add(fileBox, 0, wxEXPAND | wxALL, FromDIP(8));

        auto* questBox = new wxStaticBoxSizer(wxVERTICAL, root, "Quest metadata");
        auto* typeRow = new wxBoxSizer(wxHORIZONTAL);
        typeRow->Add(new wxStaticText(root, wxID_ANY, "Quest type:"), 0,
                     wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        questType_ = new wxChoice(root, wxID_ANY);
        questType_->Append("0 - Type 0");
        questType_->Append("1 - Type 1");
        questType_->SetSelection(0);
        questType_->SetToolTip("Jade uses two journal categories. The runtime binary does not embed their authored display names.");
        typeRow->Add(questType_, 0);
        typeRow->AddStretchSpacer(1);
        questBox->Add(typeRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));

        auto* questGrid = new wxFlexGridSizer(2, 4, FromDIP(6), FromDIP(8));
        questGrid->AddGrowableCol(1, 1);
        questGrid->AddGrowableCol(3, 2);
        questGrid->Add(new wxStaticText(root, wxID_ANY, "Quest name StrRef:"), 0, wxALIGN_CENTER_VERTICAL);
        questNameRef_ = new wxTextCtrl(root, wxID_ANY);
        questGrid->Add(questNameRef_, 1, wxEXPAND);
        questGrid->Add(new wxStaticText(root, wxID_ANY, "Resolved name:"), 0, wxALIGN_CENTER_VERTICAL);
        questNameResolved_ = new wxTextCtrl(root, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
        questGrid->Add(questNameResolved_, 1, wxEXPAND);
        questGrid->Add(new wxStaticText(root, wxID_ANY, "Description StrRef:"), 0, wxALIGN_CENTER_VERTICAL);
        questDescriptionRef_ = new wxTextCtrl(root, wxID_ANY);
        questGrid->Add(questDescriptionRef_, 1, wxEXPAND);
        questGrid->Add(new wxStaticText(root, wxID_ANY, "Resolved description:"), 0, wxALIGN_CENTER_VERTICAL);
        questDescriptionResolved_ = new wxTextCtrl(root, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                                   wxTE_READONLY | wxTE_MULTILINE);
        questDescriptionResolved_->SetMinSize(FromDIP(wxSize(-1, 55)));
        questGrid->Add(questDescriptionResolved_, 1, wxEXPAND);
        questBox->Add(questGrid, 1, wxEXPAND | wxALL, FromDIP(6));

        runtimeStateEnabled_ = new wxCheckBox(root, wxID_ANY, "Store runtime quest state fields");
        runtimeStateEnabled_->SetToolTip(
            "Static QST definitions normally omit these fields. Enable this only when editing a serialized quest state.");
        questBox->Add(runtimeStateEnabled_, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));

        runtimeStatePanel_ = new wxPanel(root);
        auto* runtimeSizer = new wxBoxSizer(wxVERTICAL);
        auto* resrefRow = new wxBoxSizer(wxHORIZONTAL);
        resrefRow->Add(new wxStaticText(runtimeStatePanel_, wxID_ANY, "Quest resource reference:"), 0,
                       wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        questResRef_ = new wxTextCtrl(runtimeStatePanel_, wxID_ANY);
        resrefRow->Add(questResRef_, 1, wxEXPAND);
        runtimeSizer->Add(resrefRow, 0, wxEXPAND | wxBOTTOM, FromDIP(5));

        auto* stateRow = new wxBoxSizer(wxHORIZONTAL);
        stateRow->Add(new wxStaticText(runtimeStatePanel_, wxID_ANY, "State:"), 0,
                      wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        questActive_ = new wxCheckBox(runtimeStatePanel_, wxID_ANY, "Active");
        questComplete_ = new wxCheckBox(runtimeStatePanel_, wxID_ANY, "Complete");
        questUpdated_ = new wxCheckBox(runtimeStatePanel_, wxID_ANY, "Updated");
        stateRow->Add(questActive_, 0, wxRIGHT, FromDIP(12));
        stateRow->Add(questComplete_, 0, wxRIGHT, FromDIP(12));
        stateRow->Add(questUpdated_, 0);
        runtimeSizer->Add(stateRow, 0, wxEXPAND | wxBOTTOM, FromDIP(5));

        auto* timeRow = new wxBoxSizer(wxHORIZONTAL);
        timeRow->Add(new wxStaticText(runtimeStatePanel_, wxID_ANY, "Timestamp high:"), 0,
                     wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        questTimeHi_ = new wxTextCtrl(runtimeStatePanel_, wxID_ANY, "0");
        timeRow->Add(questTimeHi_, 1, wxRIGHT, FromDIP(12));
        timeRow->Add(new wxStaticText(runtimeStatePanel_, wxID_ANY, "Timestamp low:"), 0,
                     wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        questTimeLo_ = new wxTextCtrl(runtimeStatePanel_, wxID_ANY, "0");
        timeRow->Add(questTimeLo_, 1);
        runtimeSizer->Add(timeRow, 0, wxEXPAND);
        runtimeStatePanel_->SetSizer(runtimeSizer);
        questBox->Add(runtimeStatePanel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));

        auto* questButtons = new wxBoxSizer(wxHORIZONTAL);
        questButtons->AddStretchSpacer(1);
        questButtons->Add(new wxButton(root, ID_ApplyQuest, "Apply quest metadata"), 0);
        questBox->Add(questButtons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
        outer->Add(questBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

        auto* notebook = new wxNotebook(root, wxID_ANY);
        buildTaskPage(notebook);
        buildGroupPage(notebook);
        outer->Add(notebook, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

        root->SetSizer(outer);
    }

    void buildTaskPage(wxNotebook* notebook) {
        auto* page = new wxPanel(notebook);
        auto* splitter = new wxSplitterWindow(page, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                               wxSP_LIVE_UPDATE | wxSP_3D);
        auto* listPanel = new wxPanel(splitter);
        auto* editorPanel = new wxScrolledWindow(splitter, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
        editorPanel->SetScrollRate(0, FromDIP(12));

        auto* listSizer = new wxBoxSizer(wxVERTICAL);
        taskList_ = new wxListCtrl(listPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                   wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES | wxLC_VRULES);
        taskList_->AppendColumn("Index", wxLIST_FORMAT_RIGHT, FromDIP(60));
        taskList_->AppendColumn("ID", wxLIST_FORMAT_RIGHT, FromDIP(70));
        taskList_->AppendColumn("Task name", wxLIST_FORMAT_LEFT, FromDIP(245));
        taskList_->AppendColumn("Complete", wxLIST_FORMAT_CENTER, FromDIP(75));
        taskList_->AppendColumn("Next group", wxLIST_FORMAT_LEFT, FromDIP(150));
        taskList_->AppendColumn("Active notice", wxLIST_FORMAT_CENTER, FromDIP(95));
        taskList_->AppendColumn("Complete notice", wxLIST_FORMAT_CENTER, FromDIP(110));
        listSizer->Add(taskList_, 1, wxEXPAND | wxALL, FromDIP(6));
        auto* listButtons = new wxBoxSizer(wxHORIZONTAL);
        listButtons->Add(new wxButton(listPanel, ID_AddTask, "Add task"), 0, wxRIGHT, FromDIP(5));
        listButtons->Add(new wxButton(listPanel, ID_DeleteTask, "Delete task"), 0);
        listSizer->Add(listButtons, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
        listPanel->SetSizer(listSizer);

        auto* editorSizer = new wxStaticBoxSizer(wxVERTICAL, editorPanel, "Selected task");
        taskIdentifier_ = addSpinField(editorPanel, *editorSizer, "Identifier:", 0, kMaxQstIdentifier);
        taskNameRef_ = addTextField(editorPanel, *editorSizer, "Task name StrRef:", 0, 0);
        taskNameResolved_ = addTextField(editorPanel, *editorSizer, "Resolved task name:", wxTE_READONLY);
        taskSummaryRef_ = addTextField(editorPanel, *editorSizer, "Task summary StrRef:", 0, 0);
        taskSummaryResolved_ = addTextField(editorPanel, *editorSizer, "Resolved summary:", wxTE_READONLY | wxTE_MULTILINE);
        taskSummaryResolved_->SetMinSize(FromDIP(wxSize(-1, 80)));
        taskPreRef_ = addTextField(editorPanel, *editorSizer, "Summary before activation:", 0, 0);
        taskPreResolved_ = addTextField(editorPanel, *editorSizer, "Resolved pre-summary:", wxTE_READONLY | wxTE_MULTILINE);
        taskPreResolved_->SetMinSize(FromDIP(wxSize(-1, 65)));
        taskPostRef_ = addTextField(editorPanel, *editorSizer, "Summary after completion:", 0, 0);
        taskPostResolved_ = addTextField(editorPanel, *editorSizer, "Resolved post-summary:", wxTE_READONLY | wxTE_MULTILINE);
        taskPostResolved_->SetMinSize(FromDIP(wxSize(-1, 65)));

        auto* nextRow = new wxBoxSizer(wxHORIZONTAL);
        auto* nextLabel = new wxStaticText(editorPanel, wxID_ANY, "When group completes:");
        nextLabel->SetMinSize(FromDIP(wxSize(145, -1)));
        nextRow->Add(nextLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        taskNextGroup_ = new wxChoice(editorPanel, wxID_ANY);
        nextRow->Add(taskNextGroup_, 1, wxEXPAND);
        editorSizer->Add(nextRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));

        auto* state = new wxBoxSizer(wxHORIZONTAL);
        state->Add(new wxStaticText(editorPanel, wxID_ANY, "State:"), 0,
                   wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        taskComplete_ = new wxCheckBox(editorPanel, wxID_ANY, "Complete");
        state->Add(taskComplete_, 0);
        editorSizer->Add(state, 0, wxEXPAND | wxALL, FromDIP(6));

        auto* checks = new wxBoxSizer(wxHORIZONTAL);
        checks->Add(new wxStaticText(editorPanel, wxID_ANY, "Notifications:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        taskNotifyActive_ = new wxCheckBox(editorPanel, wxID_ANY, "On activation");
        taskNotifyComplete_ = new wxCheckBox(editorPanel, wxID_ANY, "On completion");
        checks->Add(taskNotifyActive_, 0, wxRIGHT, FromDIP(14));
        checks->Add(taskNotifyComplete_, 0);
        editorSizer->Add(checks, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
        auto* apply = new wxBoxSizer(wxHORIZONTAL);
        apply->AddStretchSpacer(1);
        apply->Add(new wxButton(editorPanel, ID_ApplyTask, "Apply task changes"), 0);
        editorSizer->Add(apply, 0, wxEXPAND | wxALL, FromDIP(6));
        editorPanel->SetSizer(editorSizer);

        splitter->SplitVertically(listPanel, editorPanel, FromDIP(650));
        splitter->SetMinimumPaneSize(FromDIP(260));
        auto* pageSizer = new wxBoxSizer(wxVERTICAL);
        pageSizer->Add(splitter, 1, wxEXPAND);
        page->SetSizer(pageSizer);
        notebook->AddPage(page, "Tasks", true);
    }

    void buildGroupPage(wxNotebook* notebook) {
        auto* page = new wxPanel(notebook);
        auto* splitter = new wxSplitterWindow(page, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                               wxSP_LIVE_UPDATE | wxSP_3D);
        auto* listPanel = new wxPanel(splitter);
        auto* editorPanel = new wxScrolledWindow(splitter, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
        editorPanel->SetScrollRate(0, FromDIP(12));

        auto* listSizer = new wxBoxSizer(wxVERTICAL);
        groupList_ = new wxListCtrl(listPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                    wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES | wxLC_VRULES);
        groupList_->AppendColumn("Index", wxLIST_FORMAT_RIGHT, FromDIP(65));
        groupList_->AppendColumn("Identifier", wxLIST_FORMAT_RIGHT, FromDIP(90));
        groupList_->AppendColumn("Active", wxLIST_FORMAT_CENTER, FromDIP(70));
        groupList_->AppendColumn("Completion rule", wxLIST_FORMAT_LEFT, FromDIP(145));
        groupList_->AppendColumn("Tasks", wxLIST_FORMAT_LEFT, FromDIP(210));
        groupList_->AppendColumn("On-complete script", wxLIST_FORMAT_LEFT, FromDIP(145));
        listSizer->Add(groupList_, 1, wxEXPAND | wxALL, FromDIP(6));
        auto* buttons = new wxBoxSizer(wxHORIZONTAL);
        buttons->Add(new wxButton(listPanel, ID_AddGroup, "Add task group"), 0, wxRIGHT, FromDIP(5));
        buttons->Add(new wxButton(listPanel, ID_DeleteGroup, "Delete task group"), 0);
        listSizer->Add(buttons, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
        listPanel->SetSizer(listSizer);

        auto* editorSizer = new wxStaticBoxSizer(wxVERTICAL, editorPanel, "Selected task group");
        groupIdentifier_ = addSpinField(editorPanel, *editorSizer, "Identifier:", 0, kMaxQstIdentifier);

        auto* groupState = new wxBoxSizer(wxHORIZONTAL);
        groupState->Add(new wxStaticText(editorPanel, wxID_ANY, "Group state:"), 0,
                        wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        groupActive_ = new wxCheckBox(editorPanel, wxID_ANY, "Active");
        groupState->Add(groupActive_, 0);
        editorSizer->Add(groupState, 0, wxEXPAND | wxALL, FromDIP(6));

        auto* modeRow = new wxBoxSizer(wxHORIZONTAL);
        auto* modeLabel = new wxStaticText(editorPanel, wxID_ANY, "Completion rule:");
        modeLabel->SetMinSize(FromDIP(wxSize(145, -1)));
        modeRow->Add(modeLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        groupCompletionMode_ = new wxChoice(editorPanel, wxID_ANY);
        groupCompletionMode_->Append("Any selected task completes the group");
        groupCompletionMode_->Append("All selected tasks must complete");
        groupCompletionMode_->SetSelection(0);
        modeRow->Add(groupCompletionMode_, 1, wxEXPAND);
        editorSizer->Add(modeRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));

        groupOnComplete_ = addTextField(editorPanel, *editorSizer, "On-complete script:", 0, 0);
        groupOnComplete_->SetHint("Optional script ResRef, up to 16 characters");

        editorSizer->Add(new wxStaticText(editorPanel, wxID_ANY, "Tasks in this group:"), 0,
                         wxLEFT | wxRIGHT | wxTOP, FromDIP(6));
        groupTasks_ = new wxCheckListBox(editorPanel, wxID_ANY);
        groupTasks_->SetMinSize(FromDIP(wxSize(-1, 220)));
        editorSizer->Add(groupTasks_, 1, wxEXPAND | wxALL, FromDIP(6));

        auto* note = new wxStaticText(
            editorPanel, wxID_ANY,
            "Task references are positions in TaskList. Jade stores them as signed bytes, so a QST can contain at most 128 tasks and 128 groups. A task may belong to one group.");
        note->Wrap(FromDIP(440));
        editorSizer->Add(note, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
        auto* apply = new wxBoxSizer(wxHORIZONTAL);
        apply->AddStretchSpacer(1);
        apply->Add(new wxButton(editorPanel, ID_ApplyGroup, "Apply group changes"), 0);
        editorSizer->Add(apply, 0, wxEXPAND | wxALL, FromDIP(6));
        editorPanel->SetSizer(editorSizer);

        splitter->SplitVertically(listPanel, editorPanel, FromDIP(650));
        splitter->SetMinimumPaneSize(FromDIP(260));
        auto* pageSizer = new wxBoxSizer(wxVERTICAL);
        pageSizer->Add(splitter, 1, wxEXPAND);
        page->SetSizer(pageSizer);
        notebook->AddPage(page, "Task groups", false);
    }

    void bindEvents() {
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { createNew(true); }, ID_New);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { openDialog({}); }, ID_Open);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { loadTlkDialog(); }, ID_LoadTlk);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { save(); }, ID_Save);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { saveAs(); }, ID_SaveAs);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { importDocument("xml"); }, ID_ImportXml);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { importDocument("json"); }, ID_ImportJson);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { exportDocument("xml"); }, ID_ExportXml);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { exportDocument("json"); }, ID_ExportJson);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { toggleDarkMode(); }, ID_DarkMode);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { showAbout(); }, wxID_ABOUT);

        Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { openDialog({}); }, ID_Open);
        Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { loadTlkDialog(); }, ID_LoadTlk);
        Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { save(); }, ID_Save);
        Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { saveAs(); }, ID_SaveAs);
        Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { applyQuest(); }, ID_ApplyQuest);
        Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { addTask(); }, ID_AddTask);
        Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { deleteTask(); }, ID_DeleteTask);
        Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { applyTask(); }, ID_ApplyTask);
        Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { addGroup(); }, ID_AddGroup);
        Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { deleteGroup(); }, ID_DeleteGroup);
        Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { applyGroup(); }, ID_ApplyGroup);

        taskList_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { loadSelectedTask(); });
        groupList_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { loadSelectedGroup(); });
        runtimeStateEnabled_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { updateRuntimeStateEnabled(); });
        Bind(wxEVT_CLOSE_WINDOW, &NeoQSTFrame::onClose, this);
    }

    GffFile& qst() { return *qst_; }
    const GffFile& qst() const { return *qst_; }

    bool confirmDiscardOrSave() {
        if (!qst().loaded() || !qst().dirty()) return true;
        const int answer = wxMessageBox(
            "The current QST has unsaved changes. Save them before continuing?",
            "Unsaved NeoQST changes", wxYES_NO | wxCANCEL | wxICON_QUESTION, this);
        if (answer == wxCANCEL) return false;
        if (answer == wxYES) return save();
        return true;
    }

    void createNew(bool prompt) {
        try {
            if (prompt && !confirmDiscardOrSave()) return;
            auto replacement = std::make_unique<GffFile>();
            initializeNewQst(*replacement);
            replacement->dirty(false);
            qst_ = std::move(replacement);
            updateAll();
            wxui::setStatusText(*this, "Created a new QST document.");
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
    }

    void openDialog(const std::filesystem::path& initialDirectory) {
        const auto path = wxui::chooseOpenFile(this, "Open Jade Empire QST", kQstWildcard, initialDirectory);
        if (path) openFile(*path);
    }

    void openFile(const std::filesystem::path& path) {
        try {
            if (!confirmDiscardOrSave()) return;
            auto replacement = std::make_unique<GffFile>();
            replacement->LoadFile(path);
            validateQst(*replacement);
            replacement->dirty(false);
            qst_ = std::move(replacement);
            settings_.addRecentFile(path);
            updateAll();
            wxui::setStatusText(*this, "Opened " + neosettings::pathToUtf8(path));
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
    }

    bool save() {
        try {
            if (!qst().loaded()) throw std::runtime_error("No QST document is loaded.");
            if (qst().filename().empty()) return saveAs();
            validateQst(qst());
            qst().SaveFile();
            qst().dirty(false);
            settings_.addRecentFile(qst().filename());
            updateTitleAndPaths();
            wxui::setStatusText(*this, "Saved " + neosettings::pathToUtf8(qst().filename()));
            return true;
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
            return false;
        }
    }

    bool saveAs() {
        try {
            if (!qst().loaded()) throw std::runtime_error("No QST document is loaded.");
            const std::string defaultName = qst().filename().empty()
                                                ? "untitled.qst"
                                                : neosettings::pathToUtf8(qst().filename().filename());
            auto path = wxui::chooseSaveFile(this, "Save Jade Empire QST as", kQstWildcard, defaultName);
            if (!path) return false;
            if (path->extension().empty()) *path += ".qst";
            validateQst(qst());
            qst().SaveFile(*path);
            qst().dirty(false);
            settings_.addRecentFile(*path);
            updateTitleAndPaths();
            wxui::setStatusText(*this, "Saved " + neosettings::pathToUtf8(*path));
            return true;
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
            return false;
        }
    }

    void loadTlkDialog() {
        const auto path = wxui::chooseOpenFile(this, "Load Jade Empire TLK", kTlkWildcard);
        if (!path) return;
        loadTlk(*path, true);
    }

    void loadTlk(const std::filesystem::path& path, bool reportErrors) {
        try {
            neotlk::TlkLookup replacement;
            replacement.load(path);
            tlk_ = std::move(replacement);
            tlkPathValue_ = path;
            settings_.setLastTlkPath(path);
            updateAll();
            wxui::setStatusText(*this, "Loaded TLK " + neosettings::pathToUtf8(path));
        } catch (const std::exception& ex) {
            if (reportErrors) wxui::showError(this, ex);
        }
    }

    void tryLoadCachedTlk() {
        const auto cached = settings_.lastTlkPath();
        if (cached && std::filesystem::is_regular_file(*cached)) loadTlk(*cached, false);
    }

    std::string resolve(std::optional<UInt32> strref) const {
        if (!strref) return {};
        if (!tlk_) return "StrRef " + std::to_string(*strref) + " (load a Jade TLK to resolve)";
        return tlk_->resolve(*strref).value_or("Missing StrRef " + std::to_string(*strref));
    }

    void updateTitleAndPaths() {
        const std::string name = qst().filename().empty()
                                     ? "Untitled.qst"
                                     : neosettings::pathToUtf8(qst().filename().filename());
        SetTitle(wxui::toWx(std::string("NeoQST - ") + name + (qst().dirty() ? " *" : "")));
        filePath_->ChangeValue(qst().filename().empty() ? wxString("(unsaved)") : neosettings::pathToWx(qst().filename()));
        tlkPath_->ChangeValue(tlkPathValue_.empty() ? wxString("(not loaded)") : neosettings::pathToWx(tlkPathValue_));
    }

    void updateAll() {
        updateTitleAndPaths();
        updateQuestFields();
        refreshTaskList();
        refreshGroupList();
    }

    void updateRuntimeStateEnabled() {
        if (runtimeStatePanel_ != nullptr && runtimeStateEnabled_ != nullptr) {
            runtimeStatePanel_->Enable(runtimeStateEnabled_->GetValue());
            runtimeStatePanel_->GetParent()->Layout();
        }
    }

    void updateQuestFields() {
        const GffStruct* root = qst().root();
        if (root == nullptr) return;
        const auto name = qstJadeStringRef(*root, "QuestName");
        const auto description = qstJadeStringRef(*root, "QuestDescription");
        questNameRef_->ChangeValue(wxui::toWx(strRefText(name)));
        questDescriptionRef_->ChangeValue(wxui::toWx(strRefText(description)));
        questNameResolved_->ChangeValue(wxui::toWx(resolve(name)));
        questDescriptionResolved_->ChangeValue(wxui::toWx(resolve(description)));

        const std::int32_t questType = qstInt(*root, "QuestType").value_or(0);
        questTypeValues_.clear();
        questType_->Clear();
        questType_->Append("0 - Type 0");
        questTypeValues_.push_back(0);
        questType_->Append("1 - Type 1");
        questTypeValues_.push_back(1);
        if (questType == 0 || questType == 1) {
            questType_->SetSelection(questType);
        } else {
            questType_->Append(wxui::toWx("Existing value " + std::to_string(questType) +
                                          " (runtime treats nonzero as Type 1)"));
            questTypeValues_.push_back(questType);
            questType_->SetSelection(static_cast<int>(questTypeValues_.size() - 1));
        }

        const bool hasRuntimeState = root->GetFieldByLabel("QuestResRef") != nullptr ||
                                     root->GetFieldByLabel("QuestActive") != nullptr ||
                                     root->GetFieldByLabel("QuestComplete") != nullptr ||
                                     root->GetFieldByLabel("QuestUpdated") != nullptr ||
                                     root->GetFieldByLabel("TimeHi") != nullptr ||
                                     root->GetFieldByLabel("TimeLo") != nullptr;
        runtimeStateEnabled_->SetValue(hasRuntimeState);
        questResRef_->ChangeValue(wxui::toWx(qstResRef(*root, "QuestResRef").value_or(std::string{})));
        questActive_->SetValue(qstInt(*root, "QuestActive").value_or(0) != 0);
        questComplete_->SetValue(qstInt(*root, "QuestComplete").value_or(0) != 0);
        questUpdated_->SetValue(qstInt(*root, "QuestUpdated").value_or(0) != 0);
        questTimeHi_->ChangeValue(wxui::toWx(std::to_string(qstDword(*root, "TimeHi").value_or(0))));
        questTimeLo_->ChangeValue(wxui::toWx(std::to_string(qstDword(*root, "TimeLo").value_or(0))));
        updateRuntimeStateEnabled();
    }

    std::string describeNextGroup(std::int32_t value) const {
        if (value == kQstNoNextGroup) return "No transition";
        if (value == kQstCompleteQuest) return "Complete quest";
        const GffList& groups = requireQstTaskGroupList(qst());
        if (value >= 0 && static_cast<std::size_t>(value) < groups.count()) {
            const GffStruct& group = listStruct(groups, static_cast<std::size_t>(value), "task group");
            return "Group " + std::to_string(value) + " (ID " +
                   std::to_string(qstEffectiveIdentifier(group)) + ")";
        }
        return "Invalid group index " + std::to_string(value);
    }

    void rebuildNextGroupChoices(std::int32_t selectedValue) {
        taskNextGroup_->Clear();
        taskNextGroupValues_.clear();
        taskNextGroup_->Append("-1 - No transition");
        taskNextGroupValues_.push_back(kQstNoNextGroup);
        taskNextGroup_->Append("-2 - Complete and deactivate quest");
        taskNextGroupValues_.push_back(kQstCompleteQuest);
        const GffList& groups = requireQstTaskGroupList(qst());
        for (std::size_t index = 0; index < groups.count(); ++index) {
            const GffStruct& group = listStruct(groups, index, "task group");
            taskNextGroup_->Append(wxui::toWx(std::to_string(index) + " - Group ID " +
                                               std::to_string(qstEffectiveIdentifier(group))));
            taskNextGroupValues_.push_back(static_cast<std::int32_t>(index));
        }
        auto found = std::find(taskNextGroupValues_.begin(), taskNextGroupValues_.end(), selectedValue);
        if (found == taskNextGroupValues_.end()) {
            taskNextGroup_->Append(wxui::toWx("Existing invalid value " + std::to_string(selectedValue)));
            taskNextGroupValues_.push_back(selectedValue);
            taskNextGroup_->SetSelection(static_cast<int>(taskNextGroupValues_.size() - 1));
        } else {
            taskNextGroup_->SetSelection(static_cast<int>(std::distance(taskNextGroupValues_.begin(), found)));
        }
    }

    std::string groupTaskSummary(const std::vector<std::int32_t>& indices) const {
        std::ostringstream output;
        for (std::size_t index = 0; index < indices.size(); ++index) {
            if (index != 0) output << ", ";
            output << indices[index];
        }
        return output.str();
    }

    void refreshTaskList(std::optional<std::size_t> select = std::nullopt) {
        taskList_->Freeze();
        taskList_->DeleteAllItems();
        const GffList& tasks = requireQstTaskList(qst());
        for (std::size_t index = 0; index < tasks.count(); ++index) {
            const GffStruct& task = listStruct(tasks, index, "task");
            const long row = taskList_->InsertItem(taskList_->GetItemCount(), wxui::toWx(std::to_string(index)));
            taskList_->SetItem(row, 1, wxui::toWx(std::to_string(qstEffectiveIdentifier(task))));
            taskList_->SetItem(row, 2, wxui::toWx(resolve(qstJadeStringRef(task, "TaskName"))));
            taskList_->SetItem(row, 3, qstInt(task, "Complete").value_or(0) != 0 ? "Yes" : "No");
            taskList_->SetItem(row, 4, wxui::toWx(describeNextGroup(qstInt(task, "NextTaskGroup").value_or(kQstNoNextGroup))));
            taskList_->SetItem(row, 5, qstInt(task, "NotifyActive").value_or(0) != 0 ? "Yes" : "No");
            taskList_->SetItem(row, 6, qstInt(task, "NotifyComplete").value_or(0) != 0 ? "Yes" : "No");
            taskList_->SetItemData(row, static_cast<long>(index));
            wxui::styleListRow(*taskList_, row, darkMode_);
        }
        taskList_->Thaw();
        if (!select && tasks.count() != 0) select = 0;
        if (select && *select < tasks.count()) wxui::selectRow(*taskList_, static_cast<long>(*select));
        else clearTaskFields();
    }

    void refreshGroupList(std::optional<std::size_t> select = std::nullopt) {
        groupList_->Freeze();
        groupList_->DeleteAllItems();
        const GffList& groups = requireQstTaskGroupList(qst());
        for (std::size_t index = 0; index < groups.count(); ++index) {
            const GffStruct& group = listStruct(groups, index, "task group");
            const long row = groupList_->InsertItem(groupList_->GetItemCount(), wxui::toWx(std::to_string(index)));
            groupList_->SetItem(row, 1, wxui::toWx(std::to_string(qstEffectiveIdentifier(group))));
            groupList_->SetItem(row, 2, qstInt(group, "Active").value_or(0) != 0 ? "Yes" : "No");
            groupList_->SetItem(row, 3, qstInt(group, "ANDGroup").value_or(0) != 0 ? "All tasks" : "Any task");
            groupList_->SetItem(row, 4, wxui::toWx(groupTaskSummary(qstGroupTaskIndices(qst(), index))));
            groupList_->SetItem(row, 5, wxui::toWx(qstResRef(group, "OnComplete").value_or(std::string{})));
            groupList_->SetItemData(row, static_cast<long>(index));
            wxui::styleListRow(*groupList_, row, darkMode_);
        }
        groupList_->Thaw();
        if (!select && groups.count() != 0) select = 0;
        if (select && *select < groups.count()) wxui::selectRow(*groupList_, static_cast<long>(*select));
        else clearGroupFields();
    }

    std::optional<std::size_t> selectedIndex(const wxListCtrl& list) const {
        const long row = wxui::selectedRow(list);
        if (row < 0) return std::nullopt;
        const long data = list.GetItemData(row);
        if (data < 0) return std::nullopt;
        return static_cast<std::size_t>(data);
    }

    void loadSelectedTask() {
        try {
            const auto index = selectedIndex(*taskList_);
            if (!index) { clearTaskFields(); return; }
            const GffStruct& task = listStruct(requireQstTaskList(qst()), *index, "task");
            taskIdentifier_->SetValue(qstEffectiveIdentifier(task));
            const auto name = qstJadeStringRef(task, "TaskName");
            const auto summary = qstJadeStringRef(task, "TaskSummary");
            const auto pre = qstJadeStringRef(task, "QuestSummaryPre");
            const auto post = qstJadeStringRef(task, "QuestSummaryPost");
            taskNameRef_->ChangeValue(wxui::toWx(strRefText(name)));
            taskNameResolved_->ChangeValue(wxui::toWx(resolve(name)));
            taskSummaryRef_->ChangeValue(wxui::toWx(strRefText(summary)));
            taskSummaryResolved_->ChangeValue(wxui::toWx(resolve(summary)));
            taskPreRef_->ChangeValue(wxui::toWx(strRefText(pre)));
            taskPreResolved_->ChangeValue(wxui::toWx(resolve(pre)));
            taskPostRef_->ChangeValue(wxui::toWx(strRefText(post)));
            taskPostResolved_->ChangeValue(wxui::toWx(resolve(post)));
            rebuildNextGroupChoices(qstInt(task, "NextTaskGroup").value_or(kQstNoNextGroup));
            taskComplete_->SetValue(qstInt(task, "Complete").value_or(0) != 0);
            taskNotifyActive_->SetValue(qstInt(task, "NotifyActive").value_or(0) != 0);
            taskNotifyComplete_->SetValue(qstInt(task, "NotifyComplete").value_or(0) != 0);
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
    }

    void clearTaskFields() {
        if (taskIdentifier_) taskIdentifier_->SetValue(0);
        for (wxTextCtrl* control : {taskNameRef_, taskNameResolved_, taskSummaryRef_,
                                    taskSummaryResolved_, taskPreRef_, taskPreResolved_, taskPostRef_,
                                    taskPostResolved_}) {
            if (control) control->ChangeValue(wxEmptyString);
        }
        if (taskNextGroup_) {
            taskNextGroup_->Clear();
            taskNextGroupValues_.clear();
        }
        if (taskComplete_) taskComplete_->SetValue(false);
        if (taskNotifyActive_) taskNotifyActive_->SetValue(false);
        if (taskNotifyComplete_) taskNotifyComplete_->SetValue(false);
    }

    void populateGroupTasks(const std::vector<std::int32_t>& checked) {
        groupTasks_->Clear();
        std::unordered_set<std::int32_t> checkedSet(checked.begin(), checked.end());
        const GffList& tasks = requireQstTaskList(qst());
        for (std::size_t index = 0; index < tasks.count(); ++index) {
            const GffStruct& task = listStruct(tasks, index, "task");
            std::string label = std::to_string(index) + " - ID " +
                                std::to_string(qstEffectiveIdentifier(task));
            const std::string resolvedName = resolve(qstJadeStringRef(task, "TaskName"));
            if (!resolvedName.empty()) label += " - " + resolvedName;
            groupTasks_->Append(wxui::toWx(label));
            if (checkedSet.find(static_cast<std::int32_t>(index)) != checkedSet.end()) {
                groupTasks_->Check(static_cast<unsigned int>(index), true);
            }
        }
    }

    void loadSelectedGroup() {
        try {
            const auto index = selectedIndex(*groupList_);
            if (!index) { clearGroupFields(); return; }
            const GffStruct& group = listStruct(requireQstTaskGroupList(qst()), *index, "task group");
            groupIdentifier_->SetValue(qstEffectiveIdentifier(group));
            groupActive_->SetValue(qstInt(group, "Active").value_or(0) != 0);
            groupCompletionMode_->SetSelection(qstInt(group, "ANDGroup").value_or(0) != 0 ? 1 : 0);
            groupOnComplete_->ChangeValue(wxui::toWx(qstResRef(group, "OnComplete").value_or(std::string{})));
            populateGroupTasks(qstGroupTaskIndices(qst(), *index));
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
    }

    void clearGroupFields() {
        if (groupIdentifier_) groupIdentifier_->SetValue(0);
        if (groupActive_) groupActive_->SetValue(false);
        if (groupCompletionMode_) groupCompletionMode_->SetSelection(0);
        if (groupOnComplete_) groupOnComplete_->ChangeValue(wxEmptyString);
        if (groupTasks_) groupTasks_->Clear();
    }

    void applyQuest() {
        try {
            GffStruct* root = qst().root();
            if (root == nullptr) throw std::runtime_error("The QST root struct is missing.");
            setQstJadeStringRef(*root, "QuestName", 4u, parseStrRef(*questNameRef_, "Quest name StrRef"));
            setQstJadeStringRef(*root, "QuestDescription", 4u,
                                parseStrRef(*questDescriptionRef_, "Quest description StrRef"));

            const int typeSelection = questType_->GetSelection();
            if (typeSelection == wxNOT_FOUND || static_cast<std::size_t>(typeSelection) >= questTypeValues_.size()) {
                throw std::runtime_error("Select a quest type.");
            }
            const std::int32_t questType = questTypeValues_[static_cast<std::size_t>(typeSelection)];
            if (root->GetFieldByLabel("QuestType") != nullptr || questType != 0) {
                setQstInt(*root, "QuestType", questType);
            }

            if (runtimeStateEnabled_->GetValue()) {
                setQstOptionalResRef(*root, "QuestResRef", parseOptionalResRef(*questResRef_, "Quest resource reference"));
                setQstInt(*root, "QuestActive", questActive_->GetValue() ? 1 : 0);
                setQstInt(*root, "QuestComplete", questComplete_->GetValue() ? 1 : 0);
                setQstInt(*root, "QuestUpdated", questUpdated_->GetValue() ? 1 : 0);
                setQstDword(*root, "TimeHi", parseUInt32(*questTimeHi_, "Timestamp high"));
                setQstDword(*root, "TimeLo", parseUInt32(*questTimeLo_, "Timestamp low"));
            } else {
                setQstOptionalResRef(*root, "QuestResRef", std::nullopt);
                setQstOptionalInt(*root, "QuestActive", std::nullopt);
                setQstOptionalInt(*root, "QuestComplete", std::nullopt);
                setQstOptionalInt(*root, "QuestUpdated", std::nullopt);
                setQstOptionalDword(*root, "TimeHi", std::nullopt);
                setQstOptionalDword(*root, "TimeLo", std::nullopt);
            }

            qst().dirty(true);
            validateQst(qst());
            updateAll();
            wxui::setStatusText(*this, "Applied quest metadata.");
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
    }

    void applyTask() {
        try {
            const auto index = selectedIndex(*taskList_);
            if (!index) throw std::runtime_error("Select a task first.");
            changeQstTaskIdentifier(qst(), *index, taskIdentifier_->GetValue());
            GffStruct& task = listStruct(requireQstTaskList(qst()), *index, "task");
            setQstJadeStringRef(task, "TaskName", 4u, parseStrRef(*taskNameRef_, "Task name StrRef"));
            setQstJadeStringRef(task, "TaskSummary", 4u, parseStrRef(*taskSummaryRef_, "Task summary StrRef"));
            setQstJadeStringRef(task, "QuestSummaryPre", 4u,
                                parseStrRef(*taskPreRef_, "Quest summary before activation StrRef"), true);
            setQstJadeStringRef(task, "QuestSummaryPost", 4u,
                                parseStrRef(*taskPostRef_, "Quest summary after completion StrRef"), true);

            const int nextSelection = taskNextGroup_->GetSelection();
            if (nextSelection == wxNOT_FOUND ||
                static_cast<std::size_t>(nextSelection) >= taskNextGroupValues_.size()) {
                throw std::runtime_error("Select what should happen when this task's group completes.");
            }
            setQstInt(task, "NextTaskGroup", taskNextGroupValues_[static_cast<std::size_t>(nextSelection)]);
            setQstOptionalInt(task, "Complete", taskComplete_->GetValue() ? std::optional<std::int32_t>{1}
                                                                           : std::nullopt);
            setQstInt(task, "NotifyActive", taskNotifyActive_->GetValue() ? 1 : 0);
            setQstInt(task, "NotifyComplete", taskNotifyComplete_->GetValue() ? 1 : 0);
            qst().dirty(true);
            validateQst(qst());
            refreshTaskList(*index);
            refreshGroupList(selectedIndex(*groupList_));
            updateTitleAndPaths();
            wxui::setStatusText(*this, "Applied task changes.");
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
    }

    void addTask() {
        try {
            const auto result = appendQstTask(qst());
            refreshTaskList(result.index);
            refreshGroupList(selectedIndex(*groupList_));
            updateTitleAndPaths();
            wxui::setStatusText(*this, "Added task " + std::to_string(result.identifier) + ".");
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
    }

    void deleteTask() {
        try {
            const auto index = selectedIndex(*taskList_);
            if (!index) throw std::runtime_error("Select a task first.");
            if (!wxui::confirm(this, "Delete task", "Delete the selected task and repair all TaskIndexList positions?")) return;
            const auto result = deleteQstTask(qst(), *index);
            refreshTaskList(result.nextSelection);
            refreshGroupList(selectedIndex(*groupList_));
            updateTitleAndPaths();
            wxui::setStatusText(*this, "Deleted task " + std::to_string(result.identifier) +
                                       "; repaired " + std::to_string(result.repairedReferences) + " list references.");
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
    }

    void applyGroup() {
        try {
            const auto index = selectedIndex(*groupList_);
            if (!index) throw std::runtime_error("Select a task group first.");
            changeQstGroupIdentifier(qst(), *index, groupIdentifier_->GetValue());

            std::vector<std::int32_t> taskIndices;
            for (unsigned int taskIndex = 0; taskIndex < groupTasks_->GetCount(); ++taskIndex) {
                if (groupTasks_->IsChecked(taskIndex)) {
                    taskIndices.push_back(static_cast<std::int32_t>(taskIndex));
                }
            }
            replaceQstGroupTaskIndices(qst(), *index, taskIndices);

            GffStruct& group = listStruct(requireQstTaskGroupList(qst()), *index, "task group");
            setQstOptionalInt(group, "Active", groupActive_->GetValue() ? std::optional<std::int32_t>{1}
                                                                         : std::nullopt);
            setQstOptionalInt(group, "ANDGroup", groupCompletionMode_->GetSelection() == 1
                                                     ? std::optional<std::int32_t>{1}
                                                     : std::nullopt);
            setQstOptionalResRef(group, "OnComplete",
                                 parseOptionalResRef(*groupOnComplete_, "On-complete script"));
            qst().dirty(true);
            validateQst(qst());
            refreshGroupList(*index);
            refreshTaskList(selectedIndex(*taskList_));
            updateTitleAndPaths();
            wxui::setStatusText(*this, "Applied task-group changes.");
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
    }

    void addGroup() {
        try {
            const auto result = appendQstTaskGroup(qst());
            refreshGroupList(result.index);
            refreshTaskList(selectedIndex(*taskList_));
            updateTitleAndPaths();
            wxui::setStatusText(*this, "Added task group " + std::to_string(result.identifier) + ".");
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
    }

    void deleteGroup() {
        try {
            const auto index = selectedIndex(*groupList_);
            if (!index) throw std::runtime_error("Select a task group first.");
            if (!wxui::confirm(this, "Delete task group", "Delete the selected task group and repair NextTaskGroup positions?")) return;
            const auto result = deleteQstTaskGroup(qst(), *index);
            refreshGroupList(result.nextSelection);
            refreshTaskList(selectedIndex(*taskList_));
            updateTitleAndPaths();
            wxui::setStatusText(*this, "Deleted task group " + std::to_string(result.identifier) +
                                       "; repaired " + std::to_string(result.repairedReferences) + " task transitions.");
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
    }

    void importDocument(const std::string& format) {
        try {
            if (!confirmDiscardOrSave()) return;
            const char* wildcard = format == "xml" ? kXmlWildcard : kJsonWildcard;
            const auto path = wxui::chooseOpenFile(this, "Import QST " + format, wildcard);
            if (!path) return;
            auto replacement = std::make_unique<GffFile>();
            if (format == "xml") LoadGffXml(*replacement, readTextFile(*path));
            else LoadGffXml(*replacement, gffJsonToXml(readTextFile(*path)));
            validateQst(*replacement);
            replacement->dirty(true);
            qst_ = std::move(replacement);
            updateAll();
            wxui::setStatusText(*this, "Imported " + format + " QST document.");
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
    }

    void exportDocument(const std::string& format) {
        try {
            validateQst(qst());
            const char* wildcard = format == "xml" ? kXmlWildcard : kJsonWildcard;
            std::string defaultName = qst().filename().empty() ? "quest" : neosettings::pathToUtf8(qst().filename().stem());
            defaultName += "." + format;
            const auto path = wxui::chooseSaveFile(this, "Export QST " + format, wildcard, defaultName);
            if (!path) return;
            const std::string xml = ToGffXml(qst());
            writeTextFile(*path, format == "xml" ? xml : gffXmlToJson(xml));
            wxui::setStatusText(*this, "Exported " + neosettings::pathToUtf8(*path));
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
    }

    void toggleDarkMode() {
        darkMode_ = darkModeItem_ && darkModeItem_->IsChecked();
        settings_.setDarkMode(darkMode_);
        wxui::applyTheme(this, darkMode_);
        refreshTaskList(selectedIndex(*taskList_));
        refreshGroupList(selectedIndex(*groupList_));
    }

    void showAbout() {
        wxAboutDialogInfo info;
        info.SetName("NeoQST");
        info.SetVersion("1.0.0");
        info.SetDescription("Jade Empire QST/QST2 quest editor");
        info.SetIcon(GetIcon());
        wxAboutBox(info, this);
    }

    void onClose(wxCloseEvent& event) {
        if (event.CanVeto() && !confirmDiscardOrSave()) {
            event.Veto();
            return;
        }
        settings_.saveWindowPlacement(*this);
        event.Skip();
    }

    neosettings::AppSettings settings_;
    std::unique_ptr<neogames::OpenGameDirectoryMenu> gameDirectoryMenu_;
    std::unique_ptr<GffFile> qst_ = std::make_unique<GffFile>();
    std::optional<neotlk::TlkLookup> tlk_;
    std::filesystem::path tlkPathValue_;
    bool darkMode_ = false;

    wxMenuItem* darkModeItem_ = nullptr;
    wxTextCtrl* filePath_ = nullptr;
    wxTextCtrl* tlkPath_ = nullptr;
    wxChoice* questType_ = nullptr;
    std::vector<std::int32_t> questTypeValues_;
    wxTextCtrl* questNameRef_ = nullptr;
    wxTextCtrl* questNameResolved_ = nullptr;
    wxTextCtrl* questDescriptionRef_ = nullptr;
    wxTextCtrl* questDescriptionResolved_ = nullptr;
    wxCheckBox* runtimeStateEnabled_ = nullptr;
    wxPanel* runtimeStatePanel_ = nullptr;
    wxTextCtrl* questResRef_ = nullptr;
    wxCheckBox* questActive_ = nullptr;
    wxCheckBox* questComplete_ = nullptr;
    wxCheckBox* questUpdated_ = nullptr;
    wxTextCtrl* questTimeHi_ = nullptr;
    wxTextCtrl* questTimeLo_ = nullptr;

    wxListCtrl* taskList_ = nullptr;
    wxSpinCtrl* taskIdentifier_ = nullptr;
    wxTextCtrl* taskNameRef_ = nullptr;
    wxTextCtrl* taskNameResolved_ = nullptr;
    wxTextCtrl* taskSummaryRef_ = nullptr;
    wxTextCtrl* taskSummaryResolved_ = nullptr;
    wxTextCtrl* taskPreRef_ = nullptr;
    wxTextCtrl* taskPreResolved_ = nullptr;
    wxTextCtrl* taskPostRef_ = nullptr;
    wxTextCtrl* taskPostResolved_ = nullptr;
    wxChoice* taskNextGroup_ = nullptr;
    std::vector<std::int32_t> taskNextGroupValues_;
    wxCheckBox* taskComplete_ = nullptr;
    wxCheckBox* taskNotifyActive_ = nullptr;
    wxCheckBox* taskNotifyComplete_ = nullptr;

    wxListCtrl* groupList_ = nullptr;
    wxSpinCtrl* groupIdentifier_ = nullptr;
    wxCheckBox* groupActive_ = nullptr;
    wxChoice* groupCompletionMode_ = nullptr;
    wxTextCtrl* groupOnComplete_ = nullptr;
    wxCheckListBox* groupTasks_ = nullptr;
};

class NeoQSTApp final : public wxApp {
public:
    bool OnInit() override {
#if wxCHECK_VERSION(3, 3, 0)
        SetAppearance(Appearance::System);
#endif
        auto* frame = new NeoQSTFrame();
        frame->Show(true);
        if (argc > 1) {
            frame->openFromCommandLine(neosettings::pathFromWx(wxString(argv[1])));
        }
        return true;
    }
};

} // namespace

wxIMPLEMENT_APP(NeoQSTApp);
