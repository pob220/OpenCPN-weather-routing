/***************************************************************************
 *   Copyright (C) 2010 by David S. Register                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <https://www.gnu.org/licenses/>. *
 **************************************************************************/

/**
 * \file
 *
 * Implement toolbar.h -- OpenCPN Toolbar
 */

#include <vector>

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/listbox.h>
#include <wx/scrolwin.h>
#include <wx/spinctrl.h>

#include "config.h"
#include "toolbar.h"

#include "model/ais_state_vars.h"
#include "model/boat_profile_service.h"
#include "model/config_vars.h"
#include "model/gui_vars.h"
#include "model/idents.h"
#include "model/ocpn_types.h"
#include "model/svg_utils.h"

#include "chartdb.h"
#include "chcanv.h"
#include "compass.h"
#include "font_mgr.h"
#include "gui_lib.h"
#include "navutil.h"
#include "ocpn_platform.h"
#include "pluginmanager.h"
#include "s52plib.h"
#include "styles.h"
#include "top_frame.h"
#include "user_colors.h"

#ifdef __ANDROID__
#include "androidUTIL.h"
#endif

#ifdef ocpnUSE_GL
#include "gl_chart_canvas.h"
#endif

#ifdef ocpnUSE_GL
extern GLenum g_texture_rectangle_format;
#endif

ocpnFloatingToolbarDialog *g_MainToolbar;

