/*
StreamMulticast — OBS Multi-RTMP Plugin with Per-Output Re-Encode
Copyright (C) 2026 Avanatro <contact@avanatro.com>

GPLv2 — see LICENSE for full text.
*/

#pragma once

#include "../core/Endpoint.hpp"

#include <vector>
#include <string>

struct obs_encoder;
typedef struct obs_encoder obs_encoder_t;

namespace smulti {

struct EncoderTemplate {
	std::string type_id;
	std::string settings_json;
	size_t mixer_idx = 0;

	bool valid() const { return !type_id.empty() && !settings_json.empty(); }
};

class EncoderFactory {
public:
	EncoderFactory() = default;
	~EncoderFactory() = default;

	/**
	 * Creates either:
	 *  - a clone of OBS's currently configured streaming video encoder, or
	 *  - a custom H.264/HEVC encoder from the endpoint settings.
	 */
	obs_encoder_t *create_video_encoder(const Endpoint &ep,
	                                    const std::string &name_hint,
	                                    const EncoderTemplate *obs_template = nullptr) const;

	/**
	 * Creates either a clone of OBS's streaming audio encoder or a custom
	 * FFmpeg AAC encoder.
	 */
	obs_encoder_t *create_audio_encoder(const Endpoint &ep,
	                                    const std::string &name_hint,
	                                    const EncoderTemplate *obs_template = nullptr) const;

	/**
	 * Capture OBS-native streaming encoder settings. These functions touch
	 * OBS frontend state and therefore must be called on the frontend/UI
	 * thread. The resulting plain C++ snapshot is safe to reuse from the
	 * reconnect worker without touching OBS frontend objects again.
	 */
	bool capture_obs_video_template(EncoderTemplate &out) const;
	bool capture_obs_audio_template(EncoderTemplate &out) const;

	/** Hardware/software backends available for the requested codec. */
	static std::vector<EncoderBackend> available_backends(VideoCodec codec = VideoCodec::H264);

	static std::string backend_label(EncoderBackend backend);
	static std::string codec_label(VideoCodec codec);

	/** Preferred registered encoder id for a backend+codec pair. Empty if unsupported. */
	static std::string encoder_type_id(EncoderBackend backend, VideoCodec codec);

private:
	static bool is_encoder_available(const std::string &type_id);
	static std::string resolve_encoder_type(EncoderBackend backend, VideoCodec codec);

	obs_encoder_t *clone_obs_video_encoder(const Endpoint &ep,
	                                       const std::string &name_hint,
	                                       const EncoderTemplate &templ) const;
	obs_encoder_t *clone_obs_audio_encoder(const Endpoint &ep,
	                                       const std::string &name_hint,
	                                       const EncoderTemplate &templ) const;
};

} // namespace smulti
