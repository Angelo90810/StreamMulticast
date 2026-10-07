/*
StreamMulticast — Broadcast Hub persistent state
Copyright (C) 2026 Avanatro <contact@avanatro.com>
GPLv2 — see LICENSE for full text.
*/

#include "HubConfig.hpp"
#include "SecretStore.hpp"
#include "../plugin-support.h"

#include <obs-module.h>
#include <obs-data.h>
#include <util/base.h>
#include <util/platform.h>

namespace smulti {

namespace {

void set_protected(obs_data_t *root, const char *name, const std::string &value)
{
	if (value.empty())
		return;

	std::string protected_value;
	if (protect_secret(value, protected_value))
		obs_data_set_string(root, name, protected_value.c_str());
	else
		obs_data_set_string(root, name, value.c_str());
}

std::string get_protected(obs_data_t *root, const char *name)
{
	const char *raw = obs_data_get_string(root, name);
	if (!raw || !*raw)
		return {};

	std::string value(raw);
	if (value.rfind("dpapi:", 0) != 0)
		return value;

	std::string clear;
	if (!unprotect_secret(value, clear)) {
		obs_log(LOG_WARNING, "HubConfig: could not decrypt %s for this Windows user", name);
		return {};
	}
	return clear;
}

} // namespace

HubConfig::HubConfig()
	: m_path(resolve_path())
{
}

std::string HubConfig::resolve_path() const
{
	char *raw = obs_module_get_config_path(obs_current_module(), "hub.json");
	if (!raw)
		return {};

	std::string path(raw);
	bfree(raw);

	const size_t slash = path.find_last_of("/\\");
	if (slash != std::string::npos) {
		const std::string dir = path.substr(0, slash);
		if (!dir.empty())
			os_mkdirs(dir.c_str());
	}
	return path;
}

bool HubConfig::load()
{
	if (m_path.empty() || !os_file_exists(m_path.c_str()))
		return false;

	obs_data_t *root = obs_data_create_from_json_file_safe(m_path.c_str(), "bak");
	if (!root)
		return false;

	m_state.plan.title = obs_data_get_string(root, "title");
	m_state.plan.description = obs_data_get_string(root, "description");
	m_state.plan.scheduled_start_utc = obs_data_get_string(root, "scheduled_start_utc");

	int privacy = static_cast<int>(obs_data_get_int(root, "privacy"));
	if (privacy < static_cast<int>(BroadcastPrivacy::Public) ||
	    privacy > static_cast<int>(BroadcastPrivacy::Private))
		privacy = static_cast<int>(BroadcastPrivacy::Public);
	m_state.plan.privacy = static_cast<BroadcastPrivacy>(privacy);

	m_state.meta_app_id = obs_data_get_string(root, "meta_app_id");
	m_state.meta_client_token = get_protected(root, "meta_client_token");
	m_state.facebook_user_token = get_protected(root, "facebook_user_token");
	m_state.facebook_page_id = obs_data_get_string(root, "facebook_page_id");
	m_state.facebook_page_name = obs_data_get_string(root, "facebook_page_name");
	m_state.facebook_page_token = get_protected(root, "facebook_page_token");
	m_state.facebook_live_video_id = obs_data_get_string(root, "facebook_live_video_id");

	obs_data_release(root);
	return true;
}

bool HubConfig::save(const HubState &state)
{
	m_state = state;
	if (m_path.empty())
		return false;

	obs_data_t *root = obs_data_create();
	obs_data_set_int(root, "schema_version", 1);
	obs_data_set_string(root, "title", state.plan.title.c_str());
	obs_data_set_string(root, "description", state.plan.description.c_str());
	obs_data_set_string(root, "scheduled_start_utc", state.plan.scheduled_start_utc.c_str());
	obs_data_set_int(root, "privacy", static_cast<long long>(state.plan.privacy));

	obs_data_set_string(root, "meta_app_id", state.meta_app_id.c_str());
	set_protected(root, "meta_client_token", state.meta_client_token);
	set_protected(root, "facebook_user_token", state.facebook_user_token);
	obs_data_set_string(root, "facebook_page_id", state.facebook_page_id.c_str());
	obs_data_set_string(root, "facebook_page_name", state.facebook_page_name.c_str());
	set_protected(root, "facebook_page_token", state.facebook_page_token);
	obs_data_set_string(root, "facebook_live_video_id", state.facebook_live_video_id.c_str());

	const bool ok = obs_data_save_json_safe(root, m_path.c_str(), "tmp", "bak");
	obs_data_release(root);
	return ok;
}

} // namespace smulti
