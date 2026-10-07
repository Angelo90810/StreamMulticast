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

obs_encoder_t *EncoderFactory::clone_obs_video_encoder(const Endpoint &ep,
                                                        const std::string &name_hint) const
{
	obs_output_t *main_output = obs_frontend_get_streaming_output();
	if (!main_output) {
		obs_log(LOG_ERROR, "EncoderFactory: OBS streaming output is unavailable");
		return nullptr;
	}

	obs_encoder_t *source = obs_output_get_video_encoder(main_output);
	if (!source) {
		obs_output_release(main_output);
		obs_log(LOG_ERROR, "EncoderFactory: OBS streaming video encoder is unavailable");
		return nullptr;
	}

	const char *type_id_raw = obs_encoder_get_id(source);
	const std::string type_id = type_id_raw ? type_id_raw : "";
	obs_data_t *settings = obs_encoder_get_settings(source);
	std::string encoder_name = name_hint + "_video_" + ep.id;

	obs_encoder_t *enc = nullptr;
	if (!type_id.empty() && settings) {
		enc = obs_video_encoder_create(type_id.c_str(), encoder_name.c_str(), settings, nullptr);
	}

	if (settings)
		obs_data_release(settings);
	obs_output_release(main_output);

	if (!enc) {
		obs_log(LOG_ERROR, "EncoderFactory: failed to clone OBS streaming video encoder");
		return nullptr;
	}

	obs_encoder_set_video(enc, obs_get_video());
	obs_log(LOG_INFO,
	        "EncoderFactory: cloned OBS video encoder '%s' (type=%s)",
	        encoder_name.c_str(), type_id.empty() ? "(unknown)" : type_id.c_str());
	return enc;
}

obs_encoder_t *EncoderFactory::clone_obs_audio_encoder(const Endpoint &ep,
                                                        const std::string &name_hint) const
{
	obs_output_t *main_output = obs_frontend_get_streaming_output();
	if (!main_output) {
		obs_log(LOG_ERROR, "EncoderFactory: OBS streaming output is unavailable for audio clone");
		return nullptr;
	}

	obs_encoder_t *source = obs_output_get_audio_encoder(main_output, 0);
	if (!source) {
		obs_output_release(main_output);
		obs_log(LOG_ERROR, "EncoderFactory: OBS streaming audio encoder is unavailable");
		return nullptr;
	}

	const char *type_id_raw = obs_encoder_get_id(source);
	const std::string type_id = type_id_raw ? type_id_raw : "";
	const size_t mixer_idx = obs_encoder_get_mixer_index(source);
	obs_data_t *settings = obs_encoder_get_settings(source);
	std::string encoder_name = name_hint + "_audio_" + ep.id;

	obs_encoder_t *enc = nullptr;
	if (!type_id.empty() && settings) {
		enc = obs_audio_encoder_create(type_id.c_str(), encoder_name.c_str(), settings, mixer_idx, nullptr);
	}

	if (settings)
		obs_data_release(settings);
	obs_output_release(main_output);

	if (!enc) {
		obs_log(LOG_ERROR, "EncoderFactory: failed to clone OBS streaming audio encoder");
		return nullptr;
	}

	obs_encoder_set_audio(enc, obs_get_audio());
	obs_log(LOG_INFO,
	        "EncoderFactory: cloned OBS audio encoder '%s' (type=%s)",
	        encoder_name.c_str(), type_id.empty() ? "(unknown)" : type_id.c_str());
	return enc;
}

obs_encoder_t *EncoderFactory::create_video_encoder(const Endpoint &ep,
                                                     const std::string &name_hint) const
{
	if (ep.video_settings_mode == EncoderSettingsMode::UseOBS)
		return clone_obs_video_encoder(ep, name_hint);

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
                                                     const std::string &name_hint) const
{
	if (ep.audio_settings_mode == EncoderSettingsMode::UseOBS)
		return clone_obs_audio_encoder(ep, name_hint);

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
