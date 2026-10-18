#include "Session.h"

#include <wx/dirdlg.h>
#include <wx/listbox.h>
#include <wx/notebook.h>
#include <wx/splitter.h>
#include <wx/textctrl.h>
#include <wx/wx.h>

#include <memory>
#include <string>
#include <vector>

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

struct NoteTab
{
    inkstow::portable::Session session;
    wxTextCtrl* editor = nullptr; // Owned by the notebook.
};

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
        file->Append(wxID_CLOSE, "&Close tab\tCtrl+W");
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
        mNotebook = new wxNotebook(splitter, wxID_ANY);
        splitter->SetMinimumPaneSize(130);
        splitter->SplitVertically(mNotes, mNotebook, 260);
        splitter->SetSashGravity(0.0);

        Bind(wxEVT_MENU, [this](wxCommandEvent&) { openPicker(); }, wxID_OPEN);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { saveActive(); }, wxID_SAVE);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { closeTab(); }, wxID_CLOSE);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
        mNotes->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { openSelectedNote(); });
        mNotebook->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent& event) {
            if (!mChangingPages) {
                updateActive();
            }
            event.Skip();
        });
        Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
            if (confirmAllTabs()) {
                event.Skip();
            } else if (event.CanVeto()) {
                event.Veto();
            }
        });
    }

    void openVault(const wxString& folder)
    {
        inkstow::portable::Session next;
        std::string error;
        if (!next.openVault(toUtf8(folder), &error)) {
            showError(error);
            return;
        }
        if (!confirmAllTabs()) {
            return;
        }
        mChangingPages = true;
        mNotebook->DeleteAllPages();
        mTabs.clear();
        mChangingPages = false;
        mBrowser = std::move(next);
        mFolder = folder;
        mNotes->Clear();
        for (const auto& note : mBrowser.getNotes()) {
            mNotes->Append(fromUtf8(note));
        }
        updateActive();
        SetStatusText(wxString::Format("%zu notes in %s", mBrowser.getNotes().size(), folder));
    }

private:
    void showError(const std::string& error)
    {
        wxMessageBox(fromUtf8(error), "Inkstow", wxOK | wxICON_ERROR, this);
    }

    NoteTab* activeTab() const
    {
        const int index = mNotebook->GetSelection();
        return index == wxNOT_FOUND ? nullptr : mTabs[static_cast<std::size_t>(index)].get();
    }

    void updateTabLabel(NoteTab& tab)
    {
        for (std::size_t i = 0; i < mTabs.size(); ++i) {
            if (mTabs[i].get() == &tab) {
                mNotebook->SetPageText(
                    i,
                    (tab.session.isDirty() ? "* " : "") + fromUtf8(tab.session.getPath()));
                break;
            }
        }
        updateActive();
    }

    void updateActive()
    {
        const NoteTab* tab = activeTab();
        const wxString note = tab == nullptr ? "No note" : fromUtf8(tab->session.getPath());
        SetTitle(
            (tab != nullptr && tab->session.isDirty() ? "* " : "") + note +
            " - Inkstow (wxWidgets experiment)");
        mNotes->SetSelection(tab == nullptr ? wxNOT_FOUND : mNotes->FindString(note));
        if (tab != nullptr) {
            SetStatusText(note);
        }
    }

    bool saveTab(NoteTab& tab)
    {
        if (!tab.session.isDirty()) {
            return true;
        }
        std::string error;
        if (!tab.session.save(&error)) {
            showError(tab.session.getPath() + ": " + error);
            return false;
        }
        updateTabLabel(tab);
        SetStatusText("Saved " + fromUtf8(tab.session.getPath()));
        return true;
    }

    void saveActive()
    {
        if (auto* tab = activeTab()) {
            saveTab(*tab);
        }
    }

    // Discard is only applied after the destination is ready (or the tab is removed).
    bool confirmTab(NoteTab& tab)
    {
        if (!tab.session.isDirty()) {
            return true;
        }
        const int choice = wxMessageBox(
            "Save changes to " + fromUtf8(tab.session.getPath()) + "?",
            "Unsaved changes",
            wxYES_NO | wxCANCEL | wxICON_QUESTION,
            this);
        return choice == wxNO || (choice == wxYES && saveTab(tab));
    }

    bool confirmAllTabs()
    {
        bool dirty = false;
        for (const auto& tab : mTabs) {
            dirty |= tab->session.isDirty();
        }
        if (!dirty) {
            return true;
        }
        const int choice = wxMessageBox(
            "Save changes in open tabs before leaving this vault?",
            "Unsaved changes",
            wxYES_NO | wxCANCEL | wxICON_QUESTION,
            this);
        if (choice == wxNO) {
            return true;
        }
        if (choice != wxYES) {
            return false;
        }
        for (const auto& tab : mTabs) {
            if (!saveTab(*tab)) {
                return false;
            }
        }
        return true;
    }

    void closeTab()
    {
        const int selected = mNotebook->GetSelection();
        if (selected == wxNOT_FOUND || !confirmTab(*mTabs[static_cast<std::size_t>(selected)])) {
            return;
        }
        mChangingPages = true;
        mNotebook->DeletePage(static_cast<std::size_t>(selected));
        mTabs.erase(mTabs.begin() + selected);
        mChangingPages = false;
        updateActive();
    }

    void openPicker()
    {
        wxDirDialog picker(this, "Open a disposable vault", mFolder, wxDD_DEFAULT_STYLE);
        if (picker.ShowModal() == wxID_OK) {
            openVault(picker.GetPath());
        }
    }

    void openSelectedNote()
    {
        const int selected = mNotes->GetSelection();
        if (selected == wxNOT_FOUND) {
            return;
        }
        const std::string path = toUtf8(mNotes->GetString(selected));
        for (std::size_t i = 0; i < mTabs.size(); ++i) {
            if (mTabs[i]->session.getPath() == path) {
                mNotebook->SetSelection(i);
                updateActive();
                return;
            }
        }
        auto tab = std::make_unique<NoteTab>();
        std::string error;
        if (!tab->session.openVault(toUtf8(mFolder), &error) ||
            !tab->session.selectNote(path, &error))
        {
            showError(error);
            updateActive();
            return;
        }
        tab->editor = new wxTextCtrl(
            mNotebook,
            wxID_ANY,
            fromUtf8(tab->session.getContent()),
            wxDefaultPosition,
            wxDefaultSize,
            wxTE_MULTILINE | wxTE_RICH2);
        NoteTab* current = tab.get();
        tab->editor->Bind(wxEVT_TEXT, [this, current](wxCommandEvent&) {
            current->session.setContent(toUtf8(current->editor->GetValue()));
            updateTabLabel(*current);
        });
        mTabs.push_back(std::move(tab));
        mNotebook->AddPage(current->editor, fromUtf8(path), true);
        updateActive();
    }

    inkstow::portable::Session mBrowser;
    std::vector<std::unique_ptr<NoteTab>> mTabs;
    wxListBox* mNotes = nullptr;
    wxNotebook* mNotebook = nullptr;
    wxString mFolder;
    bool mChangingPages = false;
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
