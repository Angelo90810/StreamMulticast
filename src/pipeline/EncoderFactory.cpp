/*
StreamMulticast — OBS Multi-RTMP Plugin with Per-Output Re-Encode
Copyright (C) 2026 Avanatro <contact@avanatro.com>

GPLv2 — see LICENSE for full text.
*/

#include "EncoderFactory.hpp"
#include "../plugin-support.h"

#include <obs.h>
#include <obs-data.h>
#include <obs-output.h>
#include <obs-frontend-api.h>
#include <util/config-file.h>
#include <util/dstr.h>
#include <util/platform.h>
#include <util/bmem.h>

#include <cstring>


namespace smulti {

std::string EncoderFactory::encoder_type_id(EncoderBackend backend, VideoCodec codec)
{
	if (codec == VideoCodec::HEVC) {
		switch (backend) {
		case EncoderBackend::NVENC: return "obs_nvenc_hevc_tex";
		case EncoderBackend::QSV:   return "obs_qsv11_hevc";
		case EncoderBackend::AMF:   return "h265_texture_amf";
		case EncoderBackend::X264:  return {};
		default:                    return {};
		}
	}

	switch (backend) {
	case EncoderBackend::X264:  return "obs_x264";
	case EncoderBackend::NVENC: return "obs_nvenc_h264_tex";
	case EncoderBackend::QSV:   return "obs_qsv11_v2";
	case EncoderBackend::AMF:   return "h264_texture_amf";
	default:                    return "obs_x264";
	}
}

std::string EncoderFactory::backend_label(EncoderBackend backend)
{
	switch (backend) {
	case EncoderBackend::X264:  return "x264 (Software)";
	case EncoderBackend::NVENC: return "NVENC (NVIDIA GPU)";
	case EncoderBackend::QSV:   return "QSV (Intel GPU)";
	case EncoderBackend::AMF:   return "AMF (AMD GPU)";
	default:                    return "Unknown";
	}
}

std::string EncoderFactory::codec_label(VideoCodec codec)
{
	return codec == VideoCodec::HEVC ? "H.265 / HEVC" : "H.264 / AVC";
}

bool EncoderFactory::is_encoder_available(const std::string &type_id)
{
	if (type_id.empty())
		return false;

	/* Do not cache this list. OBS modules are discovered during startup and
	 * plugin load order is not a stable API contract. A cache populated
	 * before obs-nvenc/obs-qsv/AMF registers would hide that encoder for the
	 * entire OBS session. */
	size_t idx = 0;
	const char *id = nullptr;
	while (obs_enum_encoder_types(idx++, &id)) {
		if (id && type_id == id)
			return true;
	}
	return false;
}

std::string EncoderFactory::resolve_encoder_type(EncoderBackend backend, VideoCodec codec)
{
	std::string preferred = encoder_type_id(backend, codec);
	if (is_encoder_available(preferred))
		return preferred;

	if (backend == EncoderBackend::NVENC) {
		if (codec == VideoCodec::HEVC) {
			if (is_encoder_available("ffmpeg_hevc_nvenc"))
				return "ffmpeg_hevc_nvenc";
		} else {
			if (is_encoder_available("jim_nvenc"))
				return "jim_nvenc";
			if (is_encoder_available("ffmpeg_nvenc"))
				return "ffmpeg_nvenc";
		}
	}

	if (backend == EncoderBackend::QSV && codec == VideoCodec::H264 &&
	    is_encoder_available("obs_qsv11"))
		return "obs_qsv11";

	return {};
}

std::vector<EncoderBackend> EncoderFactory::available_backends(VideoCodec codec)
{
	std::vector<EncoderBackend> result;
	for (EncoderBackend backend : {
		EncoderBackend::X264,
		EncoderBackend::NVENC,
		EncoderBackend::QSV,
		EncoderBackend::AMF,
	}) {
		if (!resolve_encoder_type(backend, codec).empty())
			result.push_back(backend);
	}
	return result;
}

namespace {

std::string profile_file_path(const char *file_name)
{
	char *profile_raw = obs_frontend_get_current_profile_path();
	if (!profile_raw)
		return {};
	std::string path = profile_raw;
	bfree(profile_raw);
	if (path.empty())
		return {};
	if (path.back() != '/' && path.back() != '\\')
		path += '/';
	return path + file_name;
}

bool is_advanced_output_mode(config_t *profile)
{
	const char *mode = config_get_string(profile, "Output", "Mode");
	return mode && astrcmpi(mode, "Advanced") == 0;
}

/* RTMP cannot carry Opus. OBS only allows Opus for protocols such as
 * WHIP; for an RTMP endpoint the closest faithful choice is AAC at the
 * same bitrate. */
std::string rtmp_compatible_audio_encoder(const char *id)
{
	if (!id || !*id || astrcmpi(id, "opus") == 0 || strstr(id, "opus") != nullptr)
		return "ffmpeg_aac";
	if (astrcmpi(id, "aac") == 0)
		return "ffmpeg_aac";
	return id;
}

} // namespace

bool EncoderFactory::capture_live_video_template(EncoderTemplate &out)
{
	out = {};

	obs_output_t *main_output = obs_frontend_get_streaming_output();
	if (!main_output)
		return false;

	obs_encoder_t *source = obs_output_get_video_encoder(main_output);
	if (!source) {
		obs_output_release(main_output);
		return false;
	}

	const char *type_id = obs_encoder_get_id(source);
	obs_data_t *settings = obs_encoder_get_settings(source);
	if (type_id && *type_id && settings) {
		const char *json = obs_data_get_json(settings);
		out.type_id = type_id;
		out.settings_json = json ? json : "";
	}

	if (settings)
		obs_data_release(settings);
	obs_output_release(main_output);

	if (!out.valid()) {
		out = {};
		return false;
	}
	return true;
}

bool EncoderFactory::capture_live_audio_template(EncoderTemplate &out)
{
	out = {};

	obs_output_t *main_output = obs_frontend_get_streaming_output();
	if (!main_output)
		return false;

	obs_encoder_t *source = obs_output_get_audio_encoder(main_output, 0);
	if (!source) {
		obs_output_release(main_output);
		return false;
	}

	const char *type_id = obs_encoder_get_id(source);
	obs_data_t *settings = obs_encoder_get_settings(source);
	if (type_id && *type_id && settings) {
		const char *json = obs_data_get_json(settings);
		out.type_id = rtmp_compatible_audio_encoder(type_id);
		out.settings_json = json ? json : "";
		out.mixer_idx = obs_encoder_get_mixer_index(source);
	}

	if (settings)
		obs_data_release(settings);
	obs_output_release(main_output);

	if (!out.valid()) {
		out = {};
		return false;
	}
	return true;
}

bool EncoderFactory::capture_profile_video_template(EncoderTemplate &out)
{
	out = {};

	config_t *profile = obs_frontend_get_profile_config();
	if (!profile)
		return false;

	obs_data_t *settings = nullptr;

	if (is_advanced_output_mode(profile)) {
		/* AdvancedOutput: encoder id in AdvOut/Encoder, its settings in
		 * the profile's streamEncoder.json (absent = encoder defaults). */
		const char *type_id = config_get_string(profile, "AdvOut", "Encoder");
		if (!type_id || !*type_id)
			return false;
		out.type_id = type_id;

		const std::string json_path = profile_file_path("streamEncoder.json");
		if (!json_path.empty() && os_file_exists(json_path.c_str()))
			settings = obs_data_create_from_json_file_safe(json_path.c_str(), "bak");
		if (!settings)
			settings = obs_data_create();
	} else {
		/* SimpleOutput: same simple-name → encoder-id table and preset keys
		 * as OBS's SimpleOutput::Update(). */
		const char *simple = config_get_string(profile, "SimpleOutput", "StreamEncoder");
		const std::string enc = simple ? simple : "";
		const char *preset_key = "Preset";

		if (enc == "x264" || enc == "x264_lowcpu" || enc.empty()) {
			out.type_id = "obs_x264";
		} else if (enc == "qsv") {
			out.type_id = "obs_qsv11_v2";
			preset_key = "QSVPreset";
		} else if (enc == "qsv_av1") {
			out.type_id = "obs_qsv11_av1";
			preset_key = "QSVPreset";
		} else if (enc == "amd") {
			out.type_id = "h264_texture_amf";
			preset_key = "AMDPreset";
		} else if (enc == "amd_hevc") {
			out.type_id = "h265_texture_amf";
			preset_key = "AMDPreset";
		} else if (enc == "amd_av1") {
			out.type_id = "av1_texture_amf";
			preset_key = "AMDAV1Preset";
		} else if (enc == "nvenc") {
			out.type_id = is_encoder_available("obs_nvenc_h264_tex") ? "obs_nvenc_h264_tex"
			                                                         : "ffmpeg_nvenc";
			preset_key = "NVENCPreset2";
		} else if (enc == "nvenc_hevc") {
			out.type_id = is_encoder_available("obs_nvenc_hevc_tex") ? "obs_nvenc_hevc_tex"
			                                                         : "ffmpeg_hevc_nvenc";
			preset_key = "NVENCPreset2";
		} else if (enc == "nvenc_av1") {
			out.type_id = "obs_nvenc_av1_tex";
			preset_key = "NVENCPreset2";
		} else {
			out.type_id = "obs_x264";
		}

		settings = obs_data_create();
		obs_data_set_string(settings, "rate_control", "CBR");
		obs_data_set_int(settings, "bitrate",
		                 static_cast<long long>(config_get_uint(profile, "SimpleOutput", "VBitrate")));
		/* Simple mode relies on the service for the keyframe interval;
		 * every RTMP platform StreamMulticast targets accepts 2 s. */
		obs_data_set_int(settings, "keyint_sec", 2);

		const char *preset = config_get_string(profile, "SimpleOutput", preset_key);
		if (preset && *preset) {
			const bool legacy_nvenc = out.type_id.rfind("ffmpeg_", 0) == 0 &&
			                          std::string(preset_key) == "NVENCPreset2";
			obs_data_set_string(settings, legacy_nvenc ? "preset2" : "preset", preset);
		}

		if (out.type_id == "obs_x264") {
			const char *custom = config_get_string(profile, "SimpleOutput", "x264Settings");
			if (config_get_bool(profile, "SimpleOutput", "UseAdvanced") && custom)
				obs_data_set_string(settings, "x264opts", custom);
		}
	}

	if (!is_encoder_available(out.type_id)) {
		obs_log(LOG_ERROR, "EncoderFactory: OBS profile encoder '%s' is not available",
		        out.type_id.c_str());
		obs_data_release(settings);
		out = {};
		return false;
	}

	const char *json = obs_data_get_json(settings);
	out.settings_json = json ? json : "";
	obs_data_release(settings);

	if (!out.valid()) {
		out = {};
		return false;
	}
	return true;
}

bool EncoderFactory::capture_profile_audio_template(EncoderTemplate &out)
{
	out = {};

	config_t *profile = obs_frontend_get_profile_config();
	if (!profile)
		return false;

	int bitrate = 160;
	if (is_advanced_output_mode(profile)) {
		out.type_id = rtmp_compatible_audio_encoder(
			config_get_string(profile, "AdvOut", "AudioEncoder"));

		long long track = config_get_int(profile, "AdvOut", "TrackIndex");
		if (track < 1 || track > 6)
			track = 1;
		out.mixer_idx = static_cast<size_t>(track - 1);

		const std::string key = "Track" + std::to_string(track) + "Bitrate";
		const long long configured = config_get_int(profile, "AdvOut", key.c_str());
		if (configured > 0)
			bitrate = static_cast<int>(configured);
	} else {
		out.type_id = rtmp_compatible_audio_encoder(
			config_get_string(profile, "SimpleOutput", "StreamAudioEncoder"));
		out.mixer_idx = 0;
		const long long configured =
			static_cast<long long>(config_get_uint(profile, "SimpleOutput", "ABitrate"));
		if (configured > 0)
			bitrate = static_cast<int>(configured);
	}

	if (!is_encoder_available(out.type_id))
		out.type_id = "ffmpeg_aac";

	obs_data_t *settings = obs_data_create();
	obs_data_set_int(settings, "bitrate", bitrate);
	const char *json = obs_data_get_json(settings);
	out.settings_json = json ? json : "";
	obs_data_release(settings);

	if (!out.valid()) {
		out = {};
		return false;
	}
	return true;
}

bool EncoderFactory::capture_obs_video_template(EncoderTemplate &out) const
{
	if (capture_live_video_template(out))
		return true;

	if (capture_profile_video_template(out)) {
		obs_log(LOG_INFO,
		        "EncoderFactory: OBS streaming output not created yet — using the "
		        "active profile's video encoder settings (type=%s)",
		        out.type_id.c_str());
		return true;
	}

	obs_log(LOG_ERROR, "EncoderFactory: could not read OBS streaming video encoder "
	                   "settings from the live output or the active profile");
	return false;
}

bool EncoderFactory::capture_obs_audio_template(EncoderTemplate &out) const
{
	if (capture_live_audio_template(out))
		return true;

	if (capture_profile_audio_template(out)) {
		obs_log(LOG_INFO,
		        "EncoderFactory: OBS streaming output not created yet — using the "
		        "active profile's audio encoder settings (type=%s, track=%zu)",
		        out.type_id.c_str(), out.mixer_idx + 1);
		return true;
	}

	obs_log(LOG_ERROR, "EncoderFactory: could not read OBS streaming audio encoder "
	                   "settings from the live output or the active profile");
	return false;
}

obs_encoder_t *EncoderFactory::clone_obs_video_encoder(const Endpoint &ep,
                                                        const std::string &name_hint,
                                                        const EncoderTemplate &templ) const
{
	if (!templ.valid())
		return nullptr;

	obs_data_t *settings = obs_data_create_from_json(templ.settings_json.c_str());
	if (!settings)
		return nullptr;

	std::string encoder_name = name_hint + "_video_" + ep.id;
	obs_encoder_t *enc = obs_video_encoder_create(
		templ.type_id.c_str(), encoder_name.c_str(), settings, nullptr);
	obs_data_release(settings);

	if (!enc) {
		obs_log(LOG_ERROR, "EncoderFactory: failed to clone OBS streaming video encoder");
		return nullptr;
	}

	obs_encoder_set_video(enc, obs_get_video());
	obs_log(LOG_INFO,
	        "EncoderFactory: cloned cached OBS video encoder '%s' (type=%s)",
	        encoder_name.c_str(), templ.type_id.c_str());
	return enc;
}

obs_encoder_t *EncoderFactory::clone_obs_audio_encoder(const Endpoint &ep,
                                                        const std::string &name_hint,
                                                        const EncoderTemplate &templ) const
{
	if (!templ.valid())
		return nullptr;

	obs_data_t *settings = obs_data_create_from_json(templ.settings_json.c_str());
	if (!settings)
		return nullptr;

	std::string encoder_name = name_hint + "_audio_" + ep.id;
	obs_encoder_t *enc = obs_audio_encoder_create(
		templ.type_id.c_str(), encoder_name.c_str(), settings, templ.mixer_idx, nullptr);
	obs_data_release(settings);

	if (!enc) {
		obs_log(LOG_ERROR, "EncoderFactory: failed to clone OBS streaming audio encoder");
		return nullptr;
	}

	obs_encoder_set_audio(enc, obs_get_audio());
	obs_log(LOG_INFO,
	        "EncoderFactory: cloned cached OBS audio encoder '%s' (type=%s)",
	        encoder_name.c_str(), templ.type_id.c_str());
	return enc;
}

obs_encoder_t *EncoderFactory::create_video_encoder(const Endpoint &ep,
                                                     const std::string &name_hint,
                                                     const EncoderTemplate *obs_template) const
{
	if (ep.video_settings_mode == EncoderSettingsMode::UseOBS)
		return obs_template ? clone_obs_video_encoder(ep, name_hint, *obs_template) : nullptr;

	std::string type_id = resolve_encoder_type(ep.encoder_backend, ep.video_codec);
	if (type_id.empty()) {
		obs_log(LOG_ERROR,
		        "EncoderFactory: no %s encoder available for backend %s",
		        codec_label(ep.video_codec).c_str(),
		        backend_label(ep.encoder_backend).c_str());
		return nullptr;
	}

	obs_data_t *settings = obs_data_create();
	obs_data_set_int(settings, "bitrate", ep.video_bitrate_kbps);
	obs_data_set_int(settings, "keyint_sec", ep.keyframe_interval_sec);
	obs_data_set_string(settings, "rate_control", "CBR");

	if (type_id == "obs_x264") {
		/* Match OBS's normal streaming defaults. Forcing "zerolatency"
		 * disables compression tools and needlessly hurts quality at the
		 * same bitrate; a multistream RTMP output does not require it. */
		obs_data_set_string(settings, "preset", "veryfast");
		obs_data_set_string(settings, "profile", "high");
		obs_data_set_string(settings, "tune", "");
		obs_data_set_bool(settings, "use_bufsize", false);
	}

	if (type_id == "obs_nvenc_h264_tex" || type_id == "obs_nvenc_hevc_tex" ||
	    type_id == "jim_nvenc" || type_id == "ffmpeg_nvenc" ||
	    type_id == "ffmpeg_hevc_nvenc") {
		obs_data_set_string(settings, "preset", "p5");
		obs_data_set_string(settings, "multipass", "qres");
		obs_data_set_string(settings, "tune", "hq");
		if (ep.video_codec == VideoCodec::H264)
			obs_data_set_string(settings, "profile", "high");
		obs_data_set_bool(settings, "adaptive_quantization", true);
		obs_data_set_int(settings, "bf", 2);
	}

	std::string encoder_name = name_hint + "_video_" + ep.id;
	obs_encoder_t *enc = obs_video_encoder_create(
		type_id.c_str(), encoder_name.c_str(), settings, nullptr);
	obs_data_release(settings);

	if (!enc) {
		obs_log(LOG_ERROR,
		        "EncoderFactory: obs_video_encoder_create failed for type '%s'",
		        type_id.c_str());
		return nullptr;
	}

	obs_encoder_set_video(enc, obs_get_video());
	obs_log(LOG_INFO,
	        "EncoderFactory: created video encoder '%s' (type=%s, codec=%s, bitrate=%d kbps)",
	        encoder_name.c_str(), type_id.c_str(),
	        codec_label(ep.video_codec).c_str(), ep.video_bitrate_kbps);
	return enc;
}

obs_encoder_t *EncoderFactory::create_audio_encoder(const Endpoint &ep,
                                                     const std::string &name_hint,
                                                     const EncoderTemplate *obs_template) const
{
	if (ep.audio_settings_mode == EncoderSettingsMode::UseOBS)
		return obs_template ? clone_obs_audio_encoder(ep, name_hint, *obs_template) : nullptr;

	obs_data_t *settings = obs_data_create();
	obs_data_set_int(settings, "bitrate", ep.audio_bitrate_kbps);

	std::string encoder_name = name_hint + "_audio_" + ep.id;
	obs_encoder_t *enc = obs_audio_encoder_create(
		"ffmpeg_aac", encoder_name.c_str(), settings, 0, nullptr);
	obs_data_release(settings);

	if (!enc) {
		obs_log(LOG_ERROR, "EncoderFactory: obs_audio_encoder_create failed");
		return nullptr;
	}

	obs_encoder_set_audio(enc, obs_get_audio());
	obs_log(LOG_INFO,
	        "EncoderFactory: created audio encoder '%s' (%d kbps AAC)",
	        encoder_name.c_str(), ep.audio_bitrate_kbps);
	return enc;
}

} // namespace smulti
