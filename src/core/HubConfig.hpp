/*
StreamMulticast — Broadcast Hub persistent state
Copyright (C) 2026 Avanatro <contact@avanatro.com>
GPLv2 — see LICENSE for full text.
*/

#pragma once

#include <string>

namespace smulti {

enum class BroadcastPrivacy : int {
	Public = 0,
	Unlisted = 1,
	Private = 2,
};

struct BroadcastPlan {
	std::string title;
	std::string description;
	std::string scheduled_start_utc;
	BroadcastPrivacy privacy = BroadcastPrivacy::Public;
};

struct HubState {
	BroadcastPlan plan;

	std::string meta_app_id;
	std::string meta_client_token;
	std::string facebook_user_token;
	std::string facebook_page_id;
	std::string facebook_page_name;
	std::string facebook_page_token;
	std::string facebook_live_video_id;
};

class HubConfig {
public:
	HubConfig();

	bool load();
	bool save(const HubState &state);
	const HubState &state() const { return m_state; }
	HubState &state() { return m_state; }

private:
	std::string resolve_path() const;
	std::string m_path;
	HubState m_state;
};

} // namespace smulti
