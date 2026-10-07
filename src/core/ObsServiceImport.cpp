/*
StreamMulticast — OBS Multi-RTMP Plugin with Per-Output Re-Encode
Copyright (C) 2026 Avanatro <contact@avanatro.com>

GPLv2 — see LICENSE for full text.
*/

#include "ObsServiceImport.hpp"
#include "../plugin-support.h"

#include <obs.h>
#include <obs-data.h>
#include <util/platform.h>
#include <util/base.h>
#include <obs-frontend-api.h>

#include <string>

namespace smulti {

/* -----------------------------------------------------------------------
 * Known service → RTMP URL mapping
 *
 * OBS's rtmp-services plugin ships a full services.json (~50 platforms),
 * but for v1.0.6 we hardcode the major ones to avoid having to parse
 * OBS's data folder.  Unknown services fall through with an error.
 *
 * Servers are taken from OBS's services.json (rtmp_common service type)
 * as of OBS 31.x.
 * ----------------------------------------------------------------------- */
static std::string resolve_service_server(const std::string &service_name)
{
	if (service_name == "Twitch")
		return "rtmp://live.twitch.tv/app";
	if (service_name == "YouTube - RTMPS")
		return "rtmps://a.rtmps.youtube.com/live2";
	if (service_name == "Facebook Live")
		return "rtmps://rtmp-api.facebook.com:443/rtmp/";
	if (service_name == "Kick")
		return "rtmps://fa723fc1b171.global-contribute.live-video.net/app";
	if (service_name == "Trovo")
		return "rtmp://livepush.trovo.live/push";
	if (service_name == "Restream.io - RTMP")
		return "rtmp://live.restream.io/live";
	return {};
}

static bool try_import_live_obs_service(ObsServiceConfig &cfg)
{
	obs_output_t *output = obs_frontend_get_streaming_output();
	if (!output)
		return false;

	obs_service_t *service = obs_output_get_service(output);
	if (!service) {
		obs_output_release(output);
		return false;
	}

	/* StreamMulticast currently creates rtmp_output endpoints only. Do not
	 * silently convert WHIP/HLS/SRT/etc. OBS profiles into a bogus RTMP
	 * endpoint just because they also expose a URL/key-like pair. */
	const char *protocol_raw = obs_service_get_protocol(service);
	const std::string protocol = protocol_raw ? protocol_raw : "";
	if (!protocol.empty() && protocol != "RTMP" && protocol != "RTMPS") {
		cfg.error_message = "Active OBS service uses unsupported protocol '" +
		                    protocol + "'; StreamMulticast endpoints require RTMP/RTMPS";
		obs_output_release(output);
		return false;
	}

	const char *server = obs_service_get_connect_info(
		service, OBS_SERVICE_CONNECT_INFO_SERVER_URL);
	const char *key = obs_service_get_connect_info(
		service, OBS_SERVICE_CONNECT_INFO_STREAM_KEY);

	obs_data_t *settings = obs_service_get_settings(service);
	const char *service_setting = settings
		? obs_data_get_string(settings, "service") : nullptr;
	const char *service_name = obs_service_get_name(service);

	cfg.server_url = server ? server : "";
	cfg.stream_key = key ? key : "";
	cfg.service_name =
		(service_setting && *service_setting) ? service_setting
		: (service_name ? service_name : "");

	if (settings)
		obs_data_release(settings);
	obs_output_release(output);

	if (cfg.server_url.empty() || cfg.stream_key.empty())
		return false;

	cfg.ok = true;
	obs_log(LOG_INFO,
	        "ObsServiceImport: imported active OBS service '%s' (server=%s)",
	        cfg.service_name.c_str(), cfg.server_url.c_str());
	return true;
}

/* -----------------------------------------------------------------------
 * import_from_active_obs_profile
 * ----------------------------------------------------------------------- */
ObsServiceConfig import_from_active_obs_profile()
{
	ObsServiceConfig cfg;

	/* Prefer OBS's live service object. It already resolves service-specific
	 * "auto" servers and avoids duplicating/staling the services.json table.
	 * Keep the profile-file path as a compatibility fallback for outputs that
	 * have not yet materialized their service object. */
	if (try_import_live_obs_service(cfg))
		return cfg;
	if (!cfg.error_message.empty())
		return cfg;

	/* Ask OBS for the exact active profile directory.  Profile display
	 * names and directory names can differ (ProfileDir), so reconstructing
	 * this path from user.ini/global.ini is not reliable. */
	char *profile_path_raw = obs_frontend_get_current_profile_path();
	if (!profile_path_raw || !*profile_path_raw) {
		if (profile_path_raw)
			bfree(profile_path_raw);
		cfg.error_message = "Cannot resolve the active OBS profile directory";
		return cfg;
	}
	std::string profile_path(profile_path_raw);
	bfree(profile_path_raw);

	obs_log(LOG_INFO, "ObsServiceImport: active profile path resolved");

	/* Load service.json from the exact active profile directory. */
	std::string serviceJson = profile_path + "/service.json";
	obs_data_t *data = obs_data_create_from_json_file_safe(serviceJson.c_str(), "bak");
	if (!data) {
		cfg.error_message = "service.json not found or invalid in the active OBS profile";
		return cfg;
	}

	/* 4. Extract service settings */
	obs_data_t *settings = obs_data_get_obj(data, "settings");
	if (settings) {
		const char *svc    = obs_data_get_string(settings, "service");
		const char *server = obs_data_get_string(settings, "server");
		const char *key    = obs_data_get_string(settings, "key");

		cfg.service_name = svc    ? svc    : "";
		cfg.server_url   = server ? server : "";
		cfg.stream_key   = key    ? key    : "";

		obs_data_release(settings);
	}
	obs_data_release(data);

	/* HLS cannot be represented by this plugin's RTMP output. Never
	 * substitute an RTMP URL for an HLS profile and pretend settings were
	 * inherited faithfully. */
	if (cfg.service_name == "YouTube - HLS") {
		cfg.error_message = "The active OBS profile uses YouTube HLS; switch OBS to an RTMP/RTMPS service or enter an RTMP endpoint manually";
		return cfg;
	}

	/* 5. Resolve "auto" / empty server via service-name → RTMP-URL map */
	if (cfg.server_url == "auto" || cfg.server_url.empty()) {
		std::string resolved = resolve_service_server(cfg.service_name);
		if (!resolved.empty()) {
			cfg.server_url = resolved;
		}
	}

	/* 6. Validate */
	if (cfg.stream_key.empty()) {
		cfg.error_message = "No stream key in the active OBS profile "
		                    "(have you connected an account in OBS?)";
		return cfg;
	}
	if (cfg.server_url.empty()) {
		cfg.error_message = "Unknown service '" + cfg.service_name +
		                    "' — please enter the server URL manually";
		return cfg;
	}

	cfg.ok = true;
	obs_log(LOG_INFO,
	        "ObsServiceImport: imported '%s' from active OBS profile (server=%s)",
	        cfg.service_name.c_str(), cfg.server_url.c_str());
	return cfg;
}

} // namespace smulti
