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
	                                    const std::string &name_hint) const;

	/**
	 * Creates either a clone of OBS's streaming audio encoder or a custom
	 * FFmpeg AAC encoder.
	 */
	obs_encoder_t *create_audio_encoder(const Endpoint &ep,
	                                    const std::string &name_hint) const;

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
	                                       const std::string &name_hint) const;
	obs_encoder_t *clone_obs_audio_encoder(const Endpoint &ep,
	                                       const std::string &name_hint) const;
};

} // namespace smulti
