/*
StreamMulticast — OBS Multi-RTMP Plugin with Per-Output Re-Encode
Copyright (C) 2026 Avanatro <contact@avanatro.com>

GPLv2 — see LICENSE for full text.
*/

#include "Endpoint.hpp"

#include <obs-data.h>

#include <cstdlib>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace smulti {

std::string Endpoint::generate_uuid()
{
	static bool seeded = false;
	if (!seeded) {
		srand(static_cast<unsigned>(time(nullptr)));
		seeded = true;
	}

	auto rand_hex = [](int bits) -> unsigned int {
		if (bits >= 32)
			return static_cast<unsigned int>(rand());
		return static_cast<unsigned int>(rand()) & ((1u << bits) - 1u);
	};

	std::ostringstream ss;
	ss << std::hex << std::setfill('0');
	ss << std::setw(8) << rand_hex(32) << '-';
	ss << std::setw(4) << rand_hex(16) << '-';
	ss << std::setw(4) << (0x4000u | rand_hex(12)) << '-';
	ss << std::setw(4) << (0x8000u | rand_hex(14)) << '-';
	ss << std::setw(12) << ((static_cast<uint64_t>(rand_hex(32)) << 16) | rand_hex(16));
	return ss.str();
}

Endpoint Endpoint::make_default(const std::string &name)
{
	Endpoint ep;
	ep.id = generate_uuid();
	ep.name = name;
	ep.server_url = "";
	ep.stream_key = "";
	ep.video_settings_mode = EncoderSettingsMode::Custom;
	ep.video_codec = VideoCodec::H264;
	ep.encoder_backend = EncoderBackend::X264;
	ep.video_bitrate_kbps = 6000;
	ep.keyframe_interval_sec = 2;
	ep.audio_settings_mode = EncoderSettingsMode::Custom;
	ep.audio_bitrate_kbps = 160;
	ep.orientation = OutputOrientation::SourceMatch;
	ep.enabled = true;
	ep.linked_to_main = true;
	ep.sort_order = 0;
	return ep;
}

obs_data_t *Endpoint::serialize() const
{
	obs_data_t *data = obs_data_create();

	obs_data_set_string(data, "id", id.c_str());
	obs_data_set_string(data, "name", name.c_str());
	obs_data_set_string(data, "server_url", server_url.c_str());
	obs_data_set_string(data, "stream_key", stream_key.c_str());

	obs_data_set_int(data, "video_settings_mode", static_cast<long long>(video_settings_mode));
	obs_data_set_int(data, "video_codec", static_cast<long long>(video_codec));
	obs_data_set_int(data, "encoder_backend", static_cast<long long>(encoder_backend));
	obs_data_set_int(data, "video_bitrate", video_bitrate_kbps);
	obs_data_set_int(data, "keyframe_interval", keyframe_interval_sec);

	obs_data_set_int(data, "audio_settings_mode", static_cast<long long>(audio_settings_mode));
	obs_data_set_int(data, "audio_bitrate", audio_bitrate_kbps);

	obs_data_set_int(data, "orientation", static_cast<long long>(orientation));
	obs_data_set_bool(data, "enabled", enabled);
	obs_data_set_bool(data, "linked_to_main", linked_to_main);
	obs_data_set_int(data, "sort_order", sort_order);

	return data;
}

Endpoint Endpoint::deserialize(obs_data_t *data)
{
	if (!data)
		return Endpoint{};

	Endpoint ep;
	ep.id = obs_data_get_string(data, "id");
	ep.name = obs_data_get_string(data, "name");
	ep.server_url = obs_data_get_string(data, "server_url");
	ep.stream_key = obs_data_get_string(data, "stream_key");

	/* Missing v2 keys deliberately preserve the old endpoint behaviour. */
	ep.video_settings_mode = obs_data_has_user_value(data, "video_settings_mode")
		? static_cast<EncoderSettingsMode>(static_cast<int>(obs_data_get_int(data, "video_settings_mode")))
		: EncoderSettingsMode::Custom;
	ep.video_codec = obs_data_has_user_value(data, "video_codec")
		? static_cast<VideoCodec>(static_cast<int>(obs_data_get_int(data, "video_codec")))
		: VideoCodec::H264;

	ep.encoder_backend = static_cast<EncoderBackend>(
		static_cast<int>(obs_data_get_int(data, "encoder_backend")));
	ep.video_bitrate_kbps = static_cast<int>(obs_data_get_int(data, "video_bitrate"));
	ep.keyframe_interval_sec = static_cast<int>(obs_data_get_int(data, "keyframe_interval"));

	ep.audio_settings_mode = obs_data_has_user_value(data, "audio_settings_mode")
		? static_cast<EncoderSettingsMode>(static_cast<int>(obs_data_get_int(data, "audio_settings_mode")))
		: EncoderSettingsMode::Custom;
	ep.audio_bitrate_kbps = static_cast<int>(obs_data_get_int(data, "audio_bitrate"));

	int orientation = static_cast<int>(obs_data_get_int(data, "orientation"));
	if (orientation < static_cast<int>(OutputOrientation::SourceMatch) ||
	    orientation > static_cast<int>(OutputOrientation::Vertical1080x1920Rotated))
		orientation = static_cast<int>(OutputOrientation::SourceMatch);
	ep.orientation = static_cast<OutputOrientation>(orientation);

	ep.enabled = obs_data_get_bool(data, "enabled");
	ep.linked_to_main = obs_data_get_bool(data, "linked_to_main");
	ep.sort_order = static_cast<int>(obs_data_get_int(data, "sort_order"));

	if (ep.video_bitrate_kbps < 500) ep.video_bitrate_kbps = 500;
	if (ep.video_bitrate_kbps > 50000) ep.video_bitrate_kbps = 50000;
	if (ep.keyframe_interval_sec <= 0) ep.keyframe_interval_sec = 2;
	if (ep.audio_bitrate_kbps <= 0) ep.audio_bitrate_kbps = 160;

	return ep;
}

} // namespace smulti
