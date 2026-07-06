#include "model/boat_profile.h"

#include <algorithm>
#include <chrono>
#include <random>
#include <sstream>

#include <wx/file.h>
#include <wx/filename.h>
#include <wx/jsonreader.h>
#include <wx/jsonval.h>
#include <wx/jsonwriter.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include "model/config_vars.h"

namespace {

const int kSchemaVersion = 1;

bool IsPositive(double value) { return value > 0.0; }

void SetError(wxString* error, const wxString& value) {
  if (error) *error = value;
}

wxString GetEnv(const wxString& name) {
  const char* value = wxGetenv(name);
  return value ? wxString::FromUTF8(value) : wxString();
}

wxString JoinPath(const wxString& dir, const wxString& leaf) {
  return wxFileName(dir, leaf).GetFullPath();
}

wxString ReadString(const wxJSONValue& object, const wxString& key,
                    const wxString& fallback = wxEmptyString) {
  wxJSONValue value = object.ItemAt(key);
  return value.IsString() ? value.AsString() : fallback;
}

double ReadDouble(const wxJSONValue& object, const wxString& key,
                  double fallback = 0.0) {
  wxJSONValue value = object.ItemAt(key);
  if (value.IsDouble()) return value.AsDouble();
  if (value.IsInt()) return static_cast<double>(value.AsInt());
  if (value.IsUInt()) return static_cast<double>(value.AsUInt());
  if (value.IsLong()) return static_cast<double>(value.AsLong());
  if (value.IsULong()) return static_cast<double>(value.AsULong());
  return fallback;
}

int ReadInt(const wxJSONValue& object, const wxString& key, int fallback = 0) {
  wxJSONValue value = object.ItemAt(key);
  if (value.IsInt()) return value.AsInt();
  if (value.IsLong()) return static_cast<int>(value.AsLong());
  if (value.IsUInt()) return static_cast<int>(value.AsUInt());
  if (value.IsULong()) return static_cast<int>(value.AsULong());
  return fallback;
}

wxJSONValue ToJson(const BoatProfile& profile) {
  wxJSONValue value;
  value["id"] = profile.id;
  value["name"] = profile.name;
  value["length_m"] = profile.length_m;
  value["beam_m"] = profile.beam_m;
  value["draft_m"] = profile.draft_m;
  value["air_draft_m"] = profile.air_draft_m;
  value["vessel_type"] = profile.vessel_type;
  value["displacement_t"] = profile.displacement_t;
  value["sail_area_m2"] = profile.sail_area_m2;
  value["cruising_speed_kn"] = profile.cruising_speed_kn;
  value["max_speed_kn"] = profile.max_speed_kn;
  value["motoring_speed_kn"] = profile.motoring_speed_kn;
  value["engine_consumption_lph"] = profile.engine_consumption_lph;
  value["polar_file"] = profile.polar_file;
  value["upwind_twa_deg"] = profile.upwind_twa_deg;
  value["downwind_twa_deg"] = profile.downwind_twa_deg;
  value["min_routing_wind_kn"] = profile.min_routing_wind_kn;
  value["max_routing_wind_kn"] = profile.max_routing_wind_kn;
  value["current_grid_spacing_deg"] = profile.current_grid_spacing_deg;
  value["current_duration_hours"] = profile.current_duration_hours;
  value["current_step_hours"] = profile.current_step_hours;
  value["current_provider"] = profile.current_provider;
  value["data_directory"] = profile.data_directory;
  value["notes"] = profile.notes;
  return value;
}

BoatProfile FromJson(const wxJSONValue& value) {
  BoatProfile profile;
  profile.id = ReadString(value, "id");
  profile.name = ReadString(value, "name");
  profile.length_m = ReadDouble(value, "length_m");
  profile.beam_m = ReadDouble(value, "beam_m");
  profile.draft_m = ReadDouble(value, "draft_m");
  profile.air_draft_m = ReadDouble(value, "air_draft_m");
  profile.vessel_type =
      ReadString(value, "vessel_type", "Cruising sailboat");
  profile.displacement_t = ReadDouble(value, "displacement_t");
  profile.sail_area_m2 = ReadDouble(value, "sail_area_m2");
  profile.cruising_speed_kn = ReadDouble(value, "cruising_speed_kn");
  profile.max_speed_kn = ReadDouble(value, "max_speed_kn");
  profile.motoring_speed_kn = ReadDouble(value, "motoring_speed_kn");
  profile.engine_consumption_lph =
      ReadDouble(value, "engine_consumption_lph");
  profile.polar_file = ReadString(value, "polar_file");
  profile.upwind_twa_deg = ReadDouble(value, "upwind_twa_deg", 45.0);
  profile.downwind_twa_deg = ReadDouble(value, "downwind_twa_deg", 150.0);
  profile.min_routing_wind_kn = ReadDouble(value, "min_routing_wind_kn");
  profile.max_routing_wind_kn = ReadDouble(value, "max_routing_wind_kn", 40.0);
  profile.current_grid_spacing_deg =
      ReadDouble(value, "current_grid_spacing_deg", 0.05);
  profile.current_duration_hours = ReadInt(value, "current_duration_hours", 24);
  profile.current_step_hours = ReadInt(value, "current_step_hours", 1);
  profile.current_provider = ReadString(value, "current_provider", "synthetic");
  profile.data_directory = ReadString(value, "data_directory");
  profile.notes = ReadString(value, "notes");
  return profile;
}

bool SameProfileId(const BoatProfile& profile, const wxString& id) {
  return profile.id == id;
}

}  // namespace

