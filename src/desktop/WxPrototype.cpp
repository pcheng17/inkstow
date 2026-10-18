#include "Session.h"

#include <wx/dirdlg.h>
#include <wx/listbox.h>
#include <wx/splitter.h>
#include <wx/textctrl.h>
#include <wx/wx.h>

#include <string>

namespace
{
wxString fromUtf8(const std::string& value)
{
    return wxString::FromUTF8(value.data(), value.size());
}

std::string toUtf8(const wxString& value)
{
    const auto bytes = value.ToUTF8();
    return std::string(bytes.data(), bytes.length());
}

class PrototypeFrame final : public wxFrame
{
public:
    PrototypeFrame()
        : wxFrame(
              nullptr,
              wxID_ANY,
              "Inkstow - wxWidgets experiment",
              wxDefaultPosition,
              wxSize(980, 680))
    {
        auto* file = new wxMenu;
        file->Append(wxID_OPEN, "&Open vault...\tCtrl+O");
        file->Append(wxID_SAVE, "&Save\tCtrl+S");
        file->AppendSeparator();
        file->Append(wxID_EXIT, "&Quit");
        auto* bar = new wxMenuBar;
        bar->Append(file, "&File");
        SetMenuBar(bar);
        CreateStatusBar();
        SetStatusText("Open a disposable vault to explore the native widgets");

        auto* splitter = new wxSplitterWindow(
            this,
            wxID_ANY,
            wxDefaultPosition,
            wxDefaultSize,
            wxSP_LIVE_UPDATE | wxSP_3D);
        mNotes = new wxListBox(splitter, wxID_ANY);
        mEditor = new wxTextCtrl(
            splitter,
            wxID_ANY,
            wxEmptyString,
            wxDefaultPosition,
            wxDefaultSize,
            wxTE_MULTILINE | wxTE_RICH2);
        mEditor->Enable(false);
        splitter->SetMinimumPaneSize(130);
        splitter->SplitVertically(mNotes, mEditor, 260);
        splitter->SetSashGravity(0.0);

        Bind(wxEVT_MENU, [this](wxCommandEvent&) { openPicker(); }, wxID_OPEN);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { save(); }, wxID_SAVE);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
        mNotes->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { selectNote(); });
        mEditor->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
            mSession.setContent(toUtf8(mEditor->GetValue()));
            updateTitle();
        });
        Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
            if (confirmLeave()) {
                event.Skip();
            } else if (event.CanVeto()) {
                event.Veto();
            }
        });
    }

    void openVault(const wxString& folder)
    {
        const std::string previousContent = mSession.getContent();
        bool discarded = false;
        if (!confirmLeave(&discarded)) {
            return;
        }
        std::string error;
        if (!mSession.openVault(toUtf8(folder), &error)) {
            restoreFailedNavigation(discarded, previousContent);
            showError(error);
            return;
        }
        mFolder = folder;
        mNotes->Clear();
        for (const auto& note : mSession.getNotes()) {
            mNotes->Append(fromUtf8(note));
        }
        mEditor->ChangeValue(wxEmptyString);
        mEditor->Enable(false);
        updateTitle();
        SetStatusText(wxString::Format("%zu notes in %s", mSession.getNotes().size(), folder));
    }

private:
    void showError(const std::string& error)
    {
        wxMessageBox(fromUtf8(error), "Inkstow", wxOK | wxICON_ERROR, this);
    }

    bool save()
    {
        if (!mSession.isDirty()) {
            return true;
        }
        std::string error;
        if (!mSession.save(&error)) {
            showError(error);
            return false;
        }
        updateTitle();
        SetStatusText("Saved " + fromUtf8(mSession.getPath()));
        return true;
    }

    bool confirmLeave(bool* discarded = nullptr)
    {
        if (discarded != nullptr) {
            *discarded = false;
        }
        if (!mSession.isDirty()) {
            return true;
        }
        const int choice = wxMessageBox(
            "Save changes before leaving this note?",
            "Unsaved changes",
            wxYES_NO | wxCANCEL | wxICON_QUESTION,
            this);
        if (choice == wxYES) {
            return save();
        }
        if (choice == wxNO) {
            mSession.discardEdits();
            if (discarded != nullptr) {
                *discarded = true;
            }
            return true;
        }
        return false;
    }

    void restoreFailedNavigation(bool discarded, const std::string& previousContent)
    {
        if (discarded) {
            mSession.setContent(previousContent);
        }
        updateTitle();
    }

    void openPicker()
    {
        wxDirDialog picker(this, "Open a disposable vault", mFolder, wxDD_DEFAULT_STYLE);
        if (picker.ShowModal() == wxID_OK) {
            openVault(picker.GetPath());
        }
    }

    void selectNote()
    {
        const int selected = mNotes->GetSelection();
        if (selected == wxNOT_FOUND) {
            return;
        }
        const std::string previousContent = mSession.getContent();
        bool discarded = false;
        if (!confirmLeave(&discarded)) {
            mNotes->SetSelection(
                mSession.getPath().empty() ? wxNOT_FOUND
                                           : mNotes->FindString(fromUtf8(mSession.getPath())));
            return;
        }
        std::string error;
        if (!mSession.selectNote(toUtf8(mNotes->GetString(selected)), &error)) {
            restoreFailedNavigation(discarded, previousContent);
            showError(error);
            mNotes->SetSelection(
                mSession.getPath().empty() ? wxNOT_FOUND
                                           : mNotes->FindString(fromUtf8(mSession.getPath())));
            return;
        }
        mEditor->Enable(true);
        mEditor->ChangeValue(fromUtf8(mSession.getContent()));
        updateTitle();
        SetStatusText(fromUtf8(mSession.getPath()));
    }

    void updateTitle()
    {
        const wxString note = mSession.getPath().empty() ? "No note" : fromUtf8(mSession.getPath());
        SetTitle((mSession.isDirty() ? "* " : "") + note + " - Inkstow (wxWidgets experiment)");
    }

    inkstow::portable::Session mSession;
    wxListBox* mNotes = nullptr;
    wxTextCtrl* mEditor = nullptr;
    wxString mFolder;
};

class PrototypeApp final : public wxApp
{
public:
    bool OnInit() override
    {
        if (argc > 2) {
            wxMessageBox("Usage: inkstow-wx [disposable-vault-directory]", "Inkstow");
            return false;
        }
        auto* frame = new PrototypeFrame;
        frame->Show();
        if (argc == 2) {
            frame->openVault(argv[1]);
        }
        return true;
    }
};
} // namespace

wxIMPLEMENT_APP(PrototypeApp);
