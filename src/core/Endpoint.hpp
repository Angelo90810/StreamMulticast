/*
StreamMulticast — OBS Multi-RTMP Plugin with Per-Output Re-Encode
Copyright (C) 2026 Avanatro <contact@avanatro.com>

GPLv2 — see LICENSE for full text.
*/

#pragma once

#include <string>
#include <cstdint>

struct obs_data;
typedef struct obs_data obs_data_t;

namespace smulti {

enum class EncoderBackend : int {
	X264  = 0,
	NVENC = 1,
	QSV   = 2,
	AMF   = 3,
};

/** Video codec used by a custom endpoint encoder. */
enum class VideoCodec : int {
	H264 = 0,
	HEVC = 1,
};

/**
 * EncoderSettingsMode
 *
 * Custom: use the endpoint's codec/backend/bitrate settings.
 * UseOBS: clone the encoder type + settings currently configured for
 *         OBS's native streaming output.  We clone instead of sharing the
 *         encoder so per-endpoint scaling/rotation remains independent.
 */
enum class EncoderSettingsMode : int {
	Custom = 0,
	UseOBS = 1,
};

/**
 * Output framing modes.
 *
 * SourceMatch:
 *   Keep OBS's normal output shape.
 *
 * Vertical1080x1920Stretch:
 *   Scale the full OBS frame to 1080x1920.  This intentionally stretches
 *   16:9 into 9:16 so the vertical frame is completely filled.
 *
 * Vertical1080x1920Rotated:
 *   Render the current OBS Program scene to a dedicated 1080x1920 view,
 *   rotated 90 degrees.  The whole landscape composition is preserved; a
 *   viewer can turn the phone sideways to see it as a normal 1920x1080 view.
 */
enum class OutputOrientation : int {
	SourceMatch                 = 0,
	Vertical1080x1920Stretch    = 1,
	Vertical1080x1920Rotated    = 2,
};

struct Endpoint {
	/* Identity */
	std::string id;
	std::string name;

	/* Connection */
	std::string server_url;
	std::string stream_key;

	/* Video */
	EncoderSettingsMode video_settings_mode = EncoderSettingsMode::Custom;
	VideoCodec video_codec = VideoCodec::H264;
	EncoderBackend encoder_backend = EncoderBackend::X264;
	int video_bitrate_kbps = 6000;
	int keyframe_interval_sec = 2;

	/* Audio */
	EncoderSettingsMode audio_settings_mode = EncoderSettingsMode::Custom;
	int audio_bitrate_kbps = 160;

	/* Framing */
	OutputOrientation orientation = OutputOrientation::SourceMatch;

	/* Behaviour */
	bool enabled = true;
	bool linked_to_main = true;

	/* Ordering */
	int sort_order = 0;

	obs_data_t *serialize() const;
	static Endpoint deserialize(obs_data_t *data);
	static std::string generate_uuid();
	static Endpoint make_default(const std::string &name = "New Endpoint");

	bool operator==(const Endpoint &o) const { return id == o.id; }
	bool operator!=(const Endpoint &o) const { return id != o.id; }
};

} // namespace smulti
