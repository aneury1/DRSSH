#include <wx/statline.h>
// ProfileDialog - edit/manage connection profiles
#include "ConnectionProfile.h"
#include <wx/dialog.h>
#include <wx/listbox.h>
#include <wx/textctrl.h>
#include <wx/spinctrl.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/msgdlg.h>

class ProfileDialog : public wxDialog
{
public:
    ProfileDialog(wxWindow* parent, ProfileManager& mgr)
        : wxDialog(parent, wxID_ANY, "Manage Connection Profiles",
                   wxDefaultPosition, wxSize(700, 480),
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
        , m_mgr(mgr)
    {
        auto* root = new wxBoxSizer(wxHORIZONTAL);

        // --- Left: list ---
        auto* leftSz = new wxBoxSizer(wxVERTICAL);
        m_list = new wxListBox(this, wxID_ANY);
        leftSz->Add(new wxStaticText(this, wxID_ANY, "Profiles:"), 0, wxBOTTOM, 4);
        leftSz->Add(m_list, 1, wxEXPAND);

        auto* listBtns = new wxBoxSizer(wxHORIZONTAL);
        m_btnNew    = new wxButton(this, wxID_ANY, "New");
        m_btnDelete = new wxButton(this, wxID_ANY, "Delete");
        listBtns->Add(m_btnNew,    0, wxRIGHT, 6);
        listBtns->Add(m_btnDelete, 0);
        leftSz->Add(listBtns, 0, wxTOP, 6);

        root->Add(leftSz, 0, wxEXPAND | wxALL, 10);
        root->Add(new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_VERTICAL),
                  0, wxEXPAND | wxTOP | wxBOTTOM, 10);

        // --- Right: fields ---
        auto* fg = new wxFlexGridSizer(2, 8, 8);
        fg->AddGrowableCol(1);

        auto addRow = [&](const wxString& label, wxTextCtrl*& ctrl,
                          long style = 0)
        {
            fg->Add(new wxStaticText(this, wxID_ANY, label),
                    0, wxALIGN_CENTER_VERTICAL);
            ctrl = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition,
                                  wxDefaultSize, style);
            fg->Add(ctrl, 1, wxEXPAND);
        };

        addRow("Name:",         m_name);
        addRow("Host:",         m_host);

        // port row
        fg->Add(new wxStaticText(this, wxID_ANY, "Port:"), 0, wxALIGN_CENTER_VERTICAL);
        m_port = new wxSpinCtrl(this, wxID_ANY, "22", wxDefaultPosition,
                                wxDefaultSize, wxSP_ARROW_KEYS, 1, 65535, 22);
        fg->Add(m_port, 1, wxEXPAND);

        addRow("User:",         m_user);
        addRow("Password:",     m_password, wxTE_PASSWORD);
        addRow("Filter regex:", m_filter);
        addRow("journalctl args:", m_journalArgs);

        auto* rightSz = new wxBoxSizer(wxVERTICAL);
        rightSz->Add(fg, 0, wxEXPAND);

        auto* saveBtns = new wxBoxSizer(wxHORIZONTAL);
        m_btnSave = new wxButton(this, wxID_ANY, "Save Profile");
        saveBtns->Add(m_btnSave, 0);
        rightSz->Add(saveBtns, 0, wxTOP, 12);

        rightSz->AddStretchSpacer();
        auto* dlgBtns = CreateButtonSizer(wxOK | wxCANCEL);
        rightSz->Add(dlgBtns, 0, wxEXPAND | wxTOP, 8);

        root->Add(rightSz, 1, wxEXPAND | wxALL, 10);
        SetSizer(root);

        // Bindings
        m_list->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { OnListSelect(); });
        m_btnNew->Bind(wxEVT_BUTTON,    [this](wxCommandEvent&) { OnNew(); });
        m_btnDelete->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnDelete(); });
        m_btnSave->Bind(wxEVT_BUTTON,   [this](wxCommandEvent&) { OnSave(); });

        RefreshList();
    }

private:
    void RefreshList()
    {
        m_list->Clear();
        for (const auto& p : m_mgr.Profiles())
            m_list->Append(wxString::FromUTF8(p.name.empty() ? p.host : p.name));
    }

    void FillFields(const ConnectionProfile& p)
    {
        m_name->SetValue(wxString::FromUTF8(p.name));
        m_host->SetValue(wxString::FromUTF8(p.host));
        m_port->SetValue(p.port);
        m_user->SetValue(wxString::FromUTF8(p.user));
        m_password->SetValue(wxString::FromUTF8(p.password));
        m_filter->SetValue(wxString::FromUTF8(p.filter));
        m_journalArgs->SetValue(wxString::FromUTF8(p.journalArgs));
    }

    ConnectionProfile ReadFields() const
    {
        ConnectionProfile p;
        p.name        = m_name->GetValue().ToStdString();
        p.host        = m_host->GetValue().ToStdString();
        p.port        = m_port->GetValue();
        p.user        = m_user->GetValue().ToStdString();
        p.password    = m_password->GetValue().ToStdString();
        p.filter      = m_filter->GetValue().ToStdString();
        p.journalArgs = m_journalArgs->GetValue().ToStdString();
        return p;
    }

    void OnListSelect()
    {
        int sel = m_list->GetSelection();
        if (sel == wxNOT_FOUND) return;
        auto& profs = m_mgr.Profiles();
        if ((std::size_t)sel < profs.size())
            FillFields(profs[(std::size_t)sel]);
    }

    void OnNew()
    {
        ConnectionProfile p;
        p.name = "New Profile";
        p.port = 22;
        m_mgr.Add(p);
        RefreshList();
        m_list->SetSelection((int)m_mgr.Profiles().size() - 1);
        FillFields(p);
    }

    void OnDelete()
    {
        int sel = m_list->GetSelection();
        if (sel == wxNOT_FOUND) return;
        m_mgr.Remove((std::size_t)sel);
        RefreshList();
    }

    void OnSave()
    {
        int sel = m_list->GetSelection();
        if (sel == wxNOT_FOUND)
        {
            m_mgr.Add(ReadFields());
        }
        else
        {
            m_mgr.Update((std::size_t)sel, ReadFields());
        }
        m_mgr.Save();
        RefreshList();
    }

    ProfileManager& m_mgr;

    wxListBox*  m_list{};
    wxButton*   m_btnNew{};
    wxButton*   m_btnDelete{};
    wxButton*   m_btnSave{};

    wxTextCtrl* m_name{};
    wxTextCtrl* m_host{};
    wxSpinCtrl* m_port{};
    wxTextCtrl* m_user{};
    wxTextCtrl* m_password{};
    wxTextCtrl* m_filter{};
    wxTextCtrl* m_journalArgs{};
};

// Factory function called from MainFrame
wxDialog* CreateProfileDialog(wxWindow* parent, ProfileManager& mgr)
{
    return new ProfileDialog(parent, mgr);
}
