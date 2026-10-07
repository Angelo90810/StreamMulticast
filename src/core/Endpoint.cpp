/*
StreamMulticast — OBS Multi-RTMP Plugin with Per-Output Re-Encode
Copyright (C) 2026 Avanatro <contact@avanatro.com>

GPLv2 — see LICENSE for full text.
*/

#include "Endpoint.hpp"
#include "SecretStore.hpp"

#include <obs-data.h>

#include <random>
#include <array>
#include <sstream>
#include <iomanip>

namespace smulti {

std::string Endpoint::generate_uuid()
{
	/* MSVC rand() only provides 15 useful bits.  Use the standard random
	 * facility so endpoint identity remains collision-resistant even across
	 * rapid add/import operations and multiple OBS processes. */
	static thread_local std::mt19937_64 rng([] {
		std::random_device rd;
		std::seed_seq seed{
			rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd()
		};
		return std::mt19937_64(seed);
	}());

	std::uniform_int_distribution<unsigned int> byte_dist(0, 255);
	std::array<unsigned char, 16> bytes{};
	for (auto &b : bytes)
		b = static_cast<unsigned char>(byte_dist(rng));

	/* RFC 4122 variant + version 4 bits. */
	bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);
	bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);

	std::ostringstream ss;
	ss << std::hex << std::setfill('0');
	for (size_t i = 0; i < bytes.size(); ++i) {
		if (i == 4 || i == 6 || i == 8 || i == 10)
			ss << '-';
		ss << std::setw(2) << static_cast<unsigned int>(bytes[i]);
	}
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

	std::string protected_key;
	if (protect_secret(stream_key, protected_key)) {
		obs_data_set_string(data, "stream_key_protected", protected_key.c_str());
	} else {
		/* Non-Windows compatibility path until native keychain backends are
		 * implemented. Existing configs remain readable everywhere. */
		obs_data_set_string(data, "stream_key", stream_key.c_str());
	}

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

	if (obs_data_has_user_value(data, "stream_key_protected")) {
		const char *protected_raw = obs_data_get_string(data, "stream_key_protected");
		std::string decrypted;
		if (protected_raw && unprotect_secret(protected_raw, decrypted))
			ep.stream_key = std::move(decrypted);
		else
			ep.stream_key.clear();
	} else {
		/* v1/v2 compatibility: plaintext keys migrate to protected storage
		 * automatically on the next successful save on Windows. */
		ep.stream_key = obs_data_get_string(data, "stream_key");
	}

	/* Missing v2 keys deliberately preserve the old endpoint behaviour. */
	int video_mode = obs_data_has_user_value(data, "video_settings_mode")
		? static_cast<int>(obs_data_get_int(data, "video_settings_mode"))
		: static_cast<int>(EncoderSettingsMode::Custom);
	if (video_mode < static_cast<int>(EncoderSettingsMode::Custom) ||
	    video_mode > static_cast<int>(EncoderSettingsMode::UseOBS))
		video_mode = static_cast<int>(EncoderSettingsMode::Custom);
	ep.video_settings_mode = static_cast<EncoderSettingsMode>(video_mode);

	int codec = obs_data_has_user_value(data, "video_codec")
		? static_cast<int>(obs_data_get_int(data, "video_codec"))
		: static_cast<int>(VideoCodec::H264);
	if (codec < static_cast<int>(VideoCodec::H264) ||
	    codec > static_cast<int>(VideoCodec::HEVC))
		codec = static_cast<int>(VideoCodec::H264);
	ep.video_codec = static_cast<VideoCodec>(codec);

	int backend = obs_data_has_user_value(data, "encoder_backend")
		? static_cast<int>(obs_data_get_int(data, "encoder_backend"))
		: static_cast<int>(EncoderBackend::X264);
	if (backend < static_cast<int>(EncoderBackend::X264) ||
	    backend > static_cast<int>(EncoderBackend::AMF))
		backend = static_cast<int>(EncoderBackend::X264);
	ep.encoder_backend = static_cast<EncoderBackend>(backend);
	ep.video_bitrate_kbps = obs_data_has_user_value(data, "video_bitrate")
		? static_cast<int>(obs_data_get_int(data, "video_bitrate")) : 6000;
	ep.keyframe_interval_sec = obs_data_has_user_value(data, "keyframe_interval")
		? static_cast<int>(obs_data_get_int(data, "keyframe_interval")) : 2;

	int audio_mode = obs_data_has_user_value(data, "audio_settings_mode")
		? static_cast<int>(obs_data_get_int(data, "audio_settings_mode"))
		: static_cast<int>(EncoderSettingsMode::Custom);
	if (audio_mode < static_cast<int>(EncoderSettingsMode::Custom) ||
	    audio_mode > static_cast<int>(EncoderSettingsMode::UseOBS))
		audio_mode = static_cast<int>(EncoderSettingsMode::Custom);
	ep.audio_settings_mode = static_cast<EncoderSettingsMode>(audio_mode);
	ep.audio_bitrate_kbps = obs_data_has_user_value(data, "audio_bitrate")
		? static_cast<int>(obs_data_get_int(data, "audio_bitrate")) : 160;

	int orientation = obs_data_has_user_value(data, "orientation")
		? static_cast<int>(obs_data_get_int(data, "orientation"))
		: static_cast<int>(OutputOrientation::SourceMatch);
	if (orientation < static_cast<int>(OutputOrientation::SourceMatch) ||
	    orientation > static_cast<int>(OutputOrientation::Vertical1080x1920Rotated))
		orientation = static_cast<int>(OutputOrientation::SourceMatch);
	ep.orientation = static_cast<OutputOrientation>(orientation);

	ep.enabled = obs_data_has_user_value(data, "enabled")
		? obs_data_get_bool(data, "enabled") : true;
	ep.linked_to_main = obs_data_has_user_value(data, "linked_to_main")
		? obs_data_get_bool(data, "linked_to_main") : true;
	ep.sort_order = obs_data_has_user_value(data, "sort_order")
		? static_cast<int>(obs_data_get_int(data, "sort_order")) : 0;

	if (ep.video_bitrate_kbps < 500) ep.video_bitrate_kbps = 500;
	if (ep.video_bitrate_kbps > 50000) ep.video_bitrate_kbps = 50000;
	if (ep.keyframe_interval_sec < 1 || ep.keyframe_interval_sec > 10)
		ep.keyframe_interval_sec = 2;
	if (ep.audio_bitrate_kbps < 64) ep.audio_bitrate_kbps = 64;
	if (ep.audio_bitrate_kbps > 320) ep.audio_bitrate_kbps = 320;

	return ep;
}

} // namespace smulti