BoatProfileStore::BoatProfileStore(const wxString& file_name)
    : m_file_name(file_name) {}

wxString BoatProfileStore::DefaultConfigDir() {
  wxString test_home = GetEnv("SUPERCPN_TEST_HOME");
  if (!test_home.empty()) return JoinPath(test_home, "supercpn");

#ifdef __WXMSW__
  wxString appdata = GetEnv("APPDATA");
  if (!appdata.empty()) return JoinPath(appdata, "SuperCPN");
  return JoinPath(wxGetHomeDir(), "SuperCPN");
#elif defined(__WXOSX__)
  return JoinPath(JoinPath(wxGetHomeDir(), "Library/Application Support"),
                  "SuperCPN");
#else
  wxString xdg_config = GetEnv("XDG_CONFIG_HOME");
  if (!xdg_config.empty()) return JoinPath(xdg_config, "supercpn");
  return JoinPath(JoinPath(wxGetHomeDir(), ".config"), "supercpn");
#endif
}

wxString BoatProfileStore::DefaultFileName() {
  return JoinPath(DefaultConfigDir(), "boat_profiles.json");
}

wxString BoatProfileStore::NewProfileId() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  static std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<int> dist(0, 0xffffff);
  std::ostringstream id;
  id << "boat-" << millis << "-" << std::hex << dist(rng);
  return id.str();
}

BoatProfile BoatProfileStore::CreateDefaultProfile(const wxString& name) {
  BoatProfile profile;
  profile.id = NewProfileId();
  profile.name = name;
  profile.length_m = 10.0;
  profile.beam_m = 3.2;
  profile.draft_m = 1.5;
  profile.air_draft_m = 12.0;
  profile.vessel_type = "Cruising sailboat";
  profile.displacement_t = 6.0;
  profile.sail_area_m2 = 45.0;
  profile.cruising_speed_kn = 6.0;
  profile.max_speed_kn = 8.0;
  profile.motoring_speed_kn = 5.5;
  profile.engine_consumption_lph = 2.5;
  profile.upwind_twa_deg = 45.0;
  profile.downwind_twa_deg = 150.0;
  profile.min_routing_wind_kn = 3.0;
  profile.max_routing_wind_kn = 40.0;
  profile.current_grid_spacing_deg = 0.05;
  profile.current_duration_hours = 24;
  profile.current_step_hours = 1;
  profile.current_provider = "synthetic";
  return profile;
}

BoatProfile BoatProfileStore::CreateFromCurrentSettings(const wxString& name) {
  BoatProfile profile = CreateDefaultProfile(name);
  if (g_n_ownship_length_meters > 0.0)
    profile.length_m = g_n_ownship_length_meters;
  if (g_n_ownship_beam_meters > 0.0) profile.beam_m = g_n_ownship_beam_meters;
  if (g_defaultBoatSpeed > 0.0) profile.cruising_speed_kn = g_defaultBoatSpeed;
  profile.max_speed_kn =
      std::max(profile.cruising_speed_kn * 1.25,
               profile.cruising_speed_kn + 0.5);
  return profile;
}

