#ifndef MODEL_BOAT_PROFILE_H
#define MODEL_BOAT_PROFILE_H

#include <vector>

#include <wx/string.h>

struct BoatProfile {
  wxString id;
  wxString name;
  double length_m = 0.0;
  double beam_m = 0.0;
  double draft_m = 0.0;
  double air_draft_m = 0.0;
  wxString vessel_type = "Cruising sailboat";
  double displacement_t = 0.0;
  double sail_area_m2 = 0.0;
  double cruising_speed_kn = 0.0;
  double max_speed_kn = 0.0;
  double motoring_speed_kn = 0.0;
  double engine_consumption_lph = 0.0;
  wxString polar_file;
  double upwind_twa_deg = 45.0;
  double downwind_twa_deg = 150.0;
  double min_routing_wind_kn = 0.0;
  double max_routing_wind_kn = 40.0;
  wxString weather_provider = "saildocs_gfs";
  wxString weather_model = "GFS";
  wxString weather_grib_directory;
  int weather_forecast_hours = 72;
  int weather_step_hours = 3;
  double weather_grid_spacing_deg = 0.25;
  bool weather_include_gusts = true;
  bool weather_include_waves = false;
  double current_grid_spacing_deg = 0.05;
  int current_duration_hours = 24;
  int current_step_hours = 1;
  wxString current_provider = "synthetic";
  wxString data_directory;
  wxString notes;
};

struct BoatProfileValidation {
  bool ok = true;
  std::vector<wxString> errors;
};

class BoatProfileStore {
public:
  explicit BoatProfileStore(const wxString& file_name = DefaultFileName());

  static wxString DefaultConfigDir();
  static wxString DefaultFileName();
  static wxString NewProfileId();
  static BoatProfile CreateDefaultProfile(const wxString& name);
  static BoatProfile CreateFromCurrentSettings(const wxString& name);
  static BoatProfileValidation Validate(const BoatProfile& profile);

  bool Load(wxString* error = nullptr);
  bool Save(wxString* error = nullptr) const;

  const std::vector<BoatProfile>& GetProfiles() const { return m_profiles; }
  const BoatProfile* GetActiveProfile() const;
  const BoatProfile* FindProfile(const wxString& id) const;
  wxString GetActiveProfileId() const { return m_active_profile_id; }
  wxString GetFileName() const { return m_file_name; }

  bool SetProfiles(const std::vector<BoatProfile>& profiles,
                   const wxString& active_profile_id,
                   wxString* error = nullptr);
  bool AddProfile(const BoatProfile& profile, wxString* error = nullptr);
  bool UpdateProfile(const BoatProfile& profile, wxString* error = nullptr);
  bool DeleteProfile(const wxString& id, wxString* error = nullptr);
  bool SetActiveProfile(const wxString& id, wxString* error = nullptr);

private:
  std::vector<BoatProfile> m_profiles;
  wxString m_active_profile_id;
  wxString m_file_name;
};

#endif