namespace {
wxString EllipsizeProfileLabel(wxWindow* win, const wxString& label,
                               int width) {
  if (!win || width <= 0 || label.empty()) return label;

  wxClientDC dc(win);
  dc.SetFont(win->GetFont());
  int text_width = 0;
  int text_height = 0;
  dc.GetTextExtent(label, &text_width, &text_height);
  if (text_width <= width) return label;

  wxString text = label;
  const wxString ellipsis = "...";
  while (text.length() > 1) {
    text.RemoveLast();
    dc.GetTextExtent(text + ellipsis, &text_width, &text_height);
    if (text_width <= width) return text + ellipsis;
  }
  return ellipsis;
}

wxString BoatProfileValidationMessage(const BoatProfileValidation& validation) {
  wxString message;
  for (const auto& error : validation.errors) {
    if (!message.empty()) message += "\n";
    message += error;
  }
  return message;
}

wxSpinCtrlDouble* AddProfileDoubleField(wxWindow* parent,
                                        wxFlexGridSizer* grid,
                                        const wxString& label, double min,
                                        double max, double increment,
                                        int digits,
                                        const wxString& unit_label) {
  grid->Add(new wxStaticText(parent, wxID_ANY, label), 0,
            wxALIGN_CENTER_VERTICAL | wxALL, 5);
  auto* ctrl = new wxSpinCtrlDouble(parent, wxID_ANY, wxEmptyString,
                                    wxDefaultPosition, wxDefaultSize,
                                    wxSP_ARROW_KEYS, min, max, 0.0,
                                    increment);
  ctrl->SetDigits(digits);
  grid->Add(ctrl, 0, wxEXPAND | wxALL, 5);
  grid->Add(new wxStaticText(parent, wxID_ANY, unit_label), 0,
            wxALIGN_CENTER_VERTICAL | wxALL, 5);
  return ctrl;
}

class BoatProfileManagerDialog : public wxDialog {
public:
  BoatProfileManagerDialog(wxWindow* parent, bool create_new)
      : wxDialog(parent, wxID_ANY, _("Manage Boat Profiles"),
                 wxDefaultPosition, wxSize(760, 620),
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
    auto& service = BoatProfileService::Get();
    service.EnsureActiveProfile(_("My Boat"));
    m_profiles = service.GetProfiles();
    m_activeProfileId = service.GetActiveProfileId();

    if (create_new) AddNewProfile(false);
    CreateControls();
    RefreshProfileList();
    int selection = create_new ? static_cast<int>(m_profiles.size()) - 1
                               : FindProfileIndex(m_activeProfileId);
    if (selection == wxNOT_FOUND && !m_profiles.empty()) selection = 0;
    SelectProfile(selection);
  }

private:
  void CreateControls() {
    auto* topSizer = new wxBoxSizer(wxVERTICAL);
    auto* mainSizer = new wxBoxSizer(wxHORIZONTAL);
    SetSizer(topSizer);

    auto* listSizer = new wxBoxSizer(wxVERTICAL);
    m_profileList = new wxListBox(this, wxID_ANY);
    m_profileList->Bind(wxEVT_LISTBOX, &BoatProfileManagerDialog::OnSelect,
                        this);
    listSizer->Add(m_profileList, 1, wxEXPAND | wxALL, 5);

    auto* newButton = new wxButton(this, wxID_ANY, _("New"));
    auto* duplicateButton = new wxButton(this, wxID_ANY, _("Duplicate"));
    auto* deleteButton = new wxButton(this, wxID_ANY, _("Delete"));
    auto* activeButton = new wxButton(this, wxID_ANY, _("Set Active"));
    newButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
      if (SaveEditorToCurrent(true)) AddNewProfile();
    });
    duplicateButton->Bind(wxEVT_BUTTON,
                          &BoatProfileManagerDialog::OnDuplicate, this);
    deleteButton->Bind(wxEVT_BUTTON, &BoatProfileManagerDialog::OnDelete, this);
    activeButton->Bind(wxEVT_BUTTON, &BoatProfileManagerDialog::OnSetActive,
                       this);
    listSizer->Add(newButton, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
    listSizer->Add(duplicateButton, 0,
                   wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
    listSizer->Add(deleteButton, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
    listSizer->Add(activeButton, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
    mainSizer->Add(listSizer, 0, wxEXPAND | wxALL, 8);

    m_editor = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition,
                                    wxDefaultSize, wxVSCROLL | wxTAB_TRAVERSAL);
    m_editor->SetScrollRate(5, 5);
    auto* editorSizer = new wxBoxSizer(wxVERTICAL);
    m_editor->SetSizer(editorSizer);

    auto* identityBox =
        new wxStaticBoxSizer(wxVERTICAL, m_editor, _("Profile"));
    auto* identityParent = identityBox->GetStaticBox();
    auto* identityGrid = new wxFlexGridSizer(0, 2, 0, 0);
    identityGrid->AddGrowableCol(1, 1);
    identityGrid->Add(new wxStaticText(identityParent, wxID_ANY, _("Name")), 0,
                      wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_name = new wxTextCtrl(identityParent, wxID_ANY);
    identityGrid->Add(m_name, 0, wxEXPAND | wxALL, 5);
    identityGrid->Add(
        new wxStaticText(identityParent, wxID_ANY, _("Vessel type")), 0,
        wxALIGN_CENTER_VERTICAL | wxALL, 5);
    wxArrayString vesselTypes;
    vesselTypes.Add(_("Cruising sailboat"));
    vesselTypes.Add(_("Racing sailboat"));
    vesselTypes.Add(_("Catamaran"));
    vesselTypes.Add(_("Motor vessel"));
    vesselTypes.Add(_("Other"));
    m_vesselType = new wxChoice(identityParent, wxID_ANY, wxDefaultPosition,
                                wxDefaultSize, vesselTypes);
    identityGrid->Add(m_vesselType, 0, wxEXPAND | wxALL, 5);
    identityBox->Add(identityGrid, 0, wxEXPAND | wxALL, 5);
    editorSizer->Add(identityBox, 0, wxEXPAND | wxALL, 5);

    auto* dimensionsBox =
        new wxStaticBoxSizer(wxVERTICAL, m_editor, _("Dimensions"));
    auto* dimensionsParent = dimensionsBox->GetStaticBox();
    auto* dimensionsGrid = new wxFlexGridSizer(0, 3, 0, 0);
    dimensionsGrid->AddGrowableCol(1, 1);
    m_length = AddProfileDoubleField(dimensionsParent, dimensionsGrid,
                                     _("Length"), 0.1, 500.0, 0.1, 2, _("m"));
    m_beam = AddProfileDoubleField(dimensionsParent, dimensionsGrid, _("Beam"),
                                   0.1, 100.0, 0.1, 2, _("m"));
    m_draft = AddProfileDoubleField(dimensionsParent, dimensionsGrid,
                                    _("Draft"), 0.1, 100.0, 0.1, 2, _("m"));
    m_airDraft = AddProfileDoubleField(dimensionsParent, dimensionsGrid,
                                       _("Air draft"), 0.1, 200.0, 0.1, 2,
                                       _("m"));
    m_displacement = AddProfileDoubleField(
        dimensionsParent, dimensionsGrid, _("Displacement"), 0.0, 1000.0, 0.1,
        2, _("t"));
    m_sailArea = AddProfileDoubleField(dimensionsParent, dimensionsGrid,
                                       _("Sail area"), 0.0, 5000.0, 1.0, 1,
                                       _("m2"));
    dimensionsBox->Add(dimensionsGrid, 0, wxEXPAND | wxALL, 5);
    editorSizer->Add(dimensionsBox, 0, wxEXPAND | wxALL, 5);

    auto* routingBox =
        new wxStaticBoxSizer(wxVERTICAL, m_editor, _("Weather routing"));
    auto* routingParent = routingBox->GetStaticBox();
    auto* routingGrid = new wxFlexGridSizer(0, 3, 0, 0);
    routingGrid->AddGrowableCol(1, 1);
    m_cruisingSpeed = AddProfileDoubleField(
        routingParent, routingGrid, _("Cruising speed"), 0.1, 200.0, 0.1, 2,
        _("kn"));
    m_maxSpeed = AddProfileDoubleField(routingParent, routingGrid,
                                       _("Maximum speed"), 0.0, 300.0, 0.1, 2,
                                       _("kn"));
    m_motoringSpeed = AddProfileDoubleField(
        routingParent, routingGrid, _("Motoring speed"), 0.0, 200.0, 0.1, 2,
        _("kn"));
    m_engineConsumption = AddProfileDoubleField(
        routingParent, routingGrid, _("Engine consumption"), 0.0, 200.0, 0.1,
        2, _("l/h"));
    m_upwindAngle = AddProfileDoubleField(
        routingParent, routingGrid, _("Best upwind angle"), 0.0, 180.0, 1.0,
        0, _("deg"));
    m_downwindAngle = AddProfileDoubleField(
        routingParent, routingGrid, _("Best downwind angle"), 0.0, 180.0, 1.0,
        0, _("deg"));
    m_minWind = AddProfileDoubleField(routingParent, routingGrid,
                                      _("Minimum routing wind"), 0.0, 200.0,
                                      0.5, 1, _("kn"));
    m_maxWind = AddProfileDoubleField(routingParent, routingGrid,
                                      _("Maximum routing wind"), 0.0, 200.0,
                                      0.5, 1, _("kn"));
    routingGrid->Add(new wxStaticText(routingParent, wxID_ANY, _("Polar file")),
                     0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_polarFile = new wxTextCtrl(routingParent, wxID_ANY);
    routingGrid->Add(m_polarFile, 0, wxEXPAND | wxALL, 5);
    routingGrid->AddSpacer(1);
    routingBox->Add(routingGrid, 0, wxEXPAND | wxALL, 5);
    editorSizer->Add(routingBox, 0, wxEXPAND | wxALL, 5);

    auto* currentsBox =
        new wxStaticBoxSizer(wxVERTICAL, m_editor, _("Currents and data"));
    auto* currentsParent = currentsBox->GetStaticBox();
    auto* currentsGrid = new wxFlexGridSizer(0, 3, 0, 0);
    currentsGrid->AddGrowableCol(1, 1);
    m_currentGrid = AddProfileDoubleField(
        currentsParent, currentsGrid, _("Current grid spacing"), 0.001, 5.0,
        0.01, 3, _("deg"));
    currentsGrid->Add(new wxStaticText(currentsParent, wxID_ANY,
                                       _("Current forecast duration")),
                      0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_currentDuration = new wxSpinCtrl(currentsParent, wxID_ANY, wxEmptyString,
                                       wxDefaultPosition, wxDefaultSize,
                                       wxSP_ARROW_KEYS, 1, 720, 24);
    currentsGrid->Add(m_currentDuration, 0, wxEXPAND | wxALL, 5);
    currentsGrid->Add(new wxStaticText(currentsParent, wxID_ANY, _("hours")),
                      0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    currentsGrid->Add(new wxStaticText(currentsParent, wxID_ANY,
                                       _("Current forecast step")),
                      0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_currentStep = new wxSpinCtrl(currentsParent, wxID_ANY, wxEmptyString,
                                   wxDefaultPosition, wxDefaultSize,
                                   wxSP_ARROW_KEYS, 1, 72, 1);
    currentsGrid->Add(m_currentStep, 0, wxEXPAND | wxALL, 5);
    currentsGrid->Add(new wxStaticText(currentsParent, wxID_ANY, _("hours")),
                      0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    currentsGrid->Add(
        new wxStaticText(currentsParent, wxID_ANY, _("Current provider")), 0,
        wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_currentProvider = new wxTextCtrl(currentsParent, wxID_ANY);
    currentsGrid->Add(m_currentProvider, 0, wxEXPAND | wxALL, 5);
    currentsGrid->AddSpacer(1);
    currentsGrid->Add(
        new wxStaticText(currentsParent, wxID_ANY, _("Data directory")), 0,
        wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_dataDirectory = new wxTextCtrl(currentsParent, wxID_ANY);
    currentsGrid->Add(m_dataDirectory, 0, wxEXPAND | wxALL, 5);
    currentsGrid->AddSpacer(1);
    currentsBox->Add(currentsGrid, 0, wxEXPAND | wxALL, 5);
    editorSizer->Add(currentsBox, 0, wxEXPAND | wxALL, 5);

    auto* notesBox = new wxStaticBoxSizer(wxVERTICAL, m_editor, _("Notes"));
    m_notes = new wxTextCtrl(notesBox->GetStaticBox(), wxID_ANY, wxEmptyString,
                             wxDefaultPosition, wxSize(-1, 90),
                             wxTE_MULTILINE);
    notesBox->Add(m_notes, 1, wxEXPAND | wxALL, 5);
    editorSizer->Add(notesBox, 0, wxEXPAND | wxALL, 5);

    mainSizer->Add(m_editor, 1, wxEXPAND | wxALL, 8);
    topSizer->Add(mainSizer, 1, wxEXPAND);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* saveButton = new wxButton(this, wxID_SAVE, _("Save"));
    auto* closeButton = new wxButton(this, wxID_CANCEL, _("Close"));
    saveButton->Bind(wxEVT_BUTTON, &BoatProfileManagerDialog::OnSave, this);
    buttons->AddStretchSpacer(1);
    buttons->Add(saveButton, 0, wxALL, 5);
    buttons->Add(closeButton, 0, wxALL, 5);
    topSizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
  }

  int FindProfileIndex(const wxString& id) const {
    for (size_t i = 0; i < m_profiles.size(); ++i) {
      if (m_profiles[i].id == id) return static_cast<int>(i);
    }
    return wxNOT_FOUND;
  }

  void RefreshProfileList() {
    if (!m_profileList) return;
    m_profileList->Clear();
    for (const auto& profile : m_profiles) {
      wxString label = profile.name.empty() ? _("Unnamed Boat") : profile.name;
      if (profile.id == m_activeProfileId) label += _(" (active)");
      m_profileList->Append(label);
    }
  }

  bool SaveEditorToCurrent(bool show_errors) {
    if (m_selectedIndex == wxNOT_FOUND ||
        m_selectedIndex >= static_cast<int>(m_profiles.size()))
      return true;
    BoatProfile profile = ReadEditor(m_profiles[m_selectedIndex]);
    auto validation = BoatProfileStore::Validate(profile);
    if (!validation.ok) {
      if (show_errors)
        wxMessageBox(BoatProfileValidationMessage(validation),
                     _("Boat Profile"), wxOK | wxICON_WARNING, this);
      return false;
    }
    m_profiles[m_selectedIndex] = profile;
    return true;
  }

  BoatProfile ReadEditor(BoatProfile profile) const {
    wxString name = m_name->GetValue();
    profile.name = name.Trim(false).Trim();
    if (profile.id.empty()) profile.id = BoatProfileStore::NewProfileId();
    profile.vessel_type = m_vesselType->GetStringSelection();
    if (profile.vessel_type.empty()) profile.vessel_type = "Other";
    profile.length_m = m_length->GetValue();
    profile.beam_m = m_beam->GetValue();
    profile.draft_m = m_draft->GetValue();
    profile.air_draft_m = m_airDraft->GetValue();
    profile.displacement_t = m_displacement->GetValue();
    profile.sail_area_m2 = m_sailArea->GetValue();
    profile.cruising_speed_kn = m_cruisingSpeed->GetValue();
    profile.max_speed_kn = m_maxSpeed->GetValue();
    profile.motoring_speed_kn = m_motoringSpeed->GetValue();
    profile.engine_consumption_lph = m_engineConsumption->GetValue();
    profile.polar_file = m_polarFile->GetValue();
    profile.upwind_twa_deg = m_upwindAngle->GetValue();
    profile.downwind_twa_deg = m_downwindAngle->GetValue();
    profile.min_routing_wind_kn = m_minWind->GetValue();
    profile.max_routing_wind_kn = m_maxWind->GetValue();
    profile.current_grid_spacing_deg = m_currentGrid->GetValue();
    profile.current_duration_hours = m_currentDuration->GetValue();
    profile.current_step_hours = m_currentStep->GetValue();
    profile.current_provider = m_currentProvider->GetValue();
    profile.data_directory = m_dataDirectory->GetValue();
    profile.notes = m_notes->GetValue();
    return profile;
  }

  void LoadEditor(const BoatProfile& profile) {
    m_name->SetValue(profile.name);
    int vesselIndex = m_vesselType->FindString(profile.vessel_type);
    if (vesselIndex == wxNOT_FOUND) vesselIndex = m_vesselType->FindString("Other");
    m_vesselType->SetSelection(vesselIndex);
    m_length->SetValue(profile.length_m);
    m_beam->SetValue(profile.beam_m);
    m_draft->SetValue(profile.draft_m);
    m_airDraft->SetValue(profile.air_draft_m);
    m_displacement->SetValue(profile.displacement_t);
    m_sailArea->SetValue(profile.sail_area_m2);
    m_cruisingSpeed->SetValue(profile.cruising_speed_kn);
    m_maxSpeed->SetValue(profile.max_speed_kn);
    m_motoringSpeed->SetValue(profile.motoring_speed_kn);
    m_engineConsumption->SetValue(profile.engine_consumption_lph);
    m_polarFile->SetValue(profile.polar_file);
    m_upwindAngle->SetValue(profile.upwind_twa_deg);
    m_downwindAngle->SetValue(profile.downwind_twa_deg);
    m_minWind->SetValue(profile.min_routing_wind_kn);
    m_maxWind->SetValue(profile.max_routing_wind_kn);
    m_currentGrid->SetValue(profile.current_grid_spacing_deg);
    m_currentDuration->SetValue(profile.current_duration_hours);
    m_currentStep->SetValue(profile.current_step_hours);
    m_currentProvider->SetValue(profile.current_provider);
    m_dataDirectory->SetValue(profile.data_directory);
    m_notes->SetValue(profile.notes);
  }

  void SelectProfile(int index) {
    if (index == wxNOT_FOUND || index >= static_cast<int>(m_profiles.size()))
      return;
    m_selectedIndex = index;
    m_profileList->SetSelection(index);
    LoadEditor(m_profiles[index]);
  }

  void AddNewProfile(bool refresh = true) {
    BoatProfile profile = BoatProfileStore::CreateFromCurrentSettings(
        wxString::Format(_("Boat %u"),
                         static_cast<unsigned>(m_profiles.size()) + 1));
    profile.id = BoatProfileStore::NewProfileId();
    m_profiles.push_back(profile);
    if (m_activeProfileId.empty()) m_activeProfileId = profile.id;
    if (refresh) {
      RefreshProfileList();
      SelectProfile(static_cast<int>(m_profiles.size()) - 1);
    }
  }

  void OnSelect(wxCommandEvent& event) {
    if (!SaveEditorToCurrent(true)) {
      m_profileList->SetSelection(m_selectedIndex);
      return;
    }
    RefreshProfileList();
    SelectProfile(event.GetSelection());
  }

  void OnDuplicate(wxCommandEvent&) {
    if (!SaveEditorToCurrent(true) || m_selectedIndex == wxNOT_FOUND) return;
    BoatProfile profile = m_profiles[m_selectedIndex];
    profile.id = BoatProfileStore::NewProfileId();
    profile.name += _(" Copy");
    m_profiles.push_back(profile);
    RefreshProfileList();
    SelectProfile(static_cast<int>(m_profiles.size()) - 1);
  }

  void OnDelete(wxCommandEvent&) {
    if (m_selectedIndex == wxNOT_FOUND || m_profiles.size() <= 1) {
      wxMessageBox(_("At least one boat profile is required."),
                   _("Boat Profile"), wxOK | wxICON_WARNING, this);
      return;
    }
    wxString id = m_profiles[m_selectedIndex].id;
    m_profiles.erase(m_profiles.begin() + m_selectedIndex);
    if (m_activeProfileId == id) m_activeProfileId = m_profiles.front().id;
    RefreshProfileList();
    SelectProfile(wxMin(m_selectedIndex, static_cast<int>(m_profiles.size()) - 1));
  }

  void OnSetActive(wxCommandEvent&) {
    if (!SaveEditorToCurrent(true) || m_selectedIndex == wxNOT_FOUND) return;
    m_activeProfileId = m_profiles[m_selectedIndex].id;
    RefreshProfileList();
    SelectProfile(m_selectedIndex);
  }

  void OnSave(wxCommandEvent&) {
    if (!SaveEditorToCurrent(true)) return;
    wxString error;
    if (!BoatProfileService::Get().SetProfiles(m_profiles, m_activeProfileId,
                                               &error)) {
      wxMessageBox(error, _("Boat Profile"), wxOK | wxICON_WARNING, this);
      return;
    }
    EndModal(wxID_OK);
  }

  std::vector<BoatProfile> m_profiles;
  wxString m_activeProfileId;
  int m_selectedIndex = wxNOT_FOUND;
  wxListBox* m_profileList = nullptr;
  wxScrolledWindow* m_editor = nullptr;
  wxTextCtrl* m_name = nullptr;
  wxChoice* m_vesselType = nullptr;
  wxSpinCtrlDouble* m_length = nullptr;
  wxSpinCtrlDouble* m_beam = nullptr;
  wxSpinCtrlDouble* m_draft = nullptr;
  wxSpinCtrlDouble* m_airDraft = nullptr;
  wxSpinCtrlDouble* m_displacement = nullptr;
  wxSpinCtrlDouble* m_sailArea = nullptr;
  wxSpinCtrlDouble* m_cruisingSpeed = nullptr;
  wxSpinCtrlDouble* m_maxSpeed = nullptr;
  wxSpinCtrlDouble* m_motoringSpeed = nullptr;
  wxSpinCtrlDouble* m_engineConsumption = nullptr;
  wxTextCtrl* m_polarFile = nullptr;
  wxSpinCtrlDouble* m_upwindAngle = nullptr;
  wxSpinCtrlDouble* m_downwindAngle = nullptr;
  wxSpinCtrlDouble* m_minWind = nullptr;
  wxSpinCtrlDouble* m_maxWind = nullptr;
  wxSpinCtrlDouble* m_currentGrid = nullptr;
  wxSpinCtrl* m_currentDuration = nullptr;
  wxSpinCtrl* m_currentStep = nullptr;
  wxTextCtrl* m_currentProvider = nullptr;
  wxTextCtrl* m_dataDirectory = nullptr;
  wxTextCtrl* m_notes = nullptr;
};
}

class ocpnToolBarTool : public wxToolBarToolBase {
public:
  ocpnToolBarTool(ocpnToolBarSimple *tbar, int id, const wxString &label,
                  const wxBitmap &bmpNormal, const wxBitmap &bmpRollover,
                  wxItemKind kind, wxObject *clientData,
                  const wxString &shortHelp, const wxString &longHelp)
      : wxToolBarToolBase((wxToolBarBase *)tbar, id, label, bmpNormal,
                          bmpRollover, kind, clientData, shortHelp, longHelp) {
    m_enabled = true;
    m_toggled = false;
    rollover = false;
    bitmapOK = false;
    m_btooltip_hiviz = false;

    toolname = g_pi_manager->GetToolOwnerCommonName(id);
    if (toolname == "") {
      isPluginTool = false;
      toolname = label;
      iconName = label;

    } else {
      isPluginTool = true;
      pluginNormalIcon = bmpNormal;
      pluginRolloverIcon = bmpRollover;
    }
  }

  ocpnToolBarTool(ocpnToolBarSimple *tbar, int id, const wxBitmap &bmpNormal,
                  const wxBitmap &bmpRollover, wxItemKind kind,
                  wxObject *clientData, const wxString &shortHelp,
                  const wxString &longHelp)
      : wxToolBarToolBase((wxToolBarBase *)tbar, id, "", bmpNormal, bmpRollover,
                          kind, clientData, shortHelp, longHelp) {
    m_enabled = true;
    m_toggled = false;
    rollover = false;
    m_btooltip_hiviz = false;
    isPluginTool = false;

    m_bmpNormal = bmpNormal;
    bitmapOK = true;
  }

  void SetSize(const wxSize &size) {
    m_width = size.x;
    m_height = size.y;
  }

  wxCoord GetWidth() const { return m_width; }

  wxCoord GetHeight() const { return m_height; }

  wxString GetToolname() { return toolname; }

  void SetIconName(wxString name) { iconName = name; }
  wxString GetIconName() { return iconName; }

  void SetTooltipHiviz(bool enable) { m_btooltip_hiviz = enable; }

  wxCoord m_x;
  wxCoord m_y;
  wxCoord m_width;
  wxCoord m_height;
  wxRect trect;
  wxString toolname;
  wxString iconName;
  wxBitmap pluginNormalIcon;
  wxBitmap pluginRolloverIcon;
  const wxBitmap *pluginToggledIcon;
  bool firstInLine;
  bool lastInLine;
  bool rollover;
  bool bitmapOK;
  bool isPluginTool;
  bool b_hilite;
  bool m_btooltip_hiviz;
  wxRect last_rect;
  wxString pluginNormalIconSVG;
  wxString pluginRolloverIconSVG;
  wxString pluginToggledIconSVG;
  wxBitmap m_activeBitmap;
};

//---------------------------------------------------------------------------------------
//          ocpnFloatingToolbarDialog Implementation
//---------------------------------------------------------------------------------------

ocpnFloatingToolbarDialog::ocpnFloatingToolbarDialog(wxWindow *parent,
                                                     wxPoint position,
                                                     long orient,
                                                     float size_factor,
                                                     ToolbarDlgCallbacks tdc)
    : m_callbacks(tdc) {
  m_pparent = parent;
  m_profileChoice = nullptr;
  m_profileListenerId = 0;
  m_ptoolbar = NULL;

  m_opacity = 255;
  m_position = position;
  m_orient = orient;
  m_sizefactor = size_factor;
  m_cornerRadius = 0;

  m_bAutoHideToolbar = false;
  m_nAutoHideToolbar = 5;
  m_toolbar_scale_tools_shown = false;
  m_backcolorString = "GREY3";
  m_toolShowMask = "XXXXXXXXXXXXXXXX";
  n_toolbarHideMethod = TOOLBAR_HIDE_TO_GRABBER;
  b_canToggleOrientation = true;
  m_enableRolloverBitmaps = true;
  m_auxOffsetY = 0;

  m_ptoolbar = CreateNewToolbar();
  if (m_ptoolbar) m_ptoolbar->SetBackgroundColour(GetGlobalColor("GREY3"));
  m_cs = (ColorScheme)-1;

  m_style = g_StyleManager->GetCurrentStyle();
  SetULDockPosition(wxPoint(2, g_maintoolbar_y));

  SetGeometry(false, wxRect());

  //    Set initial "Dock" parameters
  m_dock_x = 0;
  m_dock_y = 0;
  m_block = false;

  m_texture = 0;

  m_marginsInvisible = m_style->marginsInvisible;

  m_FloatingToolbarConfigMenu = NULL;

  m_fade_timer.SetOwner(this);
  this->Connect(wxEVT_TIMER,
                wxTimerEventHandler(ocpnFloatingToolbarDialog::FadeTimerEvent),
                NULL, this);

  if (m_bAutoHideToolbar && (m_nAutoHideToolbar > 0))
    m_fade_timer.Start(m_nAutoHideToolbar * 1000);

  m_bsubmerged = false;
  m_benableSubmerge = true;

#ifndef __ANDROID__
  if (m_pparent && m_orient == wxTB_VERTICAL) {
    m_profileChoice = new wxButton(m_pparent, wxID_ANY, wxEmptyString);
    m_profileChoice->SetToolTip(_("Active Boat Profile"));
    m_profileChoice->SetMinSize(wxSize(wxRound(170 * m_sizefactor), -1));
    m_profileChoice->Bind(wxEVT_BUTTON,
                          &ocpnFloatingToolbarDialog::OnBoatProfileChoice,
                          this);
    m_profileListenerId = BoatProfileService::Get().AddListener(
        [this](const BoatProfile*) { RefreshBoatProfileChoice(); });
    RefreshBoatProfileChoice();
  }
#endif
}

ocpnFloatingToolbarDialog::~ocpnFloatingToolbarDialog() {
  if (m_profileListenerId)
    BoatProfileService::Get().RemoveListener(m_profileListenerId);
  if (m_profileChoice) {
    m_profileChoice->Unbind(wxEVT_BUTTON,
                            &ocpnFloatingToolbarDialog::OnBoatProfileChoice,
                            this);
    m_profileChoice->Destroy();
    m_profileChoice = nullptr;
  }
  delete m_FloatingToolbarConfigMenu;

  DestroyToolBar();
}

void ocpnFloatingToolbarDialog::FadeTimerEvent(wxTimerEvent &event) {
  if (n_toolbarHideMethod == TOOLBAR_HIDE_TO_FIRST_TOOL) {
    if (g_bmasterToolbarFull) {
      if (m_bAutoHideToolbar && (m_nAutoHideToolbar > 0) /*&& !m_bsubmerged*/) {
        // Double check the mouse position
        wxPoint mp =
            top_frame::Get()->GetAbstractPrimaryCanvas()->ScreenToClient(
                ::wxGetMousePosition());
        // in the toolbar?
        wxRect r = GetToolbarRect();
        if (r.Contains(mp)) return;

        wxCommandEvent event;
        event.SetId(ID_MASTERTOGGLE);
        top_frame::Get()->OnToolLeftClick(event);
      }
    }
  }
}

void ocpnFloatingToolbarDialog::AddToolItem(ToolbarItemContainer *item) {
  m_Items.push_back(item);
}

int ocpnFloatingToolbarDialog::RebuildToolbar() {
  ocpnToolBarSimple *tb = GetToolbar();
  if (!tb) return 0;

  // Iterate over the array of items added,
  // Creating the toolbar from enabled items.
  int i_count = 0;
  for (auto it = m_Items.cbegin(); it != m_Items.cend(); it++) {
    ToolbarItemContainer *tic = *it;
    if (!tic) continue;

    bool bEnabled = _toolbarConfigMenuUtil(tic);

    if (bEnabled) {
      wxToolBarToolBase *tool =
          tb->AddTool(tic->m_ID, tic->m_label, tic->m_bmpNormal,
                      tic->m_bmpDisabled, tic->m_toolKind, tic->m_tipString);
      tic->m_tool = tool;

      //  Plugin tools may have prescribed their own SVG toolbars as file
      //  locations.
      if (!tic->m_NormalIconSVG.IsEmpty()) {
        tb->SetToolBitmapsSVG(tic->m_ID, tic->m_NormalIconSVG,
                              tic->m_RolloverIconSVG, tic->m_ToggledIconSVG);
      }
    }

    i_count++;
  }

  return i_count;
}

void ocpnFloatingToolbarDialog::SetULDockPosition(wxPoint position) {
  if (position.x >= 0) m_dock_min_x = position.x;
  if (position.y >= 0) m_dock_min_y = position.y;
}

size_t ocpnFloatingToolbarDialog::GetToolCount() {
  if (m_ptoolbar)
    return m_ptoolbar->GetToolsCount();
  else
    return 0;
}

void ocpnFloatingToolbarDialog::SetToolShowMask(wxString mask) {}

void ocpnFloatingToolbarDialog::SetToolShowCount(int count) {
  if (m_ptoolbar) {
    m_ptoolbar->SetToolShowCount(count);
    m_ptoolbar->SetDirty(true);
  }
}

int ocpnFloatingToolbarDialog::GetToolShowCount() {
  if (m_ptoolbar)
    return m_ptoolbar->GetToolShowCount();
  else
    return 0;
}

void ocpnFloatingToolbarDialog::SetBackGroundColorString(wxString colorRef) {
  m_backcolorString = colorRef;
  SetColorScheme(m_cs);  // Causes a reload of background color
}

void ocpnFloatingToolbarDialog::OnKeyDown(wxKeyEvent &event) { event.Skip(); }

void ocpnFloatingToolbarDialog::OnKeyUp(wxKeyEvent &event) { event.Skip(); }

void ocpnFloatingToolbarDialog::CreateConfigMenu() {
  if (m_FloatingToolbarConfigMenu) delete m_FloatingToolbarConfigMenu;
  m_FloatingToolbarConfigMenu = new wxMenu();
}

bool ocpnFloatingToolbarDialog::_toolbarConfigMenuUtil(
    ToolbarItemContainer *tic) {
  if (m_FloatingToolbarConfigMenu) {
    wxMenuItem *menuitem;

    if (tic->m_ID == ID_MOB && g_bPermanentMOBIcon) return true;

    if (tic->m_bRequired) return true;
    if (tic->m_bPlugin) return true;

    // Item ID trickery is needed because the wxCommandEvents for menu item
    // clicked and toolbar button clicked are 100% identical, so if we use same
    // id's we can't tell the events apart.

    int idOffset = 100;  // Hopefully no more than 100 total icons...
    int menuItemId = tic->m_ID + idOffset;

    menuitem = m_FloatingToolbarConfigMenu->FindItem(menuItemId);

    if (menuitem) {
      return menuitem->IsChecked();
    }

    menuitem = m_FloatingToolbarConfigMenu->AppendCheckItem(menuItemId,
                                                            tic->m_tipString);
    size_t n = m_FloatingToolbarConfigMenu->GetMenuItemCount();
    menuitem->Check(m_configString.Len() >= n
                        ? m_configString.GetChar(n - 1) == 'X'
                        : true);
    return menuitem->IsChecked();
  } else
    return true;
}

void ocpnFloatingToolbarDialog::EnableTool(int toolid, bool enable) {
  if (m_ptoolbar) m_ptoolbar->EnableTool(toolid, enable);
}

void ocpnFloatingToolbarDialog::SetColorScheme(ColorScheme cs) {
  m_cs = cs;
  wxColour back_color = GetGlobalColor(m_backcolorString);

  if (m_ptoolbar) {
    m_ptoolbar->SetToggledBackgroundColour(GetGlobalColor("GREY1"));
    m_ptoolbar->SetColorScheme(cs);
  }
  if (m_profileChoice) {
    m_profileChoice->SetBackgroundColour(GetGlobalColor("GREY3"));
    m_profileChoice->SetForegroundColour(GetGlobalColor("DILG1"));
    m_profileChoice->Refresh();
  }
}

wxSize ocpnFloatingToolbarDialog::GetToolSize() {
  wxSize style_tool_size;
  if (m_ptoolbar) {
    style_tool_size = m_style->GetToolSize();

    style_tool_size.x *= m_sizefactor;
    style_tool_size.y *= m_sizefactor;
  } else {
    style_tool_size.x = 32;
    style_tool_size.y = 32;
  }

  return style_tool_size;
}

void ocpnFloatingToolbarDialog::SetGeometry(bool bAvoid, wxRect rectAvoid) {
  if (m_ptoolbar) {
    wxSize style_tool_size = m_style->GetToolSize();

    style_tool_size.x *= m_sizefactor;
    style_tool_size.y *= m_sizefactor;

    m_ptoolbar->SetToolBitmapSize(style_tool_size);

    wxSize tool_size = m_ptoolbar->GetToolBitmapSize();
    int grabber_width = m_style->GetIcon("grabber").GetWidth();

    int max_rows = 10;
    int max_cols = 100;

    if (m_pparent) {
      int avoid_start =
          m_pparent->GetClientSize().x -
          (tool_size.x + m_style->GetToolSeparation()) * 2;  // default
      if (bAvoid && !rectAvoid.IsEmpty()) {
        avoid_start = m_pparent->GetClientSize().x - rectAvoid.width -
                      10;  // this is compass window, if shown
      }

      max_rows = (m_pparent->GetClientSize().y /
                  (tool_size.y + m_style->GetToolSeparation())) -
                 2;

      max_cols = (avoid_start - grabber_width) /
                 (tool_size.x + m_style->GetToolSeparation());
      max_cols -= 1;

      if (m_orient == wxTB_VERTICAL)
        max_rows = wxMax(max_rows, 2);  // at least two rows
      else
        max_cols = wxMax(max_cols, 2);  // at least two columns
    }

    if (m_orient == wxTB_VERTICAL)
      m_ptoolbar->SetMaxRowsCols(32000, 1);
    else
      m_ptoolbar->SetMaxRowsCols(100, max_cols);
    if (m_orient == wxTB_VERTICAL) {
      int viewport_height = 0;
      if (m_pparent) {
        viewport_height = m_pparent->GetClientSize().y - m_dock_min_y -
                          m_auxOffsetY - (2 * GetFloatingInset());
      }
      if (viewport_height <= 0) viewport_height = tool_size.y * max_rows;
      m_ptoolbar->SetViewportHeight(viewport_height);
    }
    m_ptoolbar->SetSizeFactor(m_sizefactor);
    m_toolbar_image.Destroy();
  }
}

int ocpnFloatingToolbarDialog::GetFloatingInset() const { return 0; }

void ocpnFloatingToolbarDialog::SetDefaultPosition() {
  if (m_block) return;

  if (m_pparent && m_ptoolbar) {
    wxSize cs = m_pparent->GetClientSize();
    if (-1 == m_dock_x)
      m_position.x = m_dock_min_x;
    else if (1 == m_dock_x)
      m_position.x = cs.x - m_ptoolbar->m_maxWidth;

    if (-1 == m_dock_y)
      m_position.y = m_dock_min_y;
    else if (1 == m_dock_y)
      m_position.y = cs.y - m_ptoolbar->m_maxHeight;

    m_position.x = wxMin(cs.x - m_ptoolbar->m_maxWidth, m_position.x);
    m_position.y = wxMin(cs.y - m_ptoolbar->m_maxHeight, m_position.y);

    m_position.x = wxMax(m_dock_min_x, m_position.x);
    m_position.y = wxMax(m_dock_min_y, m_position.y);

    m_position.y += m_auxOffsetY;
    m_position.y += GetFloatingInset();

    g_maintoolbar_x = m_position.x;
    g_maintoolbar_y = m_position.y;
    PositionBoatProfileChoice();

    // take care of left docked instrument windows and don't blast the main
    // toolbar on top of them, hinding instruments this positions the main
    // toolbar directly right of the left docked instruments onto the chart
    //        wxPoint screen_pos = m_pparent->ClientToScreen( m_position );
    // wxPoint screen_pos =
    // gFrame->GetPrimaryCanvas()->ClientToScreen(m_position);

    //  GTK sometimes has trouble with ClientToScreen() if executed in the
    //  context of an event handler The position of the window is calculated
    //  incorrectly if a deferred Move() has not been processed yet. So work
    //  around this here... Discovered with a Dashboard window left-docked,
    //  toggled on and off by toolbar tool.

    //  But this causes another problem. If a toolbar is NOT left docked, it
    //  will walk left by two pixels on each call to Reposition().
    // TODO
  }
}

void ocpnFloatingToolbarDialog::Submerge() {
  m_bsubmerged = true;
  // Hide();
  if (m_ptoolbar) m_ptoolbar->KillTooltip();
}

void ocpnFloatingToolbarDialog::HideTooltip() {
#ifndef __ANDROID__
  if (m_ptoolbar) m_ptoolbar->HideTooltip();
#endif
}

void ocpnFloatingToolbarDialog::ShowTooltips() {
#ifndef __ANDROID__
  if (m_ptoolbar) m_ptoolbar->EnableTooltips();
#endif
}

void ocpnFloatingToolbarDialog::ToggleOrientation() {}

wxRect ocpnFloatingToolbarDialog::GetToolbarRect() {
  return wxRect(m_position.x, m_position.y, m_ptoolbar->m_maxWidth,
                m_ptoolbar->m_maxHeight);
}

wxSize ocpnFloatingToolbarDialog::GetToolbarSize() {
  return wxSize(m_ptoolbar->m_maxWidth, m_ptoolbar->m_maxHeight);
}

wxPoint ocpnFloatingToolbarDialog::GetToolbarPosition() {
  return wxPoint(m_position.x, m_position.y);
}

bool ocpnFloatingToolbarDialog::MouseEvent(wxMouseEvent &event) {
  if (g_disable_main_toolbar) return false;
  if (m_ptoolbar) {
    bool bproc = m_ptoolbar->OnMouseEvent(event, m_position);
    if (bproc) m_ptoolbar->CreateBitmap();
    return bproc;
  } else
    return false;
}

void ocpnFloatingToolbarDialog::RefreshToolbar() {
  if (m_ptoolbar) {
    if (m_ptoolbar->IsDirty()) {
      Realize();
      PositionBoatProfileChoice();
      top_frame::Get()->GetAbstractPrimaryCanvas()->Refresh();
    }
  }
}

void ocpnFloatingToolbarDialog::SetAutoHideTimer(int time) {
  m_nAutoHideToolbar = time;
  if (m_bAutoHideToolbar) {
    m_fade_timer.Stop();
    m_fade_timer.Start(m_nAutoHideToolbar * 1000);
  }
}

void ocpnFloatingToolbarDialog::RefreshFadeTimer() {
  if (m_bAutoHideToolbar && (m_nAutoHideToolbar > 0)) {
    m_fade_timer.Start(m_nAutoHideToolbar * 1000);
  }
}

void ocpnFloatingToolbarDialog::SetToolShortHelp(int id, const wxString &help) {
  if (m_ptoolbar) m_ptoolbar->SetToolShortHelp(id, help);
}

void ocpnFloatingToolbarDialog::Realize() {
  if (m_ptoolbar) {
    m_ptoolbar->Realize();
    m_ptoolbar->CreateBitmap();
    m_toolbar_image.Destroy();
    PositionBoatProfileChoice();
  }
}

void ocpnFloatingToolbarDialog::DrawDC(ocpnDC &dc, double displayScale) {
  if (m_ptoolbar) {
    PositionBoatProfileChoice();
    m_ptoolbar->CreateBitmap();
    if (m_ptoolbar->GetBitmap().IsOk()) {
      dc.DrawBitmap(m_ptoolbar->GetBitmap(), m_position.x, m_position.y, false);
      m_ptoolbar->SetDirty(false);
    }
  }
}

void ocpnFloatingToolbarDialog::DrawGL(ocpnDC &gldc, double displayScale) {
  if (g_disable_main_toolbar) return;

#ifdef ocpnUSE_GL
  if (!m_ptoolbar) return;
  PositionBoatProfileChoice();

  wxColour backColor = GetGlobalColor("GREY3");
  gldc.SetBrush(wxBrush(backColor));
  gldc.SetPen(wxPen(backColor));

  wxRect r = GetToolbarRect();
  int m_end_margin = wxMin(GetToolSize().x, GetToolSize().y) / 8;

  if (m_orient == wxHORIZONTAL)
    gldc.DrawRoundedRectangle(
        (r.x - m_end_margin / 2) * displayScale, (r.y - 1) * displayScale,
        (r.width + m_end_margin) * displayScale, (r.height + 2) * displayScale,
        (m_end_margin * 1) * displayScale);
  else
    gldc.DrawRoundedRectangle(
        (r.x - 1) * displayScale, (r.y - m_end_margin / 2) * displayScale,
        (r.width + 2) * displayScale, (r.height + m_end_margin) * displayScale,
        (m_end_margin * 1.5) * displayScale);

  int width = GetToolbarSize().x;
  int height = GetToolbarSize().y;

  m_ptoolbar->CreateBitmap(displayScale);

  // Make a GL texture
  if (!m_texture) {
    glGenTextures(1, &m_texture);

    glBindTexture(g_texture_rectangle_format, m_texture);
    glTexParameterf(g_texture_rectangle_format, GL_TEXTURE_MIN_FILTER,
                    GL_NEAREST);
    glTexParameteri(g_texture_rectangle_format, GL_TEXTURE_MAG_FILTER,
                    GL_NEAREST);
    glTexParameteri(g_texture_rectangle_format, GL_TEXTURE_WRAP_S,
                    GL_CLAMP_TO_EDGE);
    glTexParameteri(g_texture_rectangle_format, GL_TEXTURE_WRAP_T,
                    GL_CLAMP_TO_EDGE);
  } else {
    glBindTexture(g_texture_rectangle_format, m_texture);
  }

  if (!m_toolbar_image.IsOk()) {
    // fill texture data
    m_toolbar_image = m_ptoolbar->GetBitmap().ConvertToImage();

    unsigned char *d = m_toolbar_image.GetData();
    unsigned char *e = new unsigned char[4 * width * height];
    for (int y = 0; y < height; y++)
      for (int x = 0; x < width; x++) {
        int i = y * width + x;
        memcpy(e + 4 * i, d + 3 * i, 3);
        e[4 * i + 3] = 255;  // d[3*i + 2] == 255 ? 0:255; //255 - d[3 * i + 2];
      }
    glTexImage2D(g_texture_rectangle_format, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, e);
    delete[] e;
    glDisable(g_texture_rectangle_format);
    glDisable(GL_BLEND);
  }

  // Render the texture
  if (m_texture) {
    glEnable(g_texture_rectangle_format);
    glBindTexture(g_texture_rectangle_format, m_texture);
    glEnable(GL_BLEND);

    int x0 = GetToolbarPosition().x, x1 = x0 + width;
    int y0 = GetToolbarPosition().y - 0, y1 = y0 + height;
    x0 *= displayScale;
    x1 *= displayScale;
    y0 *= displayScale;
    y1 *= displayScale;

    float tx, ty;
    if (GL_TEXTURE_RECTANGLE_ARB == g_texture_rectangle_format)
      tx = width, ty = height;
    else
      tx = ty = 1;

    float coords[8];
    float uv[8];

    // normal uv
    uv[0] = 0;
    uv[1] = 0;
    uv[2] = tx;
    uv[3] = 0;
    uv[4] = tx;
    uv[5] = ty;
    uv[6] = 0;
    uv[7] = ty;

    // pixels
    coords[0] = x0;
    coords[1] = y0;
    coords[2] = x1;
    coords[3] = y0;
    coords[4] = x1;
    coords[5] = y1;
    coords[6] = x0;
    coords[7] = y1;

    m_callbacks.render_gl_textures(gldc, coords, uv);
    glDisable(g_texture_rectangle_format);
    glBindTexture(g_texture_rectangle_format, 0);
    glDisable(GL_BLEND);
  }
#endif

  return;
}

void ocpnFloatingToolbarDialog::OnToolLeftClick(wxCommandEvent &event) {
  // Since Dialog events don't propagate automatically, we send it explicitly
  // (instead of relying on event.Skip()). Send events up the window hierarchy

  m_pparent->GetEventHandler()->AddPendingEvent(event);
#ifndef __WXQT__
  wxTheApp->GetTopWindow()->Raise();
#endif
}

ocpnToolBarSimple *ocpnFloatingToolbarDialog::GetToolbar() {
  if (!m_ptoolbar) {
    m_ptoolbar = CreateNewToolbar();
  }

  return m_ptoolbar;
}

ocpnToolBarSimple *ocpnFloatingToolbarDialog::CreateNewToolbar() {
  long winstyle = wxNO_BORDER | wxTB_FLAT;
  winstyle |= m_orient;

  m_ptoolbar = new ocpnToolBarSimple(this, -1, wxPoint(-1, -1), wxSize(-1, -1),
                                     winstyle, m_orient);

  // m_ptoolbar->SetBackgroundColour(GetGlobalColor("GREY2"));
  // m_ptoolbar->ClearBackground();
  m_ptoolbar->SetToggledBackgroundColour(GetGlobalColor("GREY1"));
  m_ptoolbar->SetColorScheme(m_cs);
  m_ptoolbar->EnableRolloverBitmaps(GetEnableRolloverBitmaps());

  return m_ptoolbar;
}

void ocpnFloatingToolbarDialog::RefreshBoatProfileChoice() {
  if (!m_profileChoice) return;

  auto& service = BoatProfileService::Get();
  service.EnsureActiveProfile(_("My Boat"));
  const wxString active_id = service.GetActiveProfileId();

  m_profileChoiceIds.clear();

  wxString active_label = _("Boat Profile");
  for (const auto& profile : service.GetProfiles()) {
    m_profileChoiceIds.push_back(profile.id);
    if (profile.id == active_id) {
      active_label = profile.name.empty() ? _("Unnamed Boat") : profile.name;
    }
  }

  const int text_width = wxRound(145 * m_sizefactor);
  m_profileChoice->SetLabel(EllipsizeProfileLabel(m_profileChoice,
                                                  active_label, text_width));
  PositionBoatProfileChoice();
}

void ocpnFloatingToolbarDialog::PositionBoatProfileChoice() {
  if (!m_profileChoice || !m_pparent || !m_ptoolbar) return;
  if (m_orient != wxTB_VERTICAL) {
    m_profileChoice->Hide();
    return;
  }

  const int gap = wxMax(6, wxRound(6 * m_sizefactor));
  const int width = wxRound(180 * m_sizefactor);
  const wxSize best = m_profileChoice->GetBestSize();
  const int height = best.y > 0 ? best.y : wxRound(28 * m_sizefactor);
  wxPoint pos(m_position.x + m_ptoolbar->m_maxWidth + gap, m_position.y);
  wxSize parent_size = m_pparent->GetClientSize();
  if (parent_size.x > 0) pos.x = wxMin(pos.x, parent_size.x - width - gap);
  pos.x = wxMax(gap, pos.x);
  m_profileChoice->SetSize(pos.x, pos.y, width, height);
  m_profileChoice->Show();
  m_profileChoice->Raise();
}

void ocpnFloatingToolbarDialog::OnBoatProfileChoice(wxCommandEvent &event) {
  if (!m_profileChoice) return;

  auto& service = BoatProfileService::Get();
  wxString error;

  enum {
    ID_PROFILE_BASE = wxID_HIGHEST + 4200,
    ID_PROFILE_MANAGE = wxID_HIGHEST + 5200,
    ID_PROFILE_CREATE
  };

  wxMenu menu;
  m_profileChoiceIds.clear();
  int index = 0;
  const wxString active_id = service.GetActiveProfileId();
  for (const auto& profile : service.GetProfiles()) {
    wxString label = profile.name.empty() ? _("Unnamed Boat") : profile.name;
    const int id = ID_PROFILE_BASE + index;
    wxMenuItem* item = menu.AppendRadioItem(id, label);
    item->Check(profile.id == active_id);
    m_profileChoiceIds.push_back(profile.id);
    ++index;
  }

  if (!m_profileChoiceIds.empty()) menu.AppendSeparator();
  menu.Append(ID_PROFILE_MANAGE, _("Manage Boat Profiles..."));
  menu.Append(ID_PROFILE_CREATE, _("Create New Boat Profile..."));

  menu.Bind(wxEVT_MENU, [this, &service](wxCommandEvent& event) {
    wxString error;
    const int selection = event.GetId() - ID_PROFILE_BASE;
    if (selection >= 0 && selection < (int)m_profileChoiceIds.size()) {
      if (!service.SetActiveProfile(m_profileChoiceIds[selection], &error)) {
        wxMessageBox(error, _("Boat Profile"), wxOK | wxICON_WARNING,
                     wxTheApp->GetTopWindow());
      }
      return;
    }

    if (event.GetId() == ID_PROFILE_MANAGE) {
      BoatProfileManagerDialog dlg(wxTheApp->GetTopWindow(), false);
      dlg.ShowModal();
    } else if (event.GetId() == ID_PROFILE_CREATE) {
      BoatProfileManagerDialog dlg(wxTheApp->GetTopWindow(), true);
      dlg.ShowModal();
    }

    RefreshBoatProfileChoice();
  });

  wxPoint pos(0, m_profileChoice->GetSize().y);
  m_profileChoice->PopupMenu(&menu, pos);
  RefreshBoatProfileChoice();
}

void ocpnFloatingToolbarDialog::DestroyToolBar() {
  g_toolbarConfig = GetToolConfigString();

  if (m_ptoolbar) {
    m_ptoolbar->ClearTools();
    delete m_ptoolbar;  //->Destroy();
    m_ptoolbar = NULL;
  }

  for (auto it = m_Items.cbegin(); it != m_Items.cend(); it++) {
    delete *it;
  }
  m_Items.clear();
}

bool ocpnFloatingToolbarDialog::CheckAndAddPlugInTool(ocpnToolBarSimple *tb) {
  if (!g_pi_manager) return false;

  bool bret = false;
  int n_tools = tb->GetToolsCount();

  //    Walk the PlugIn tool spec array, checking the requested position
  //    If a tool has been requested by a plugin at this position, add it
  ArrayOfPlugInToolbarTools tool_array =
      g_pi_manager->GetPluginToolbarToolArray();

  for (unsigned int i = 0; i < tool_array.GetCount(); i++) {
    PlugInToolbarToolContainer *pttc = tool_array.Item(i);
    if (pttc->position == n_tools) {
      wxBitmap *ptool_bmp;

      switch (m_cs) {
        case GLOBAL_COLOR_SCHEME_DAY:
          ptool_bmp = pttc->bitmap_day;
          ;
          break;
        case GLOBAL_COLOR_SCHEME_DUSK:
          ptool_bmp = pttc->bitmap_dusk;
          break;
        case GLOBAL_COLOR_SCHEME_NIGHT:
          ptool_bmp = pttc->bitmap_night;
          break;
        default:
          ptool_bmp = pttc->bitmap_day;
          ;
          break;
      }

      wxToolBarToolBase *tool =
          tb->AddTool(pttc->id, wxString(pttc->label), *(ptool_bmp),
                      wxString(pttc->shortHelp), pttc->kind);

      tb->SetToolBitmapsSVG(pttc->id, pttc->pluginNormalIconSVG,
                            pttc->pluginRolloverIconSVG,
                            pttc->pluginToggledIconSVG);

      bret = true;
    }
  }

  //    If we added a tool, call again (recursively) to allow for adding
  //    adjacent tools
  if (bret)
    while (CheckAndAddPlugInTool(tb)) { /* nothing to do */
    }

  return bret;
}

void ocpnFloatingToolbarDialog::EnableRolloverBitmaps(bool bEnable) {
  m_enableRolloverBitmaps = bEnable;
  if (m_ptoolbar) m_ptoolbar->EnableRolloverBitmaps(bEnable);
}

// ----------------------------------------------------------------------------

// ============================================================================
// implementation
// ============================================================================
// ----------------------------------------------------------------------------
BEGIN_EVENT_TABLE(ocpnToolBarSimple, wxEvtHandler)
EVT_TIMER(TOOLTIPON_TIMER, ocpnToolBarSimple::OnToolTipTimerEvent)
EVT_TIMER(TOOLTIPOFF_TIMER, ocpnToolBarSimple::OnToolTipOffTimerEvent)
END_EVENT_TABLE()

// ----------------------------------------------------------------------------
// tool bar tools creation
// ----------------------------------------------------------------------------

wxToolBarToolBase *ocpnToolBarSimple::CreateTool(
    int id, const wxString &label, const wxBitmap &bmpNormal,
    const wxBitmap &bmpDisabled, wxItemKind kind, wxObject *clientData,
    const wxString &shortHelp, const wxString &longHelp) {
  if (m_style->NativeToolIconExists(label)) {
    return new ocpnToolBarTool(this, id, label, bmpNormal, bmpDisabled, kind,
                               clientData, shortHelp, longHelp);
  } else {
    wxString testToolname = g_pi_manager->GetToolOwnerCommonName(id);

    if (testToolname == "") {  // Not a PlugIn tool...
      return new ocpnToolBarTool(this, id, bmpNormal, bmpDisabled, kind,
                                 clientData, shortHelp, longHelp);
    } else {
      return new ocpnToolBarTool(this, id, label, bmpNormal, bmpDisabled, kind,
                                 clientData, shortHelp, longHelp);
    }
  }
}

// ----------------------------------------------------------------------------
// ocpnToolBarSimple creation
// ----------------------------------------------------------------------------

void ocpnToolBarSimple::Init() {
  m_currentRowsOrColumns = 0;

  m_lastX = m_lastY = 0;

  m_maxWidth = m_maxHeight = 0;

  m_pressedTool = m_currentTool = -1;

  m_xPos = m_yPos = wxDefaultCoord;

  m_style = g_StyleManager->GetCurrentStyle();

  m_defaultWidth = 16;
  m_defaultHeight = 15;

  m_toggle_bg_color = wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE);
  m_toolOutlineColour.Set("BLACK");
  m_last_ro_tool = NULL;

  m_btoolbar_is_zooming = false;
  m_sizefactor = 1.0f;

  m_last_plugin_down_id = -1;
  m_leftDown = false;
  m_nShowTools = 0;
  m_scrollOffset = 0;
  m_viewportHeight = 0;
  m_contentHeight = 0;
  m_btooltip_show = false;
#ifndef __ANDROID__
  EnableTooltips();
#endif
  m_tbenableRolloverBitmaps = false;
}

wxToolBarToolBase *ocpnToolBarSimple::DoAddTool(
    int id, const wxString &label, const wxBitmap &bitmap,
    const wxBitmap &bmpDisabled, wxItemKind kind, const wxString &shortHelp,
    const wxString &longHelp, wxObject *clientData, wxCoord xPos,
    wxCoord yPos) {
  // rememeber the position for DoInsertTool()
  m_xPos = xPos;
  m_yPos = yPos;

  // InvalidateBestSize();
  return InsertTool(GetToolsCount(), id, label, bitmap, bmpDisabled, kind,
                    shortHelp, longHelp, clientData);
}

///

wxToolBarToolBase *ocpnToolBarSimple::AddTool(
    int toolid, const wxString &label, const wxBitmap &bitmap,
    const wxBitmap &bmpDisabled, wxItemKind kind, const wxString &shortHelp,
    const wxString &longHelp, wxObject *data) {
  // InvalidateBestSize();
  ocpnToolBarTool *tool = (ocpnToolBarTool *)InsertTool(
      GetToolsCount(), toolid, label, bitmap, bmpDisabled, kind, shortHelp,
      longHelp, data);
  return tool;
}

wxToolBarToolBase *ocpnToolBarSimple::InsertTool(
    size_t pos, int id, const wxString &label, const wxBitmap &bitmap,
    const wxBitmap &bmpDisabled, wxItemKind kind, const wxString &shortHelp,
    const wxString &longHelp, wxObject *clientData) {
  wxCHECK_MSG(pos <= GetToolsCount(), (wxToolBarToolBase *)NULL,
              "invalid position in wxToolBar::InsertTool()");

  wxToolBarToolBase *tool = CreateTool(id, label, bitmap, bmpDisabled, kind,
                                       clientData, shortHelp, longHelp);

  if (!InsertTool(pos, tool)) {
    delete tool;

    return NULL;
  }

  return tool;
}

wxToolBarToolBase *ocpnToolBarSimple::InsertTool(size_t pos,
                                                 wxToolBarToolBase *tool) {
  wxCHECK_MSG(pos <= GetToolsCount(), (wxToolBarToolBase *)NULL,
              "invalid position in wxToolBar::InsertTool()");

  if (!tool || !DoInsertTool(pos, tool)) {
    return NULL;
  }

  m_tools.Insert(pos, tool);
  m_nShowTools++;

  return tool;
}

bool ocpnToolBarSimple::DoInsertTool(size_t WXUNUSED(pos),
                                     wxToolBarToolBase *toolBase) {
  ocpnToolBarTool *tool = (ocpnToolBarTool *)toolBase;

  // Check if the plugin is inserting same-named tools. Make sure they have
  // different names, otherwise the style manager cannot differentiate between
  // them.
  if (tool->isPluginTool) {
    for (unsigned int i = 0; i < GetToolsCount(); i++) {
      if (tool->GetToolname() ==
          ((ocpnToolBarTool *)m_tools.Item(i)->GetData())->GetToolname()) {
        tool->toolname << "1";
      }
    }
  }

  tool->m_x = m_xPos;
  if (tool->m_x == wxDefaultCoord) tool->m_x = m_style->GetLeftMargin();

  tool->m_y = m_yPos;
  if (tool->m_y == wxDefaultCoord) tool->m_y = m_style->GetTopMargin();

  if (tool->IsButton()) {
    tool->SetSize(GetToolSize());

    // Calculate reasonable max size in case Layout() not called
    if ((tool->m_x + tool->GetNormalBitmap().GetWidth() +
         m_style->GetLeftMargin()) > m_maxWidth)
      m_maxWidth =
          (wxCoord)((tool->m_x + tool->GetWidth() + m_style->GetLeftMargin()));

    if ((tool->m_y + tool->GetNormalBitmap().GetHeight() +
         m_style->GetTopMargin()) > m_maxHeight)
      m_maxHeight =
          (wxCoord)((tool->m_y + tool->GetHeight() + m_style->GetTopMargin()));
  }

  else if (tool->IsControl()) {
    tool->SetSize(tool->GetControl()->GetSize());
  }

  tool->b_hilite = false;

  return true;
}

bool ocpnToolBarSimple::DoDeleteTool(size_t WXUNUSED(pos),
                                     wxToolBarToolBase *tool) {
  // VZ: didn't test whether it works, but why not...
  tool->Detach();

  if (m_last_ro_tool == tool) m_last_ro_tool = NULL;

  // Refresh(false);

  return true;
}

bool ocpnToolBarSimple::Create(ocpnFloatingToolbarDialog *parent, wxWindowID id,
                               const wxPoint &pos, const wxSize &size,
                               long style, int orient) {
  m_parentContainer = parent;
  m_orient = orient;

  if (IsVertical()) {
    m_lastX = 7;
    m_lastY = 3;

    m_maxRows = 32000;  // a lot
    m_maxCols = 1;
  } else {
    m_lastX = 3;
    m_lastY = 7;

    m_maxRows = 1;
    m_maxCols = 32000;  // a lot
  }

  // SetCursor(*wxSTANDARD_CURSOR);

  m_tooltip_timer.SetOwner(this, TOOLTIPON_TIMER);
  m_tooltipoff_timer.SetOwner(this, TOOLTIPOFF_TIMER);
  m_tooltip_off = 3000;

  m_tbenableRolloverBitmaps = false;

  return true;
}

ocpnToolBarSimple::~ocpnToolBarSimple() {}

void ocpnToolBarSimple::EnableTooltips() {
#ifndef __ANDROID__
  m_btooltip_show = true;
#endif
}

void ocpnToolBarSimple::DisableTooltips() {
#ifndef __ANDROID__
  ocpnToolBarSimple::m_btooltip_show = false;
#endif
}

void ocpnToolBarSimple::KillTooltip() {
  m_btooltip_show = false;

  TooltipManager::Get().HideTooltip();
  m_tooltip_timer.Stop();

  wxTheApp->GetTopWindow()->Raise();
  top_frame::Get()->GetAbstractFocusCanvas()->TriggerDeferredFocus();
}

void ocpnToolBarSimple::HideTooltip() {
#ifndef __ANDROID__
  TooltipManager::Get().HideTooltip();
#endif
}

void ocpnToolBarSimple::SetColorScheme(ColorScheme cs) {
#ifndef __ANDROID__
  TooltipManager::Get().SetColorScheme(cs);
#endif
  m_toolOutlineColour = GetGlobalColor("UIBDR");

  m_currentColorScheme = cs;
}

bool ocpnToolBarSimple::Realize() {
  if (IsVertical())
    m_style->SetOrientation(wxTB_VERTICAL);
  else
    m_style->SetOrientation(wxTB_HORIZONTAL);

  wxSize toolSize = wxSize(-1, -1);
  int separatorSize = m_style->GetToolSeparation() * m_sizefactor;
  int topMargin = m_style->GetTopMargin() * m_sizefactor;
  int leftMargin = m_style->GetLeftMargin() * m_sizefactor;

  m_currentRowsOrColumns = 0;
  m_LineCount = 1;
  m_lastX = leftMargin;
  m_lastY = topMargin;
  m_maxWidth = 0;
  m_maxHeight = 0;

  ocpnToolBarTool *lastTool = NULL;
  bool firstNode = true;
  wxToolBarToolsList::compatibility_iterator node = m_tools.GetFirst();

  int iNode = 0;

  while (node) {
    if (iNode >= m_nShowTools) break;

    ocpnToolBarTool *tool = (ocpnToolBarTool *)node->GetData();

    // Set the tool size to be the size of the first non-separator tool, usually
    // the first one
    if (toolSize.x == -1) {
      if (!tool->IsSeparator()) {
        toolSize.x = tool->m_width;
        toolSize.y = tool->m_height;
      }
    }

    tool->firstInLine = firstNode;
    tool->lastInLine = false;
    firstNode = false;

    tool->last_rect.width = 0;  // mark it invalid

    if (tool->IsSeparator()) {
      // if (GetWindowStyleFlag() & wxTB_HORIZONTAL) {
      // if (m_currentRowsOrColumns >= m_maxCols)
      // m_lastY += separatorSize;
      // else
      //  m_lastX += separatorSize;
      //}
      // else
      {
        if (m_currentRowsOrColumns >= m_maxRows)
          m_lastX += separatorSize;
        else
          m_lastY += separatorSize;
      }
    } else if (tool->IsButton()) {
      if (!IsVertical()) {
        if (m_currentRowsOrColumns >= m_maxCols) {
          tool->firstInLine = true;
          if (lastTool && m_LineCount > 1) lastTool->lastInLine = true;
          m_LineCount++;
          m_currentRowsOrColumns = 0;
          m_lastX = leftMargin;
          m_lastY += toolSize.y + topMargin;
        }
        tool->m_x = (wxCoord)m_lastX;
        tool->m_y = (wxCoord)m_lastY;

        tool->trect = wxRect(tool->m_x, tool->m_y, toolSize.x, toolSize.y);
        tool->trect.Inflate(separatorSize / 2, topMargin);

        m_lastX += toolSize.x + separatorSize;
      } else {
        if (m_currentRowsOrColumns >= m_maxRows) {
          tool->firstInLine = true;
          if (lastTool) lastTool->lastInLine = true;
          m_LineCount++;
          m_currentRowsOrColumns = 0;
          m_lastX += toolSize.x + leftMargin;
          m_lastY = topMargin;
        }
        tool->m_x = (wxCoord)m_lastX;
        tool->m_y = (wxCoord)m_lastY;

        tool->trect = wxRect(tool->m_x, tool->m_y, toolSize.x, toolSize.y);
        tool->trect.Inflate((separatorSize / 2), topMargin);

        m_lastY += toolSize.y + separatorSize;
      }
      m_currentRowsOrColumns++;
    }
    // else
    // if (tool->IsControl()) {
    // tool->m_x = (wxCoord)(m_lastX);
    // tool->m_y = (wxCoord)(m_lastY - (topMargin / 2));

    // tool->trect =
    //     wxRect(tool->m_x, tool->m_y, tool->GetWidth(), tool->GetHeight());
    // tool->trect.Inflate(separatorSize / 2, topMargin);

    // wxSize s = tool->GetControl()->GetSize();
    // m_lastX += s.x + separatorSize;
    //}

    if (m_lastX > m_maxWidth) m_maxWidth = m_lastX;
    if (m_lastY > m_maxHeight) m_maxHeight = m_lastY;

    lastTool = tool;
    node = node->GetNext();
    iNode++;
  }
  if (lastTool && (m_LineCount > 1 || IsVertical()))
    lastTool->lastInLine = true;

  m_contentHeight = m_maxHeight;

  if (!IsVertical()) {
    m_maxHeight += toolSize.y;
    m_maxHeight += m_style->GetBottomMargin();
  } else {
    m_contentHeight += m_style->GetBottomMargin() * m_sizefactor;
    m_maxWidth = GetSidebarWidth(toolSize.x);
    m_maxHeight = m_viewportHeight > 0 ? m_viewportHeight : m_contentHeight;
    if (m_maxHeight <= 0) m_maxHeight = m_contentHeight;
    m_scrollOffset = wxMin(m_scrollOffset, GetMaxScrollOffset());
    m_maxWidth += m_style->GetRightMargin() * m_sizefactor;
  }

  m_bitmap = wxNullBitmap;

  return true;
}

wxBitmap &ocpnToolBarSimple::CreateBitmap(double display_scale) {
  if (m_bitmap.IsOk()) return m_bitmap;

  // Make the bitmap
  int width = m_maxWidth;
  int height = m_maxHeight;

  wxMemoryDC mdc;
  wxBitmap bm(width, height);
  mdc.SelectObject(bm);
  mdc.SetBackground(wxBrush(GetBackgroundColour()));
  mdc.Clear();
  mdc.SetClippingRegion(0, 0, width, height);

  wxFont labelFont = *wxNORMAL_FONT;
  labelFont.SetPointSize(wxMax(8, labelFont.GetPointSize()));
  mdc.SetFont(labelFont);
  mdc.SetTextForeground(GetSidebarLabelColour());

  //  In a loop, draw the tools
  for (wxToolBarToolsList::compatibility_iterator node = m_tools.GetFirst();
       node; node = node->GetNext()) {
    wxToolBarToolBase *tool = node->GetData();
    ocpnToolBarTool *tools = (ocpnToolBarTool *)tool;
    wxRect toolRect = tools->trect;
    CreateToolBitmap(tool);

    if (tools->m_activeBitmap.IsOk()) {
      int drawY = tools->m_y;
      if (IsVertical()) drawY -= m_scrollOffset;

      if (!IsVertical() ||
          wxRect(0, drawY, width, tools->GetHeight())
              .Intersects(wxRect(0, 0, width, height))) {
        mdc.DrawBitmap(tools->m_activeBitmap, tools->m_x, drawY, false);

        if (IsVertical() && tools->IsButton()) {
          wxString label = GetDisplayLabel(tools);
          if (!label.IsEmpty()) {
            int labelX = tools->m_x + tools->GetWidth() + GetSidebarLabelGap();
            int labelMaxWidth = width - labelX - GetSidebarLabelPadding();
            if (labelMaxWidth > 0) {
              wxString displayLabel = wxControl::Ellipsize(
                  label, mdc, wxELLIPSIZE_END, labelMaxWidth);
              wxCoord textHeight;
              mdc.GetTextExtent(displayLabel, NULL, &textHeight);
              int labelY = drawY + (tools->GetHeight() - textHeight) / 2;
              mdc.DrawText(displayLabel, labelX, labelY);
            }
          }
        }
      }
    }
    int yyp = 5;
  }

  mdc.DestroyClippingRegion();
  mdc.SelectObject(wxNullBitmap);

  m_bitmap = bm;
  return m_bitmap;
}

void ocpnToolBarSimple::OnToolTipTimerEvent(wxTimerEvent &event) {
  if (!top_frame::Get())  // In case gFrame was already destroyed, but the
                          // toolbar still exists (Which should not happen,
                          // ever.)
    return;

  if (m_btooltip_show /*&& IsShown()*/) {
    if (m_last_ro_tool) {
      wxString s = m_last_ro_tool->GetShortHelp();

      if (s.Len()) {
        // Calculate tooltip position relative to the tool
        wxPoint pos_in_toolbar(m_last_ro_tool->m_x, m_last_ro_tool->m_y);
        if (IsVertical()) pos_in_toolbar.y -= m_scrollOffset;
        pos_in_toolbar.x += m_last_ro_tool->m_width + 2;

        wxPoint screenPosition =
            top_frame::Get()->GetAbstractPrimaryCanvas()->ClientToScreen(
                pos_in_toolbar);

        // Show tooltip using new system
        TooltipManager::Get().ShowTooltipAtPosition(
            top_frame::Get()->GetAbstractPrimaryCanvas()->GetWindow(), s,
            screenPosition, m_last_ro_tool->m_btooltip_hiviz);

#ifndef __WXOSX__
        wxTheApp->GetTopWindow()->Raise();
#endif

#ifndef __ANDROID__
        if (g_btouch) m_tooltipoff_timer.Start(m_tooltip_off, wxTIMER_ONE_SHOT);
#endif
      }
    }
  }
}

void ocpnToolBarSimple::OnToolTipOffTimerEvent(wxTimerEvent &event) {
  HideTooltip();
}

bool ocpnToolBarSimple::OnMouseEvent(wxMouseEvent &event, wxPoint &position) {
  wxCoord x, y;
  event.GetPosition(&x, &y);

  // in the toolbar?
  wxRect r = wxRect(position, wxSize(m_maxWidth, m_maxHeight));
  if (!r.Contains(x, y)) {
    HideTooltip();
    return false;
  }

  m_parentContainer->RefreshFadeTimer();

  if (IsVertical() && event.GetWheelRotation()) {
    int wheelDelta = event.GetWheelDelta();
    if (wheelDelta == 0) wheelDelta = 120;
    int rowDelta = GetToolSize().y + m_style->GetToolSeparation();
    int delta = -event.GetWheelRotation() * rowDelta / wheelDelta;
    if (ScrollBy(delta)) {
      m_parentContainer->Realize();
      top_frame::Get()->GetAbstractPrimaryCanvas()->Refresh(true);
    }
    return true;
  }

  ocpnToolBarTool *tool =
      (ocpnToolBarTool *)FindToolForPosition(x - position.x, y - position.y);
  if (tool == NULL) {
    m_tooltipoff_timer.Start(m_tooltip_off, wxTIMER_ONE_SHOT);
    return true;
  } else
    m_tooltipoff_timer.Stop();

  // tooltips
  if (tool && tool->IsButton() /*&& IsShown()*/) {
    if (m_btooltip_show) {
      if (tool != m_last_ro_tool) {
        TooltipManager::Get().HideTooltip();
      }

#ifndef __ANDROID__
      if (!TooltipManager::Get().IsShown()) {
        if (!m_tooltip_timer.IsRunning()) {
          m_tooltip_timer.Start(m_one_shot, wxTIMER_ONE_SHOT);
        }
      }
#endif
    }
  }

  m_last_ro_tool = tool;

  // Left button pressed.
  if (event.LeftIsDown()) m_leftDown = true;  // trigger on

  if (event.LeftDown() && tool->IsEnabled()) {
    if (tool->CanBeToggled()) {
      tool->Toggle();
      tool->bitmapOK = false;
      SetDirty(true);
      m_bitmap = wxNullBitmap;
    }

    //        Look for PlugIn tools
    //        If found, make the callback.
    if (g_pi_manager) {
      ArrayOfPlugInToolbarTools tool_array =
          g_pi_manager->GetPluginToolbarToolArray();
      for (unsigned int i = 0; i < tool_array.GetCount(); i++) {
        PlugInToolbarToolContainer *pttc = tool_array[i];
        if (tool->GetId() == pttc->id) {
          opencpn_plugin_113 *ppi =
              dynamic_cast<opencpn_plugin_113 *>(pttc->m_pplugin);
          if (ppi) {
            ppi->OnToolbarToolDownCallback(pttc->id);
            m_last_plugin_down_id = pttc->id;
          }
        }
      }
    }
  } else if (event.RightDown()) {
    OnRightClick(tool->GetId(), x, y);
  }

  // Left Button Released.  Only this action confirms selection.
  // If the button is enabled and it is not a toggle tool and it is
  // in the pressed state, then raise the button and call OnLeftClick.
  //
  // Unfortunately, some touch screen drivers do not send "LeftIsDown" events.
  // Nor do they report "LeftIsDown" in any state.
  // c.f rPI "official" 7" panel.

  // So, for this logic, assume in touch mode that the m_leftDown flag may not
  // be set, and process the left-up event anyway.
  if (event.LeftUp() && tool->IsEnabled() && (m_leftDown || g_btouch)) {
    // Pass the OnLeftClick event to tool
    if (!OnLeftClick(tool->GetId(), tool->IsToggled()) &&
        tool->CanBeToggled()) {
      // If it was a toggle, and OnLeftClick says No Toggle allowed,
      // then change it back
      tool->Toggle();
      tool->bitmapOK = false;
    }

    DoPluginToolUp();
    m_leftDown = false;
    return true;
  }

  return true;
}

// ----------------------------------------------------------------------------
// drawing
// ----------------------------------------------------------------------------

void ocpnToolBarSimple::CreateToolBitmap(wxToolBarToolBase *toolBase) {
  ocpnToolBarTool *tool = (ocpnToolBarTool *)toolBase;

  wxBitmap bmp = wxNullBitmap;

  bool bNeedClear = !tool->bitmapOK;

  if (tool->bitmapOK) {
    if (tool->IsEnabled()) {
      bmp = tool->GetNormalBitmap();
      if (!bmp.IsOk()) {
        bmp =
            m_style->GetToolIcon(tool->GetToolname(), TOOLICON_NORMAL,
                                 tool->rollover, tool->m_width, tool->m_height);
        tool->SetNormalBitmap(bmp);
        tool->bitmapOK = true;
      }
    } else {
      bmp = tool->GetDisabledBitmap();
      if (!bmp.IsOk()) {
        bmp = m_style->GetToolIcon(tool->GetToolname(), TOOLICON_DISABLED,
                                   false, tool->m_width, tool->m_height);
        tool->SetDisabledBitmap(bmp);
        tool->bitmapOK = true;
      }
    }
  } else {
    if (tool->isPluginTool) {
      int toggleFlag = tool->IsToggled() ? TOOLICON_TOGGLED : TOOLICON_NORMAL;

      // First try getting the icon from an SVG definition.
      // If it is not found, try to see if it is available in the style
      // If not there, we build a new icon from the style BG and the (default)
      // plugin icon.

      wxString svgFile = tool->pluginNormalIconSVG;
      if (toggleFlag) {
        if (tool->pluginToggledIconSVG.Length())
          svgFile = tool->pluginToggledIconSVG;
      }
      if (tool->rollover) {
        if (tool->pluginRolloverIconSVG.Length())
          svgFile = tool->pluginRolloverIconSVG;
      }

      if (!svgFile.IsEmpty()) {  // try SVG
#ifdef ocpnUSE_SVG
        bmp = LoadSVG(svgFile, tool->m_width, tool->m_height);
        if (bmp.IsOk()) {
          bmp = m_style->BuildPluginIcon(bmp, toggleFlag, m_sizefactor);
        } else
          bmp =
              m_style->BuildPluginIcon(tool->pluginNormalIcon, TOOLICON_NORMAL);
#endif
      }

      if (!bmp.IsOk() || bmp.IsNull()) {
        if (m_style->NativeToolIconExists(tool->GetToolname())) {
          bmp = m_style->GetToolIcon(tool->GetToolname(), toggleFlag,
                                     tool->rollover, tool->m_width,
                                     tool->m_height);
        } else {
          bmp = wxNullBitmap;
        }

        if (bmp.IsNull()) {  // Tool icon not found in style definition
          // bmp = m_style->BuildPluginIcon(tool->pluginNormalIcon, toggleFlag);
          bmp = tool->pluginNormalIcon;
          if (fabs(m_sizefactor - 1.0) > 0.01) {
            if (tool->m_width && tool->m_height) {
              wxImage scaled_image = bmp.ConvertToImage();
              bmp = wxBitmap(scaled_image.Scale(tool->m_width, tool->m_height,
                                                wxIMAGE_QUALITY_HIGH));
            }
          }
        }
      }
      tool->SetNormalBitmap(bmp);
      tool->bitmapOK = true;
    } else {  // Not a plugin tool
      bmp = tool->GetNormalBitmap();
      if (tool->IsEnabled()) {
        if (tool->IsToggled()) {
          if (!tool->bitmapOK) {
            if (m_style->NativeToolIconExists(tool->GetToolname())) {
              bmp = m_style->GetToolIcon(tool->GetToolname(), TOOLICON_TOGGLED,
                                         tool->rollover, tool->m_width,
                                         tool->m_height);
              tool->SetNormalBitmap(bmp);
            }
          }
        }

        else {
          if (!tool->bitmapOK) {
            if (m_style->NativeToolIconExists(tool->GetToolname())) {
              bmp = m_style->GetToolIcon(tool->GetIconName(), TOOLICON_NORMAL,
                                         tool->rollover, tool->m_width,
                                         tool->m_height);
              tool->SetNormalBitmap(bmp);
            }
          }
        }

        tool->bitmapOK = true;
      } else {
        bmp = m_style->GetToolIcon(tool->GetToolname(), TOOLICON_DISABLED,
                                   false, tool->m_width, tool->m_height);
        tool->SetDisabledBitmap(bmp);
        tool->bitmapOK = true;
      }
    }
  }
  tool->m_activeBitmap = bmp;
}

// NB! The current DrawTool code assumes that plugin tools are never disabled
// when they are present on the toolbar, since disabled plugins are removed.

void ocpnToolBarSimple::DrawTool(wxDC &dc, wxToolBarToolBase *toolBase) {
  ocpnToolBarTool *tool = (ocpnToolBarTool *)toolBase;
  // PrepareDC(dc);

  wxPoint drawAt(tool->m_x, tool->m_y);
  wxBitmap bmp = wxNullBitmap;

  bool bNeedClear = !tool->bitmapOK;

  if (tool->bitmapOK) {
    if (tool->IsEnabled()) {
      bmp = tool->GetNormalBitmap();
      if (!bmp.IsOk()) {
        bmp =
            m_style->GetToolIcon(tool->GetToolname(), TOOLICON_NORMAL,
                                 tool->rollover, tool->m_width, tool->m_height);
        tool->SetNormalBitmap(bmp);
        tool->bitmapOK = true;
      }
    } else {
      bmp = tool->GetDisabledBitmap();
      if (!bmp.IsOk()) {
        bmp = m_style->GetToolIcon(tool->GetToolname(), TOOLICON_DISABLED,
                                   false, tool->m_width, tool->m_height);
        tool->SetDisabledBitmap(bmp);
        tool->bitmapOK = true;
      }
    }
  } else {
    if (tool->isPluginTool) {
      int toggleFlag = tool->IsToggled() ? TOOLICON_TOGGLED : TOOLICON_NORMAL;

      // First try getting the icon from an SVG definition.
      // If it is not found, try to see if it is available in the style
      // If not there, we build a new icon from the style BG and the (default)
      // plugin icon.

      wxString svgFile = tool->pluginNormalIconSVG;
      if (toggleFlag) {
        if (tool->pluginToggledIconSVG.Length())
          svgFile = tool->pluginToggledIconSVG;
      }
      if (tool->rollover) {
        if (tool->pluginRolloverIconSVG.Length())
          svgFile = tool->pluginRolloverIconSVG;
      }

      if (!svgFile.IsEmpty()) {  // try SVG
#ifdef ocpnUSE_SVG
        bmp = LoadSVG(svgFile, tool->m_width, tool->m_height);
        if (bmp.IsOk()) {
          bmp = m_style->BuildPluginIcon(bmp, toggleFlag, m_sizefactor);
        } else
          bmp =
              m_style->BuildPluginIcon(tool->pluginNormalIcon, TOOLICON_NORMAL);
#endif
      }

      if (!bmp.IsOk() || bmp.IsNull()) {
        if (m_style->NativeToolIconExists(tool->GetToolname())) {
          bmp = m_style->GetToolIcon(tool->GetToolname(), toggleFlag,
                                     tool->rollover, tool->m_width,
                                     tool->m_height);
        } else {
          bmp = wxNullBitmap;
        }

        if (bmp.IsNull()) {  // Tool icon not found
          if (tool->rollover) {
            bmp =
                m_style->BuildPluginIcon(tool->pluginRolloverIcon, toggleFlag);
            if (!bmp.IsOk()) {
              bmp =
                  m_style->BuildPluginIcon(tool->pluginNormalIcon, toggleFlag);
            }
          } else {
            bmp = m_style->BuildPluginIcon(tool->pluginNormalIcon, toggleFlag);
          }
          if (fabs(m_sizefactor - 1.0) > 0.01) {
            if (tool->m_width && tool->m_height) {
              wxImage scaled_image = bmp.ConvertToImage();
              bmp = wxBitmap(scaled_image.Scale(tool->m_width, tool->m_height,
                                                wxIMAGE_QUALITY_HIGH));
            }
          }
        }
      }
      tool->SetNormalBitmap(bmp);
      tool->bitmapOK = true;
    } else {  // Not a plugin tool
      bmp = tool->GetNormalBitmap();
      if (tool->IsEnabled()) {
        if (tool->IsToggled()) {
          if (!tool->bitmapOK) {
            if (m_style->NativeToolIconExists(tool->GetToolname())) {
              bmp = m_style->GetToolIcon(tool->GetToolname(), TOOLICON_TOGGLED,
                                         tool->rollover, tool->m_width,
                                         tool->m_height);
              tool->SetNormalBitmap(bmp);
            }
          }
        }

        else {
          if (!tool->bitmapOK) {
            if (m_style->NativeToolIconExists(tool->GetToolname())) {
              bmp = m_style->GetToolIcon(tool->GetIconName(), TOOLICON_NORMAL,
                                         tool->rollover, tool->m_width,
                                         tool->m_height);
              tool->SetNormalBitmap(bmp);
            }
          }
        }

        tool->bitmapOK = true;
      } else {
        bmp = m_style->GetToolIcon(tool->GetToolname(), TOOLICON_DISABLED,
                                   false, tool->m_width, tool->m_height);
        tool->SetDisabledBitmap(bmp);
        tool->bitmapOK = true;
      }
    }
  }

  if (tool->firstInLine) {
    m_style->DrawToolbarLineStart(bmp, m_sizefactor);
  }
  if (tool->lastInLine) {
    m_style->DrawToolbarLineEnd(bmp, m_sizefactor);
  }

  if (bmp.GetWidth() != m_style->GetToolSize().x ||
      bmp.GetHeight() != m_style->GetToolSize().y) {
    //        drawAt.x -= ( bmp.GetWidth() - m_style->GetToolSize().x ) / 2;
    //        drawAt.y -= ( bmp.GetHeight() - m_style->GetToolSize().y ) / 2;
  }

  //      Clear the last drawn tool if necessary
  if ((tool->last_rect.width &&
       (tool->last_rect.x != drawAt.x || tool->last_rect.y != drawAt.y)) ||
      bNeedClear) {
    wxBrush bb(GetGlobalColor("GREY3"));
    dc.SetBrush(bb);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(tool->last_rect.x, tool->last_rect.y,
                     tool->last_rect.width, tool->last_rect.height);
  }

  //  could cache this in the tool...
  //  A bit of a hack here.  We only scale tools if they are to be magnified
  //  globally
  if (0 /*m_sizefactor > 1.0*/) {
    wxImage scaled_image = bmp.ConvertToImage();
    wxBitmap sbmp = wxBitmap(scaled_image.Scale(tool->m_width, tool->m_height,
                                                wxIMAGE_QUALITY_HIGH));
    dc.DrawBitmap(sbmp, drawAt);
    tool->last_rect =
        wxRect(drawAt.x, drawAt.y, sbmp.GetWidth(), sbmp.GetHeight());

  } else {
    dc.DrawBitmap(bmp, drawAt);
    tool->last_rect =
        wxRect(drawAt.x, drawAt.y, bmp.GetWidth(), bmp.GetHeight());
  }
}

// ----------------------------------------------------------------------------
// toolbar geometry
// ----------------------------------------------------------------------------

wxToolBarToolBase *ocpnToolBarSimple::FindToolForPosition(wxCoord x,
                                                          wxCoord y) {
  if (IsVertical()) y += m_scrollOffset;

  wxToolBarToolsList::compatibility_iterator node = m_tools.GetFirst();
  while (node) {
    ocpnToolBarTool *tool = (ocpnToolBarTool *)node->GetData();
    wxCoord hitWidth = IsVertical() ? m_maxWidth : tool->GetWidth();
    if ((x >= tool->m_x) && (y >= tool->m_y) &&
        (x < (tool->m_x + hitWidth)) && (y < (tool->m_y + tool->GetHeight()))) {
      return tool;
    }

    node = node->GetNext();
  }

  return (wxToolBarToolBase *)NULL;
}

void ocpnToolBarSimple::InvalidateBitmaps() {
  wxToolBarToolsList::compatibility_iterator node = m_tools.GetFirst();
  while (node) {
    ocpnToolBarTool *tool = (ocpnToolBarTool *)node->GetData();
    tool->bitmapOK = false;
    node = node->GetNext();
  }
  m_bitmap = wxNullBitmap;
}

wxRect ocpnToolBarSimple::GetToolRect(int tool_id) {
  wxRect rect;
  wxToolBarToolBase *tool = FindById(tool_id);
  if (tool) {
    ocpnToolBarTool *otool = (ocpnToolBarTool *)tool;
    if (otool) rect = otool->trect;
  }

  return rect;
}

// ----------------------------------------------------------------------------
// tool state change handlers
// ----------------------------------------------------------------------------

void ocpnToolBarSimple::DoEnableTool(wxToolBarToolBase *tool,
                                     bool WXUNUSED(enable)) {
  ocpnToolBarTool *t = (ocpnToolBarTool *)tool;
  t->bitmapOK = false;
}

void ocpnToolBarSimple::DoToggleTool(wxToolBarToolBase *tool,
                                     bool WXUNUSED(toggle)) {
  ocpnToolBarTool *t = (ocpnToolBarTool *)tool;
  t->bitmapOK = false;
  SetDirty(true);
}

void ocpnToolBarSimple::SetViewportHeight(int height) {
  m_viewportHeight = wxMax(0, height);
  m_scrollOffset = wxMin(m_scrollOffset, GetMaxScrollOffset());
  m_bitmap = wxNullBitmap;
}

wxCoord ocpnToolBarSimple::GetSidebarWidth(wxCoord toolWidth) const {
  return toolWidth + GetSidebarLabelGap() + GetSidebarLabelPadding() +
         wxRound(166 * m_sizefactor);
}

wxCoord ocpnToolBarSimple::GetSidebarLabelGap() const {
  return wxMax(8, wxRound(10 * m_sizefactor));
}

wxCoord ocpnToolBarSimple::GetSidebarLabelPadding() const {
  return wxMax(10, wxRound(12 * m_sizefactor));
}

wxColour ocpnToolBarSimple::GetSidebarLabelColour() const {
  return wxColour(196, 201, 204);
}

static wxString CleanToolbarLabel(wxString label) {
  if (label.IsEmpty()) return label;

  int tab = label.Find('\t');
  if (tab != wxNOT_FOUND) label = label.Left(tab);

  int shortcut = label.Find(" (");
  if (shortcut != wxNOT_FOUND) label = label.Left(shortcut);

  label.Trim(true);
  label.Trim(false);
  return label;
}

wxString ocpnToolBarSimple::GetPluginDisplayLabel(ocpnToolBarTool *tool) const {
  if (g_pi_manager) {
    ArrayOfPlugInToolbarTools tool_array =
        g_pi_manager->GetPluginToolbarToolArray();
    for (unsigned int i = 0; i < tool_array.GetCount(); i++) {
      PlugInToolbarToolContainer *pttc = tool_array[i];
      if (pttc && tool->GetId() == pttc->id) {
        wxString label = CleanToolbarLabel(pttc->label);
        if (!label.IsEmpty()) return label;

        label = CleanToolbarLabel(pttc->shortHelp);
        if (!label.IsEmpty()) return label;
      }
    }

    wxString owner = CleanToolbarLabel(
        g_pi_manager->GetToolOwnerCommonName(tool->GetId()));
    if (!owner.IsEmpty()) return owner;
  }

  return _("Plugin");
}

wxString ocpnToolBarSimple::GetDisplayLabel(ocpnToolBarTool *tool) const {
  if (!tool) return wxEmptyString;
  if (tool->isPluginTool) return GetPluginDisplayLabel(tool);

  switch (tool->GetId()) {
    case ID_MASTERTOGGLE:
      return _("Menu");
    case ID_SETTINGS:
      return _("Settings");
    case ID_MENU_ROUTE_NEW:
      return _("Create Route");
    case ID_ROUTEMANAGER:
      return _("Route & Mark Manager");
    case ID_TRACK:
      return _("Tracking");
    case ID_COLSCHEME:
      return _("Color Scheme");
    case ID_PRINT:
      return _("Print");
    case ID_ABOUT:
      return _("About");
    case ID_MOB:
      return _("MOB");
    default:
      break;
  }

  wxString label = CleanToolbarLabel(tool->GetShortHelp());
  if (!label.IsEmpty()) return label;

  label = CleanToolbarLabel(tool->GetLabel());
  if (!label.IsEmpty()) return label;

  return _("Tool");
}

int ocpnToolBarSimple::GetMaxScrollOffset() const {
  return wxMax(0, m_contentHeight - m_maxHeight);
}

bool ocpnToolBarSimple::ScrollBy(int delta) {
  int newOffset = wxMin(GetMaxScrollOffset(), wxMax(0, m_scrollOffset + delta));
  if (newOffset == m_scrollOffset) return false;

  m_scrollOffset = newOffset;
  m_bitmap = wxNullBitmap;
  return true;
}

// ----------------------------------------------------------------------------
// scrolling implementation
// ----------------------------------------------------------------------------

wxString ocpnToolBarSimple::GetToolShortHelp(int id) const {
  wxToolBarToolBase *tool = FindById(id);
  wxCHECK_MSG(tool, "", "no such tool");

  return tool->GetShortHelp();
}

wxString ocpnToolBarSimple::GetToolLongHelp(int id) const {
  wxToolBarToolBase *tool = FindById(id);
  wxCHECK_MSG(tool, "", "no such tool");

  return tool->GetLongHelp();
}

void ocpnToolBarSimple::SetToolShortHelp(int id, const wxString &help) {
  wxToolBarToolBase *tool = FindById(id);
  if (tool) {
    (void)tool->SetShortHelp(help);
  }
}

void ocpnToolBarSimple::SetToolLongHelp(int id, const wxString &help) {
  wxToolBarToolBase *tool = FindById(id);
  if (tool) {
    (void)tool->SetLongHelp(help);
  }
}

int ocpnToolBarSimple::GetToolPos(int id) const {
  size_t pos = 0;
  wxToolBarToolsList::compatibility_iterator node;

  for (node = m_tools.GetFirst(); node; node = node->GetNext()) {
    if (node->GetData()->GetId() == id) return pos;

    pos++;
  }

  return wxNOT_FOUND;
}
bool ocpnToolBarSimple::GetToolState(int id) const {
  wxToolBarToolBase *tool = FindById(id);
  wxCHECK_MSG(tool, false, "no such tool");

  return tool->IsToggled();
}

bool ocpnToolBarSimple::GetToolEnabled(int id) const {
  wxToolBarToolBase *tool = FindById(id);
  wxCHECK_MSG(tool, false, "no such tool");

  return tool->IsEnabled();
}

void ocpnToolBarSimple::ToggleTool(int id, bool toggle) {
  wxToolBarToolBase *tool = FindById(id);

  if (tool && tool->CanBeToggled() && tool->Toggle(toggle)) {
    DoToggleTool(tool, toggle);
    InvalidateBitmaps();
    top_frame::Get()->GetAbstractPrimaryCanvas()->Refresh(true);
  }
}

wxObject *ocpnToolBarSimple::GetToolClientData(int id) const {
  wxToolBarToolBase *tool = FindById(id);
  return tool ? tool->GetClientData() : (wxObject *)NULL;
}

void ocpnToolBarSimple::SetToolClientData(int id, wxObject *clientData) {
  wxToolBarToolBase *tool = FindById(id);

  wxCHECK_RET(tool, "no such tool in wxToolBar::SetToolClientData");

  tool->SetClientData(clientData);
}

void ocpnToolBarSimple::EnableTool(int id, bool enable) {
  wxToolBarToolBase *tool = FindById(id);
  if (tool) {
    if (tool->Enable(enable)) {
      DoEnableTool(tool, enable);
    }
  }

  ocpnFloatingToolbarDialog *parent = m_parentContainer;
  if (parent && parent->m_FloatingToolbarConfigMenu) {
    wxMenuItem *configItem = parent->m_FloatingToolbarConfigMenu->FindItem(id);
    if (configItem) configItem->Check(true);
  }
}

void ocpnToolBarSimple::SetToolTooltipHiViz(int id, bool b_hiviz) {
  ocpnToolBarTool *tool = (ocpnToolBarTool *)FindById(id);
  if (tool) {
    tool->SetTooltipHiviz(b_hiviz);
  }
}

void ocpnToolBarSimple::ClearTools() {
  while (GetToolsCount()) {
    DeleteToolByPos(0);
  }
}

int ocpnToolBarSimple::GetVisibleToolCount() {
  int counter = 0;
  wxToolBarToolsList::compatibility_iterator node = m_tools.GetFirst();
  while (node) {
    ocpnToolBarTool *tool = (ocpnToolBarTool *)node->GetData();
    counter++;
    node = node->GetNext();
  }
  return counter;
}

bool ocpnToolBarSimple::DeleteToolByPos(size_t pos) {
  wxCHECK_MSG(pos < GetToolsCount(), false,
              "invalid position in wxToolBar::DeleteToolByPos()");

  wxToolBarToolsList::compatibility_iterator node = m_tools.Item(pos);

  if (!DoDeleteTool(pos, node->GetData())) {
    return false;
  }

  delete node->GetData();
  m_tools.Erase(node);

  return true;
}

bool ocpnToolBarSimple::DeleteTool(int id) {
  size_t pos = 0;
  wxToolBarToolsList::compatibility_iterator node;
  for (node = m_tools.GetFirst(); node; node = node->GetNext()) {
    if (node->GetData()->GetId() == id) break;

    pos++;
  }

  if (!node || !DoDeleteTool(pos, node->GetData())) {
    return false;
  }

  delete node->GetData();
  m_tools.Erase(node);

  return true;
}

wxToolBarToolBase *ocpnToolBarSimple::AddSeparator() {
  return InsertSeparator(GetToolsCount());
}

wxToolBarToolBase *ocpnToolBarSimple::InsertSeparator(size_t pos) {
  wxCHECK_MSG(pos <= GetToolsCount(), (wxToolBarToolBase *)NULL,
              "invalid position in wxToolBar::InsertSeparator()");

  wxToolBarToolBase *tool =
      CreateTool(wxID_SEPARATOR, "", wxNullBitmap, wxNullBitmap,
                 wxITEM_SEPARATOR, (wxObject *)NULL, "", "");

  if (!tool || !DoInsertTool(pos, tool)) {
    delete tool;

    return NULL;
  }

  m_tools.Insert(pos, tool);
  m_nShowTools++;

  return tool;
}

wxToolBarToolBase *ocpnToolBarSimple::RemoveTool(int id) {
  size_t pos = 0;
  wxToolBarToolsList::compatibility_iterator node;
  for (node = m_tools.GetFirst(); node; node = node->GetNext()) {
    if (node->GetData()->GetId() == id) break;

    pos++;
  }

  if (!node) {
    // don't give any error messages - sometimes we might call RemoveTool()
    // without knowing whether the tool is or not in the toolbar
    return (wxToolBarToolBase *)NULL;
  }

  wxToolBarToolBase *tool = node->GetData();
  if (!DoDeleteTool(pos, tool)) {
    return (wxToolBarToolBase *)NULL;
  }

  m_tools.Erase(node);

  return tool;
}

wxControl *ocpnToolBarSimple::FindControl(int id) {
  for (wxToolBarToolsList::compatibility_iterator node = m_tools.GetFirst();
       node; node = node->GetNext()) {
    const wxToolBarToolBase *const tool = node->GetData();
    if (tool->IsControl()) {
      wxControl *const control = tool->GetControl();

      if (!control) {
        wxFAIL_MSG("NULL control in toolbar?");
      } else if (control->GetId() == id) {
        // found
        return control;
      }
    }
  }

  return NULL;
}

wxToolBarToolBase *ocpnToolBarSimple::FindById(int id) const {
  wxToolBarToolBase *tool = (wxToolBarToolBase *)NULL;

  for (wxToolBarToolsList::compatibility_iterator node = m_tools.GetFirst();
       node; node = node->GetNext()) {
    tool = node->GetData();
    if (tool->GetId() == id) {
      // found
      break;
    }

    tool = NULL;
  }

  return tool;
}

// ----------------------------------------------------------------------------
// event processing
// ----------------------------------------------------------------------------

// Only allow toggle if returns true

bool ocpnToolBarSimple::OnLeftClick(int id, bool toggleDown) {
  wxCommandEvent event(wxEVT_COMMAND_TOOL_CLICKED, id);
  // event.SetEventObject(this);

  // we use SetInt() to make wxCommandEvent::IsChecked() return toggleDown
  event.SetInt((int)toggleDown);

  // and SetExtraLong() for backwards compatibility
  event.SetExtraLong((long)toggleDown);

  top_frame::Get()->GetEventHandler()->AddPendingEvent(event);

  return true;
}

// Call when right button down.
void ocpnToolBarSimple::OnRightClick(int id, long WXUNUSED(x),
                                     long WXUNUSED(y)) {
  HideTooltip();

  if (m_parentContainer) {
    if (m_parentContainer->m_FloatingToolbarConfigMenu) {
      ToolbarChoicesDialog *dlg =
          new ToolbarChoicesDialog(NULL, m_parentContainer, -1, "SuperCPN",
                                   wxDefaultPosition, wxSize(100, 100));
      int rc = dlg->ShowModal();
      delete dlg;

      if (rc == wxID_OK) {
        wxCommandEvent event(wxEVT_COMMAND_TOOL_RCLICKED, id);
        event.SetEventObject(this);
        event.SetInt(id);

        top_frame::Get()->GetEventHandler()->AddPendingEvent(event);
      }
    }
  }
}

void ocpnToolBarSimple::DoPluginToolUp() {
  //        Look for PlugIn tools
  //        If found, make the callback.
  if (!g_pi_manager) return;

  ArrayOfPlugInToolbarTools tool_array =
      g_pi_manager->GetPluginToolbarToolArray();
  for (unsigned int i = 0; i < tool_array.GetCount(); i++) {
    PlugInToolbarToolContainer *pttc = tool_array[i];
    if (m_last_plugin_down_id == pttc->id) {
      opencpn_plugin_113 *ppi =
          dynamic_cast<opencpn_plugin_113 *>(pttc->m_pplugin);
      if (ppi) ppi->OnToolbarToolUpCallback(pttc->id);
    }
  }

  m_last_plugin_down_id = -1;
}

void ocpnToolBarSimple::SetToolNormalBitmapEx(wxToolBarToolBase *tool,
                                              const wxString &iconName) {
  if (tool) {
    ocpnToolBarTool *otool = (ocpnToolBarTool *)tool;
    if (otool) {
      ocpnStyle::Style *style = g_StyleManager->GetCurrentStyle();

      wxBitmap bmp = style->GetToolIcon(iconName, TOOLICON_NORMAL, false,
                                        otool->m_width, otool->m_height);
      tool->SetNormalBitmap(bmp);
      otool->SetIconName(iconName);
    }
  }
}

void ocpnToolBarSimple::SetToolNormalBitmapSVG(wxToolBarToolBase *tool,
                                               wxString fileSVG) {
  if (tool) {
    ocpnToolBarTool *otool = (ocpnToolBarTool *)tool;
    if (otool) {
      otool->pluginNormalIconSVG = fileSVG;
    }
  }
}

void ocpnToolBarSimple::SetToolBitmaps(int id, wxBitmap *bmp,
                                       wxBitmap *bmpRollover) {
  ocpnToolBarTool *tool = (ocpnToolBarTool *)FindById(id);
  if (tool) {
    if (tool->isPluginTool) {
      if (bmp->GetWidth() != tool->GetWidth()) {
        if (bmp->IsOk()) {
          wxImage ibmp = bmp->ConvertToImage();
          ibmp.Rescale(tool->GetWidth(), tool->GetHeight(),
                       wxIMAGE_QUALITY_HIGH);
          wxBitmap sbmp = wxBitmap(ibmp);
          tool->pluginNormalIcon = sbmp;
        }
      } else {
        tool->pluginNormalIcon = *bmp;
      }

      if (bmpRollover->GetWidth() != tool->GetWidth()) {
        if (bmpRollover->IsOk()) {
          wxImage ibmp = bmpRollover->ConvertToImage();
          ibmp.Rescale(tool->GetWidth(), tool->GetHeight(),
                       wxIMAGE_QUALITY_HIGH);
          wxBitmap sbmp = wxBitmap(ibmp);
          tool->pluginRolloverIcon = sbmp;
        }
      } else {
        tool->pluginRolloverIcon = *bmpRollover;
      }
      tool->bitmapOK = false;

    } else {
      tool->SetNormalBitmap(*bmp);
      tool->bitmapOK = true;
    }
    InvalidateBitmaps();
  }
}

void ocpnToolBarSimple::SetToolBitmapsSVG(int id, wxString fileSVGNormal,
                                          wxString fileSVGRollover,
                                          wxString fileSVGToggled) {
  ocpnToolBarTool *tool = (ocpnToolBarTool *)FindById(id);
  if (tool) {
    tool->pluginNormalIconSVG = fileSVGNormal;
    tool->pluginRolloverIconSVG = fileSVGRollover;
    tool->pluginToggledIconSVG = fileSVGToggled;
    tool->bitmapOK = false;
    InvalidateBitmaps();
  }
}

//-------------------------------------------------------------------------------------

ToolbarMOBDialog::ToolbarMOBDialog(wxWindow *parent)
    : wxDialog(parent, wxID_ANY, _("SuperCPN Alert"), wxDefaultPosition,
               wxSize(250, 230)) {
  wxBoxSizer *topSizer = new wxBoxSizer(wxVERTICAL);

  wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
  topSizer->Add(sizer, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, 5);

  choices.push_back(
      new wxRadioButton(this, 0, _("No, I don't want to hide it."),
                        wxDefaultPosition, wxDefaultSize, wxRB_GROUP));

  choices.push_back(new wxRadioButton(
      this, 1, _("No, and permanently remove the option to hide it."),
      wxDefaultPosition));

  choices.push_back(
      new wxRadioButton(this, 2, _("Yes, hide it."), wxDefaultPosition));

  wxStdDialogButtonSizer *buttonSizer =
      CreateStdDialogButtonSizer(wxOK | wxCANCEL);

  wxStaticText *textCtrl =
      new wxStaticText(this, wxID_ANY,
                       _("The Man Over Board button could be an important "
                         "safety feature.\nAre you sure you want to hide it?"));

  sizer->Add(textCtrl, 0, wxEXPAND | wxALL, 5);
  sizer->Add(choices[0], 0, wxEXPAND | wxALL, 5);
  sizer->Add(choices[1], 0, wxEXPAND | wxALL, 5);
  sizer->Add(choices[2], 0, wxEXPAND | wxALL, 5);
  sizer->Add(buttonSizer, 0, wxEXPAND | wxTOP, 5);

  topSizer->SetSizeHints(this);
  SetSizer(topSizer);
}

int ToolbarMOBDialog::GetSelection() {
  for (unsigned int i = 0; i < choices.size(); i++) {
    if (choices[i]->GetValue()) return choices[i]->GetId();
  }
  return 0;
}

/*!
 * ToolbarChoicesDialog event table definition
 */
BEGIN_EVENT_TABLE(ToolbarChoicesDialog, wxDialog)
END_EVENT_TABLE()

/*!
 * ToolbarChoicesDialog constructors
 */

ToolbarChoicesDialog::ToolbarChoicesDialog() {}

ToolbarChoicesDialog::ToolbarChoicesDialog(wxWindow *parent,
                                           ocpnFloatingToolbarDialog *sponsor,
                                           wxWindowID id,
                                           const wxString &caption,
                                           const wxPoint &pos,
                                           const wxSize &size, long style) {
  long wstyle = wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER;
  wxDialog::Create(parent, id, caption, pos, size, wstyle);

  m_configMenu = NULL;
  m_ToolbarDialogAncestor = sponsor;

  if (m_ToolbarDialogAncestor)
    m_configMenu = m_ToolbarDialogAncestor->m_FloatingToolbarConfigMenu;

  CreateControls();
  GetSizer()->Fit(this);

  RecalculateSize();
}

ToolbarChoicesDialog::~ToolbarChoicesDialog() {}

/*!
 * Control creation for ToolbarChoicesDialog
 */

void ToolbarChoicesDialog::CreateControls() {
  wxBoxSizer *itemBoxSizer1 = new wxBoxSizer(wxVERTICAL);
  SetSizer(itemBoxSizer1);

  wxScrolledWindow *itemDialog1 = new wxScrolledWindow(
      this, wxID_ANY, wxDefaultPosition, wxSize(-1, -1), wxHSCROLL | wxVSCROLL);
  itemDialog1->SetScrollRate(2, 2);

#ifdef __ANDROID__

  //  Set Dialog Font by custom crafted Qt Stylesheet.
  wxFont *qFont = GetOCPNScaledFont(_("Dialog"));

  wxString wqs = getFontQtStylesheet(qFont);
  wxCharBuffer sbuf = wqs.ToUTF8();
  QString qsb = QString(sbuf.data());

  QString qsbq = getQtStyleSheet();  // basic scrollbars, etc

  this->GetHandle()->setStyleSheet(qsb + qsbq);  // Concatenated style sheets

#endif
  itemBoxSizer1->Add(itemDialog1, 2, wxEXPAND | wxALL, 0);

  wxBoxSizer *itemBoxSizer2 = new wxBoxSizer(wxVERTICAL);
  itemDialog1->SetSizer(itemBoxSizer2);

  wxStaticBox *itemStaticBoxSizer3Static =
      new wxStaticBox(itemDialog1, wxID_ANY, _("Choose Toolbar Icons"));
  wxStaticBoxSizer *itemStaticBoxSizer3 =
      new wxStaticBoxSizer(itemStaticBoxSizer3Static, wxVERTICAL);
  itemBoxSizer2->Add(itemStaticBoxSizer3, 0, wxEXPAND | wxALL, 5);

  int nitems = 0;
  int max_width = -1;
  if (m_configMenu) {
    nitems = m_configMenu->GetMenuItemCount();

    cboxes.clear();
    for (int i = 0; i < nitems; i++) {
      if (i + ID_ZOOMIN == ID_MOB && g_bPermanentMOBIcon) continue;
      wxMenuItem *item = m_configMenu->FindItemByPosition(i);

      wxString label = item->GetItemLabel();
      int l = label.Len();
      max_width = wxMax(max_width, l);

      wxString windowName = "";
      if (item->GetId() == ID_MOB + 100) windowName = "MOBCheck";

      wxCheckBox *cb =
          new wxCheckBox(itemDialog1, -1, label, wxDefaultPosition,
                         wxDefaultSize, 0, wxDefaultValidator, windowName);
      //            wxCheckBox *cb = new wxCheckBox(itemDialog1, -1, label);
      itemStaticBoxSizer3->Add(cb, 0, wxALL | wxEXPAND, 2);
      cb->SetValue(item->IsChecked());

      cboxes.push_back(cb);
    }
  }

  itemBoxSizer1->SetMinSize((max_width + 20) * GetCharWidth(),
                            (nitems + 4) * GetCharHeight() * 2);

  wxBoxSizer *itemBoxSizerBottom = new wxBoxSizer(wxHORIZONTAL);
  itemBoxSizer1->Add(itemBoxSizerBottom, 0, wxALL | wxEXPAND, 5);

  wxBoxSizer *itemBoxSizerAux = new wxBoxSizer(wxHORIZONTAL);
  itemBoxSizerBottom->Add(itemBoxSizerAux, 1, wxALL, 3);

  wxBoxSizer *itemBoxSizer16 = new wxBoxSizer(wxHORIZONTAL);
  itemBoxSizerBottom->Add(itemBoxSizer16, 0, wxALL, 3);

  m_CancelButton =
      new wxButton(this, -1, _("Cancel"), wxDefaultPosition, wxDefaultSize, 0);
  itemBoxSizer16->Add(m_CancelButton, 0, wxALIGN_CENTER_VERTICAL | wxALL, 1);

  m_OKButton =
      new wxButton(this, -1, _("OK"), wxDefaultPosition, wxDefaultSize, 0);
  itemBoxSizer16->Add(m_OKButton, 0, wxALIGN_CENTER_VERTICAL | wxALL, 1);
  m_OKButton->SetDefault();

  m_CancelButton->Connect(
      wxEVT_COMMAND_BUTTON_CLICKED,
      wxCommandEventHandler(ToolbarChoicesDialog::OnCancelClick), NULL, this);
  m_OKButton->Connect(wxEVT_COMMAND_BUTTON_CLICKED,
                      wxCommandEventHandler(ToolbarChoicesDialog::OnOkClick),
                      NULL, this);

  SetColorScheme((ColorScheme)0);
}

void ToolbarChoicesDialog::SetColorScheme(ColorScheme cs) { DimeControl(this); }

void ToolbarChoicesDialog::OnCancelClick(wxCommandEvent &event) {
  EndModal(wxID_CANCEL);
}

void ToolbarChoicesDialog::OnOkClick(wxCommandEvent &event) {
  unsigned int ncheck = 0;

  wxString toolbarConfigSave = m_ToolbarDialogAncestor->GetToolConfigString();
  wxString new_toolbarConfig = toolbarConfigSave;

  for (unsigned int i = 0; i < cboxes.size(); i++) {
    wxCheckBox *cb = cboxes[i];
    wxString cbName = cb->GetName();  // Special flag passed into checkbox ctor
                                      // to find the "MOB" item
    if (cbName.IsSameAs("MOBCheck") && !cb->IsChecked()) {
      // Ask if really want to disable MOB button
      ToolbarMOBDialog mdlg(this);
      int dialog_ret = mdlg.ShowModal();
      int answer = mdlg.GetSelection();
      if (dialog_ret == wxID_OK) {
        if (answer == 1) {
          g_bPermanentMOBIcon = true;
          cb->SetValue(true);
        } else if (answer == 0) {
          cb->SetValue(true);
        }
      } else {  // wxID_CANCEL
        new_toolbarConfig = toolbarConfigSave;
        return;
      }
    }
    if (m_configMenu) {
      wxMenuItem *item = m_configMenu->FindItemByPosition(i);
      if (new_toolbarConfig.Len() > i) {
        new_toolbarConfig.SetChar(i, cb->IsChecked() ? 'X' : '.');
      } else {
        new_toolbarConfig.Append(cb->IsChecked() ? 'X' : '.');
      }
      item->Check(cb->IsChecked());
      if (cb->IsChecked()) ncheck++;
    }
  }

#if 0
     //  We always must have one Tool enabled.  Make it the Options tool....
     if( 0 == ncheck){
         new_toolbarConfig.SetChar( ID_SETTINGS -ID_ZOOMIN , 'X' );

         int idOffset = ID_PLUGIN_BASE - ID_ZOOMIN + 100;

         if(m_configMenu){
             wxMenuItem *item = m_configMenu->FindItem(ID_SETTINGS + idOffset);
             if(item)
                item->Check( true );
         }
     }
#endif
  m_ToolbarDialogAncestor->SetToolConfigString(new_toolbarConfig);

  EndModal(wxID_OK);
}

void ToolbarChoicesDialog::RecalculateSize() {
  wxSize esize = GetSize();

  if (GetParent()) {
    wxSize dsize = GetParent()->GetClientSize();
    esize.y = wxMin(esize.y, dsize.y - (4 * GetCharHeight()));
    esize.x = wxMin(esize.x, dsize.x - (2 * GetCharHeight()));
    SetSize(esize);
    Centre();

  } else {
    wxSize fsize = g_Platform->getDisplaySize();
    fsize.y = wxMin(esize.y, fsize.y - (4 * GetCharHeight()));
    fsize.x = wxMin(esize.x, fsize.x - (2 * GetCharHeight()));
    SetSize(fsize);
    CentreOnScreen();
#ifdef __ANDROID__
    Move(GetPosition().x, 10);
#endif
  }
}
