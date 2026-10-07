/*
StreamMulticast — OBS Multi-RTMP Plugin with Per-Output Re-Encode
Copyright (C) 2026 Avanatro <contact@avanatro.com>

GPLv2 — see LICENSE for full text.
*/

#include "TikTokBridgeImport.hpp"
#include "SecretStore.hpp"
#include "../plugin-support.h"

#include <obs-data.h>
#include <util/platform.h>
#include <util/base.h>
#include <util/bmem.h>
#include <QtCore/QDateTime>
#include <QtCore/QString>

namespace smulti {

static std::string read_string_alias(obs_data_t *data,
                                     const char *primary,
                                     const char *fallback)
{
	const char *value = obs_data_get_string(data, primary);
	if (value && *value)
		return value;

	value = obs_data_get_string(data, fallback);
	return value ? value : "";
}

std::string default_tiktok_bridge_path()
{
	char *base = os_get_config_path_ptr("obs-studio");
	if (!base)
		return {};

	std::string path(base);
	bfree(base);
	return path + "/plugin_config/streammulticast/tiktok_bridge.json";
}

TikTokBridgeConfig import_tiktok_bridge_file(const std::string &path)
{
	TikTokBridgeConfig cfg;
	cfg.source_path = path;

	if (path.empty()) {
		cfg.error_message = "No TikTok Bridge JSON path provided";
		return cfg;
	}

	obs_data_t *data = obs_data_create_from_json_file(path.c_str());
	if (!data) {
		cfg.error_message = "TikTok Bridge JSON not found or invalid: " + path;
		return cfg;
	}

	const char *name = obs_data_get_string(data, "name");
	const char *expires_at = obs_data_get_string(data, "expires_at");

	cfg.name = name ? name : "";
	cfg.expires_at = expires_at ? expires_at : "";
	cfg.server_url = read_string_alias(data, "server_url", "server");

	const char *protected_key = obs_data_get_string(data, "stream_key_protected");
	if (protected_key && *protected_key) {
		if (!unprotect_secret(protected_key, cfg.stream_key)) {
			obs_data_release(data);
			cfg.error_message =
				"TikTok Bridge stream key is protected for another Windows user or is corrupted";
			return cfg;
		}
	} else {
		/* Legacy / third-party helper compatibility. */
		cfg.stream_key = read_string_alias(data, "stream_key", "key");
	}

	obs_data_release(data);

	if (cfg.server_url.empty()) {
		cfg.error_message = "TikTok Bridge JSON is missing server_url/server";
		return cfg;
	}
	if (cfg.server_url.rfind("rtmp://", 0) != 0 &&
	    cfg.server_url.rfind("rtmps://", 0) != 0) {
		cfg.error_message = "TikTok Bridge server URL must use rtmp:// or rtmps://";
		return cfg;
	}
	if (cfg.stream_key.empty()) {
		cfg.error_message = "TikTok Bridge JSON is missing stream_key/key";
		return cfg;
	}

	if (!cfg.expires_at.empty()) {
		const QString raw = QString::fromStdString(cfg.expires_at);
		QDateTime expiry = QDateTime::fromString(raw, Qt::ISODateWithMs);
		if (!expiry.isValid())
			expiry = QDateTime::fromString(raw, Qt::ISODate);
		if (!expiry.isValid()) {
			cfg.error_message = "TikTok Bridge expires_at is not a valid ISO-8601 timestamp";
			return cfg;
		}
		if (expiry.toUTC() <= QDateTime::currentDateTimeUtc()) {
			cfg.error_message = "TikTok Bridge credentials have expired; refresh the bridge data before importing";
			return cfg;
		}
	}

	cfg.ok = true;
	obs_log(LOG_INFO,
	        "TikTokBridgeImport: imported RTMP data from %s (server=%s)",
	        cfg.source_path.c_str(), cfg.server_url.c_str());
	return cfg;
}

} // namespace smulti