BoatProfileValidation BoatProfileStore::Validate(const BoatProfile& profile) {
  BoatProfileValidation result;

  auto add_error = [&](const wxString& error) {
    result.ok = false;
    result.errors.push_back(error);
  };

  if (profile.id.empty()) add_error("Profile id is required.");
  wxString name = profile.name;
  if (name.Trim(false).Trim().empty())
    add_error("Profile name is required.");
  if (!IsPositive(profile.length_m)) add_error("Length must be positive.");
  if (!IsPositive(profile.beam_m)) add_error("Beam must be positive.");
  if (!IsPositive(profile.draft_m)) add_error("Draft must be positive.");
  if (!IsPositive(profile.air_draft_m))
    add_error("Air draft must be positive.");
  if (profile.displacement_t < 0.0)
    add_error("Displacement cannot be negative.");
  if (profile.sail_area_m2 < 0.0)
    add_error("Sail area cannot be negative.");
  if (!IsPositive(profile.cruising_speed_kn))
    add_error("Cruising speed must be positive.");
  if (profile.max_speed_kn < 0.0)
    add_error("Maximum speed cannot be negative.");
  if (profile.max_speed_kn > 0.0 &&
      profile.max_speed_kn < profile.cruising_speed_kn)
    add_error("Maximum speed cannot be less than cruising speed.");
  if (profile.motoring_speed_kn < 0.0)
    add_error("Motoring speed cannot be negative.");
  if (profile.engine_consumption_lph < 0.0)
    add_error("Engine consumption cannot be negative.");
  if (profile.upwind_twa_deg < 0.0 || profile.upwind_twa_deg > 180.0)
    add_error("Upwind true wind angle must be between 0 and 180 degrees.");
  if (profile.downwind_twa_deg < 0.0 || profile.downwind_twa_deg > 180.0)
    add_error("Downwind true wind angle must be between 0 and 180 degrees.");
  if (profile.max_routing_wind_kn < profile.min_routing_wind_kn)
    add_error("Maximum routing wind cannot be less than minimum routing wind.");
  if (profile.min_routing_wind_kn < 0.0)
    add_error("Minimum routing wind cannot be negative.");
  if (!IsPositive(profile.current_grid_spacing_deg))
    add_error("Current grid spacing must be positive.");
  if (profile.current_duration_hours <= 0)
    add_error("Current forecast duration must be positive.");
  if (profile.current_step_hours <= 0)
    add_error("Current forecast step must be positive.");
  if (profile.current_duration_hours > 0 && profile.current_step_hours > 0 &&
      profile.current_duration_hours % profile.current_step_hours != 0)
    add_error("Current forecast duration must be divisible by the step.");
  if (profile.current_provider.empty())
    add_error("Current provider is required.");

  return result;
}

bool BoatProfileStore::Load(wxString* error) {
  m_profiles.clear();
  m_active_profile_id.clear();

  if (!wxFileName::FileExists(m_file_name)) return true;

  wxFile file(m_file_name);
  if (!file.IsOpened()) {
    SetError(error, "Cannot open boat profile file.");
    return false;
  }

  wxString text;
  if (!file.ReadAll(&text)) {
    SetError(error, "Cannot read boat profile file.");
    return false;
  }

  wxJSONValue root;
  wxJSONReader reader;
  if (reader.Parse(text, &root) > 0 || !root.IsObject()) {
    SetError(error, "Cannot parse boat profile file.");
    return false;
  }

  if (root.HasMember("profiles") && root["profiles"].IsArray()) {
    for (int i = 0; i < root["profiles"].Size(); ++i) {
      BoatProfile profile = FromJson(root["profiles"][i]);
      auto validation = Validate(profile);
      if (!validation.ok) {
        SetError(error, "Boat profile file contains an invalid profile.");
        return false;
      }
      m_profiles.push_back(profile);
    }
  }

  m_active_profile_id = ReadString(root, "active_profile_id");
  if (!m_profiles.empty() && !FindProfile(m_active_profile_id))
    m_active_profile_id = m_profiles.front().id;

  return true;
}

