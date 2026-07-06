#ifndef MODEL_BOAT_PROFILE_SERVICE_H
#define MODEL_BOAT_PROFILE_SERVICE_H

#include <functional>
#include <vector>

#include <wx/string.h>

#include "model/boat_profile.h"

class BoatProfileService {
public:
  using Listener = std::function<void(const BoatProfile*)>;

  static BoatProfileService& Get();

  bool Load(wxString* error = nullptr);
  bool Save(wxString* error = nullptr) const;
  bool EnsureActiveProfile(const wxString& fallback_name,
                           wxString* error = nullptr);

  const std::vector<BoatProfile>& GetProfiles();
  const BoatProfile* GetActiveProfile();
  wxString GetActiveProfileId();

  bool AddProfile(const BoatProfile& profile, wxString* error = nullptr);
  bool UpdateProfile(const BoatProfile& profile, wxString* error = nullptr);
  bool DeleteProfile(const wxString& id, wxString* error = nullptr);
  bool SetProfiles(const std::vector<BoatProfile>& profiles,
                   const wxString& active_profile_id,
                   wxString* error = nullptr);
  bool SetActiveProfile(const wxString& id, wxString* error = nullptr);

  int AddListener(Listener listener);
  void RemoveListener(int id);
  void NotifyActiveProfileChanged();

private:
  BoatProfileService();

  BoatProfileStore m_store;
  bool m_loaded = false;
  int m_next_listener_id = 1;
  std::vector<std::pair<int, Listener>> m_listeners;
};

#endif
