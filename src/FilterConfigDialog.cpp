#include "FilterConfig.h"
#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/clrpicker.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/splitter.h>
#include <wx/panel.h>
#include <wx/colordlg.h>
#include <cstdint>

static uint32_t wxToARGB(const wxColour& c)
{
    return ((uint32_t)c.Alpha() << 24) |
           ((uint32_t)c.Red()   << 16) |
           ((uint32_t)c.Green() <<  8) |
           ((uint32_t)c.Blue());
}
static wxColour ARGBToWx(uint32_t v)
{
    return wxColour((v>>16)&0xFF,(v>>8)&0xFF,v&0xFF,(v>>24)&0xFF);
}

class FilterConfigDialog : public wxDialog
{
public:
    FilterConfigDialog(wxWindow* parent, FilterConfig& cfg)
        : wxDialog(parent, wxID_ANY, "Filter Configuration",
                   wxDefaultPosition, wxSize(860, 560),
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
        , m_cfg(cfg)
    {
        auto* root = new wxBoxSizer(wxHORIZONTAL);

        // ── Left list ──
        auto* leftSz = new wxBoxSizer(wxVERTICAL);
        leftSz->Add(new wxStaticText(this, wxID_ANY, "Rules (first match wins):"),
                    0, wxBOTTOM, 4);
        m_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(220,-1),
                                wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES);
        {
            wxListItem c; c.SetId(0); c.SetText("Enabled"); c.SetWidth(60); m_list->InsertColumn(0,c);
            c.SetId(1); c.SetText("Name"); c.SetWidth(150); m_list->InsertColumn(1,c);
        }
        leftSz->Add(m_list, 1, wxEXPAND);

        auto* listBtns = new wxBoxSizer(wxHORIZONTAL);
        auto* btnNew   = new wxButton(this, wxID_ANY, "New",  wxDefaultPosition, wxSize(48,-1));
        auto* btnDel   = new wxButton(this, wxID_ANY, "Del",  wxDefaultPosition, wxSize(48,-1));
        auto* btnUp    = new wxButton(this, wxID_ANY, "Up",   wxDefaultPosition, wxSize(40,-1));
        auto* btnDown  = new wxButton(this, wxID_ANY, "Down", wxDefaultPosition, wxSize(50,-1));
        listBtns->Add(btnNew,0,wxRIGHT,2); listBtns->Add(btnDel,0,wxRIGHT,2);
        listBtns->Add(btnUp, 0,wxRIGHT,2); listBtns->Add(btnDown,0);
        leftSz->Add(listBtns, 0, wxTOP, 4);

        // File ops
        auto* fileBtns = new wxBoxSizer(wxHORIZONTAL);
        auto* btnLoad  = new wxButton(this, wxID_ANY, "Load JSON...");
        auto* btnSave2 = new wxButton(this, wxID_ANY, "Save JSON...");
        fileBtns->Add(btnLoad,0,wxRIGHT,4); fileBtns->Add(btnSave2,0);
        leftSz->Add(fileBtns, 0, wxTOP, 6);

        root->Add(leftSz, 0, wxEXPAND | wxALL, 8);

        // ── Right editor ──
        auto* rightSz = new wxBoxSizer(wxVERTICAL);
        auto* fg = new wxFlexGridSizer(2, 6, 8);
        fg->AddGrowableCol(1);

        auto addRow = [&](const wxString& lbl, wxTextCtrl*& ctrl, const wxString& hint="") {
            fg->Add(new wxStaticText(this,wxID_ANY,lbl), 0, wxALIGN_CENTER_VERTICAL);
            ctrl = new wxTextCtrl(this, wxID_ANY);
            if (!hint.empty()) ctrl->SetHint(hint);
            fg->Add(ctrl, 1, wxEXPAND);
        };

        addRow("Name:",           m_name,         "My rule");
        addRow("Payload Regex:",  m_payloadRegex, "error|panic|CRIT");
        addRow("Fixed Payload:",  m_fixedPayload, "exact substring");
        addRow("Application:",    m_application,  "sshd");
        addRow("Timestamp From:", m_tsFrom,       "2024-01-01T00:00:00");
        addRow("Timestamp To:",   m_tsTo,         "2024-12-31T23:59:59");

        // Enabled checkbox
        fg->Add(new wxStaticText(this,wxID_ANY,"Enabled:"), 0, wxALIGN_CENTER_VERTICAL);
        m_enabled = new wxCheckBox(this, wxID_ANY, "");
        m_enabled->SetValue(true);
        fg->Add(m_enabled, 0);

        // Colours
        fg->Add(new wxStaticText(this,wxID_ANY,"BG Colour:"), 0, wxALIGN_CENTER_VERTICAL);
        {
            auto* colRow = new wxBoxSizer(wxHORIZONTAL);
            m_bgPicker = new wxColourPickerCtrl(this, wxID_ANY, *wxWHITE);
            colRow->Add(m_bgPicker, 0, wxRIGHT, 12);
            colRow->Add(new wxStaticText(this,wxID_ANY,"FG:"), 0, wxALIGN_CENTER_VERTICAL|wxRIGHT,4);
            m_fgPicker = new wxColourPickerCtrl(this, wxID_ANY, *wxBLACK);
            colRow->Add(m_fgPicker, 0);
            fg->Add(colRow, 1, wxEXPAND);
        }

        rightSz->Add(fg, 0, wxEXPAND);

        auto* saveRule = new wxButton(this, wxID_ANY, "Save Rule");
        rightSz->Add(saveRule, 0, wxTOP, 10);

        rightSz->AddStretchSpacer();
        rightSz->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND|wxTOP, 8);