bool BoatProfileStore::Save(wxString* error) const {
  wxFileName path(m_file_name);
  if (!path.DirExists() &&
      !path.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
    SetError(error, "Cannot create boat profile directory.");
    return false;
  }

  wxJSONValue root;
  root["schema_version"] = kSchemaVersion;
  root["active_profile_id"] = m_active_profile_id;
  for (const auto& profile : m_profiles) root["profiles"].Append(ToJson(profile));

  wxJSONWriter writer;
  wxString text;
  writer.Write(root, text);

  wxFile file(m_file_name, wxFile::write);
  if (!file.IsOpened()) {
    SetError(error, "Cannot write boat profile file.");
    return false;
  }

  if (!file.Write(text)) {
    SetError(error, "Cannot write boat profile data.");
    return false;
  }

  return true;
}

const BoatProfile* BoatProfileStore::GetActiveProfile() const {
  return FindProfile(m_active_profile_id);
}

const BoatProfile* BoatProfileStore::FindProfile(const wxString& id) const {
  auto it = std::find_if(m_profiles.begin(), m_profiles.end(),
                         [&](const BoatProfile& p) { return SameProfileId(p, id); });
  return it == m_profiles.end() ? nullptr : &*it;
}

bool BoatProfileStore::SetProfiles(const std::vector<BoatProfile>& profiles,
                                   const wxString& active_profile_id,
                                   wxString* error) {
  for (const auto& profile : profiles) {
    auto validation = Validate(profile);
    if (!validation.ok) {
      SetError(error, validation.errors.front());
      return false;
    }
  }

  for (size_t i = 0; i < profiles.size(); ++i) {
    for (size_t j = i + 1; j < profiles.size(); ++j) {
      if (profiles[i].id == profiles[j].id) {
        SetError(error, "Boat profile ids must be unique.");
        return false;
      }
    }
  }

  if (!profiles.empty() &&
      std::none_of(profiles.begin(), profiles.end(),
                   [&](const BoatProfile& p) { return p.id == active_profile_id; })) {
    SetError(error, "Active boat profile does not exist.");
    return false;
  }

  m_profiles = profiles;
  m_active_profile_id = active_profile_id;
  return true;
}

bool BoatProfileStore::AddProfile(const BoatProfile& profile, wxString* error) {
  auto validation = Validate(profile);
  if (!validation.ok) {
    SetError(error, validation.errors.front());
    return false;
  }
  if (FindProfile(profile.id)) {
    SetError(error, "Boat profile id already exists.");
    return false;
  }
  m_profiles.push_back(profile);
  if (m_active_profile_id.empty()) m_active_profile_id = profile.id;
  return true;
}

bool BoatProfileStore::UpdateProfile(const BoatProfile& profile,
                                     wxString* error) {
  auto validation = Validate(profile);
  if (!validation.ok) {
    SetError(error, validation.errors.front());
    return false;
  }
  auto it = std::find_if(m_profiles.begin(), m_profiles.end(),
                         [&](const BoatProfile& p) {
                           return SameProfileId(p, profile.id);
                         });
  if (it == m_profiles.end()) {
    SetError(error, "Boat profile does not exist.");
    return false;
  }
  *it = profile;
  return true;
}

bool BoatProfileStore::DeleteProfile(const wxString& id, wxString* error) {
  if (m_profiles.size() <= 1) {
    SetError(error, "At least one boat profile is required.");
    return false;
  }
  auto it = std::remove_if(m_profiles.begin(), m_profiles.end(),
                           [&](const BoatProfile& p) {
                             return SameProfileId(p, id);
                           });
  if (it == m_profiles.end()) {
    SetError(error, "Boat profile does not exist.");
    return false;
  }
  const bool deleted_active = m_active_profile_id == id;
  m_profiles.erase(it, m_profiles.end());
  if (deleted_active) m_active_profile_id = m_profiles.front().id;
  return true;
}

bool BoatProfileStore::SetActiveProfile(const wxString& id, wxString* error) {
  if (!FindProfile(id)) {
    SetError(error, "Boat profile does not exist.");
    return false;
  }
  m_active_profile_id = id;
  return true;
}
