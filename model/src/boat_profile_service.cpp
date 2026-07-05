#include "model/boat_profile_service.h"

#include <algorithm>

BoatProfileService& BoatProfileService::Get() {
  static BoatProfileService service;
  return service;
}

BoatProfileService::BoatProfileService() = default;

bool BoatProfileService::Load(wxString* error) {
  if (!m_store.Load(error)) return false;
  m_loaded = true;
  return true;
}

bool BoatProfileService::Save(wxString* error) const { return m_store.Save(error); }

bool BoatProfileService::EnsureActiveProfile(const wxString& fallback_name,
                                             wxString* error) {
  if (!m_loaded && !Load(error)) return false;

  if (m_store.GetActiveProfile()) return true;

  BoatProfile profile = BoatProfileStore::CreateFromCurrentSettings(fallback_name);
  if (!m_store.AddProfile(profile, error)) return false;
  if (!m_store.SetActiveProfile(profile.id, error)) return false;
  if (!m_store.Save(error)) return false;
  NotifyActiveProfileChanged();
  return true;
}

const std::vector<BoatProfile>& BoatProfileService::GetProfiles() {
  if (!m_loaded) Load();
  return m_store.GetProfiles();
}

const BoatProfile* BoatProfileService::GetActiveProfile() {
  if (!m_loaded) Load();
  return m_store.GetActiveProfile();
}

wxString BoatProfileService::GetActiveProfileId() {
  if (!m_loaded) Load();
  return m_store.GetActiveProfileId();
}

bool BoatProfileService::AddProfile(const BoatProfile& profile, wxString* error) {
  if (!m_loaded && !Load(error)) return false;
  if (!m_store.AddProfile(profile, error)) return false;
  if (!m_store.Save(error)) return false;
  NotifyActiveProfileChanged();
  return true;
}

bool BoatProfileService::UpdateProfile(const BoatProfile& profile,
                                       wxString* error) {
  if (!m_loaded && !Load(error)) return false;
  if (!m_store.UpdateProfile(profile, error)) return false;
  if (!m_store.Save(error)) return false;
  NotifyActiveProfileChanged();
  return true;
}

bool BoatProfileService::SetActiveProfile(const wxString& id, wxString* error) {
  if (!m_loaded && !Load(error)) return false;
  if (!m_store.SetActiveProfile(id, error)) return false;
  if (!m_store.Save(error)) return false;
  NotifyActiveProfileChanged();
  return true;
}

int BoatProfileService::AddListener(Listener listener) {
  const int id = m_next_listener_id++;
  m_listeners.emplace_back(id, std::move(listener));
  return id;
}

void BoatProfileService::RemoveListener(int id) {
  auto it = std::remove_if(m_listeners.begin(), m_listeners.end(),
                           [&](const auto& item) { return item.first == id; });
  m_listeners.erase(it, m_listeners.end());
}

void BoatProfileService::NotifyActiveProfileChanged() {
  const BoatProfile* active = GetActiveProfile();
  for (const auto& item : m_listeners) item.second(active);
}