        root->Add(rightSz, 1, wxEXPAND | wxALL, 8);
        SetSizer(root);

        // ── Bindings ──
        m_list->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { OnListSel(); });

        btnNew->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            FilterRule r; r.name = "New Rule"; m_cfg.AddRule(r);
            RefreshList(); m_list->SetItemState((long)m_cfg.Rules().size()-1,
                                                wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
            FillEditor(m_cfg.Rules().back());
        });
        btnDel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            long s = GetSel(); if (s<0) return;
            m_cfg.RemoveRule((std::size_t)s); RefreshList();
        });
        btnUp->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            long s = GetSel(); if (s<0) return;
            m_cfg.MoveUp((std::size_t)s); RefreshList();
            long ns = s>0?s-1:0;
            m_list->SetItemState(ns, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
        });
        btnDown->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            long s = GetSel(); if (s<0) return;
            m_cfg.MoveDown((std::size_t)s); RefreshList();
            long ns = s+1<(long)m_cfg.Rules().size()?s+1:s;
            m_list->SetItemState(ns, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
        });
        saveRule->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnSaveRule(); });

        btnLoad->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            wxFileDialog dlg(this,"Load Filter JSON","","",
                             "JSON files (*.json)|*.json|All (*)|*", wxFD_OPEN|wxFD_FILE_MUST_EXIST);
            if (dlg.ShowModal()!=wxID_OK) return;
            if (!m_cfg.LoadFromFile(dlg.GetPath().ToStdString()))
                wxMessageBox("Failed to load filter file","Error",wxOK|wxICON_ERROR,this);
            RefreshList();
        });
        btnSave2->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            wxFileDialog dlg(this,"Save Filter JSON","","filters.json",
                             "JSON files (*.json)|*.json|All (*)|*",
                             wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
            if (dlg.ShowModal()!=wxID_OK) return;
            if (!m_cfg.SaveToFile(dlg.GetPath().ToStdString()))
                wxMessageBox("Failed to save filter file","Error",wxOK|wxICON_ERROR,this);
        });

        RefreshList();
    }

private:
    long GetSel() const
    {
        long idx = m_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        return idx;
    }

    void RefreshList()
    {
        m_list->DeleteAllItems();
        const auto& rules = m_cfg.Rules();
        for (std::size_t i = 0; i < rules.size(); ++i)
        {
            long row = m_list->InsertItem((long)i, rules[i].enabled ? "Y" : "N");
            m_list->SetItem(row, 1, wxString::FromUTF8(rules[i].name));
            // Colour the list item
            wxColour bg((rules[i].bgColour>>16)&0xFF,
                        (rules[i].bgColour>>8)&0xFF,
                         rules[i].bgColour&0xFF);
            wxColour fg((rules[i].fgColour>>16)&0xFF,
                        (rules[i].fgColour>>8)&0xFF,
                         rules[i].fgColour&0xFF);
            m_list->SetItemBackgroundColour(row, bg);
            m_list->SetItemTextColour(row, fg);
        }
    }

    void FillEditor(const FilterRule& r)
    {
        m_name->SetValue(wxString::FromUTF8(r.name));
        m_payloadRegex->SetValue(wxString::FromUTF8(r.payloadRegex));
        m_fixedPayload->SetValue(wxString::FromUTF8(r.fixedPayload));
        m_application->SetValue(wxString::FromUTF8(r.application));
        m_tsFrom->SetValue(wxString::FromUTF8(r.tsFrom));
        m_tsTo->SetValue(wxString::FromUTF8(r.tsTo));
        m_enabled->SetValue(r.enabled);
        m_bgPicker->SetColour(ARGBToWx(r.bgColour));
        m_fgPicker->SetColour(ARGBToWx(r.fgColour));
    }

    void OnListSel()
    {
        long s = GetSel();
        if (s >= 0 && (std::size_t)s < m_cfg.Rules().size())
            FillEditor(m_cfg.Rules()[(std::size_t)s]);
    }

    void OnSaveRule()
    {
        long s = GetSel();
        if (s < 0 || (std::size_t)s >= m_cfg.Rules().size()) return;
        auto& r = m_cfg.Rules()[(std::size_t)s];
        r.name         = m_name->GetValue().ToStdString();
        r.payloadRegex = m_payloadRegex->GetValue().ToStdString();
        r.fixedPayload = m_fixedPayload->GetValue().ToStdString();
        r.application  = m_application->GetValue().ToStdString();
        r.tsFrom       = m_tsFrom->GetValue().ToStdString();
        r.tsTo         = m_tsTo->GetValue().ToStdString();
        r.enabled      = m_enabled->GetValue();
        r.bgColour     = wxToARGB(m_bgPicker->GetColour());
        r.fgColour     = wxToARGB(m_fgPicker->GetColour());
        RefreshList();
        m_list->SetItemState(s, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    }

    FilterConfig& m_cfg;
    wxListCtrl*   m_list{};
    wxTextCtrl*   m_name{};
    wxTextCtrl*   m_payloadRegex{};
    wxTextCtrl*   m_fixedPayload{};
    wxTextCtrl*   m_application{};
    wxTextCtrl*   m_tsFrom{};
    wxTextCtrl*   m_tsTo{};
    wxCheckBox*   m_enabled{};
    wxColourPickerCtrl* m_bgPicker{};
    wxColourPickerCtrl* m_fgPicker{};
};

wxDialog* CreateFilterConfigDialog(wxWindow* parent, FilterConfig& cfg)
{
    return new FilterConfigDialog(parent, cfg);
}
