/*
StreamMulticast core regression tests
*/

#include "../src/core/Endpoint.hpp"
#include "../src/core/SecretStore.hpp"
#include "../src/pipeline/LifecyclePolicy.hpp"

#include <obs-data.h>

#include <iostream>
#include <regex>
#include <set>
#include <string>

using namespace smulti;

namespace {

bool check(bool condition, const char *message)
{
	if (!condition)
		std::cerr << "FAIL: " << message << "\n";
	return condition;
}

bool test_uuid()
{
	std::set<std::string> seen;
	const std::regex uuid_v4(
		"^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$");

	for (int i = 0; i < 1000; ++i) {
		const std::string id = Endpoint::generate_uuid();
		if (!check(std::regex_match(id, uuid_v4), "UUID is not RFC4122 v4 shaped"))
			return false;
		if (!check(seen.insert(id).second, "duplicate UUID generated"))
			return false;
	}
	return true;
}

bool test_endpoint_roundtrip()
{
	Endpoint in = Endpoint::make_default("RoundTrip");
	in.server_url = "rtmps://example.invalid/live";
	in.stream_key = "super-secret-ci-key";
	in.video_settings_mode = EncoderSettingsMode::UseOBS;
	in.video_codec = VideoCodec::HEVC;
	in.encoder_backend = EncoderBackend::NVENC;
	in.video_bitrate_kbps = 12345;
	in.keyframe_interval_sec = 3;
	in.audio_settings_mode = EncoderSettingsMode::UseOBS;
	in.audio_bitrate_kbps = 192;
	in.orientation = OutputOrientation::Vertical1080x1920Rotated;
	in.enabled = false;
	in.linked_to_main = false;
	in.sort_order = 7;

	obs_data_t *data = in.serialize();
	if (!check(data != nullptr, "Endpoint::serialize returned null"))
		return false;

#ifdef _WIN32
	if (!check(obs_data_has_user_value(data, "stream_key_protected"),
	           "Windows serialization did not protect stream key")) {
		obs_data_release(data);
		return false;
	}
	if (!check(!obs_data_has_user_value(data, "stream_key"),
	           "Windows serialization leaked plaintext stream_key")) {
		obs_data_release(data);
		return false;
	}
#endif

	Endpoint out = Endpoint::deserialize(data);
	obs_data_release(data);

	return check(out.id == in.id, "id round-trip") &&
	       check(out.name == in.name, "name round-trip") &&
	       check(out.server_url == in.server_url, "server round-trip") &&
	       check(out.stream_key == in.stream_key, "stream key round-trip") &&
	       check(out.video_settings_mode == in.video_settings_mode, "video mode round-trip") &&
	       check(out.video_codec == in.video_codec, "codec round-trip") &&
	       check(out.encoder_backend == in.encoder_backend, "backend round-trip") &&
	       check(out.video_bitrate_kbps == in.video_bitrate_kbps, "video bitrate round-trip") &&
	       check(out.keyframe_interval_sec == in.keyframe_interval_sec, "keyint round-trip") &&
	       check(out.audio_settings_mode == in.audio_settings_mode, "audio mode round-trip") &&
	       check(out.audio_bitrate_kbps == in.audio_bitrate_kbps, "audio bitrate round-trip") &&
	       check(out.orientation == in.orientation, "orientation round-trip") &&
	       check(out.enabled == in.enabled, "enabled round-trip") &&
	       check(out.linked_to_main == in.linked_to_main, "linked round-trip") &&
	       check(out.sort_order == in.sort_order, "sort order round-trip");
}

bool test_legacy_and_validation()
{
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "id", "legacy-id");
	obs_data_set_string(data, "name", "Legacy");
	obs_data_set_string(data, "server_url", "rtmp://example.invalid/live");
	obs_data_set_string(data, "stream_key", "legacy-key");
	obs_data_set_int(data, "video_settings_mode", 99);
	obs_data_set_int(data, "video_codec", 99);
	obs_data_set_int(data, "encoder_backend", 99);
	obs_data_set_int(data, "video_bitrate", 1);
	obs_data_set_int(data, "keyframe_interval", 99);
	obs_data_set_int(data, "audio_settings_mode", 99);
	obs_data_set_int(data, "audio_bitrate", 999);
	obs_data_set_int(data, "orientation", 99);

	Endpoint ep = Endpoint::deserialize(data);
	obs_data_release(data);

	return check(ep.stream_key == "legacy-key", "legacy plaintext key compatibility") &&
	       check(ep.video_settings_mode == EncoderSettingsMode::Custom, "invalid video mode clamp") &&
	       check(ep.video_codec == VideoCodec::H264, "invalid codec clamp") &&
	       check(ep.encoder_backend == EncoderBackend::X264, "invalid backend clamp") &&
	       check(ep.video_bitrate_kbps == 500, "video bitrate lower clamp") &&
	       check(ep.keyframe_interval_sec == 2, "invalid keyint default") &&
	       check(ep.audio_settings_mode == EncoderSettingsMode::Custom, "invalid audio mode clamp") &&
	       check(ep.audio_bitrate_kbps == 320, "audio bitrate upper clamp") &&
	       check(ep.orientation == OutputOrientation::SourceMatch, "invalid orientation clamp");
}

bool test_undecryptable_secret_preservation()
{
#ifdef _WIN32
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "id", "foreign-dpapi");
	obs_data_set_string(data, "name", "Foreign DPAPI");
	obs_data_set_string(data, "server_url", "rtmps://example.invalid/live");
	obs_data_set_string(data, "stream_key_protected", "dpapi:00");

	Endpoint ep = Endpoint::deserialize(data);
	obs_data_release(data);

	if (!check(ep.stream_key.empty(), "undecryptable DPAPI key should not become plaintext"))
		return false;
	if (!check(ep.stream_key_decryption_failed, "undecryptable DPAPI key should be flagged"))
		return false;
	if (!check(ep.preserved_protected_stream_key == "dpapi:00",
	           "undecryptable DPAPI blob should be preserved"))
		return false;

	obs_data_t *roundtrip = ep.serialize();
	const std::string preserved = obs_data_get_string(roundtrip, "stream_key_protected");
	obs_data_release(roundtrip);
	return check(preserved == "dpapi:00", "undecryptable DPAPI blob was not round-tripped");
#else
	return true;
#endif
}

bool test_secret_store()
{
#ifdef _WIN32
	std::string protected_value;
	std::string plain;
	if (!check(protect_secret("dpapi-test-secret", protected_value), "DPAPI protect failed"))
		return false;
	if (!check(protected_value.rfind("dpapi:", 0) == 0, "DPAPI prefix missing"))
		return false;
	if (!check(protected_value.find("dpapi-test-secret") == std::string::npos,
	           "protected value contains plaintext"))
		return false;
	if (!check(unprotect_secret(protected_value, plain), "DPAPI unprotect failed"))
		return false;
	return check(plain == "dpapi-test-secret", "DPAPI round-trip mismatch");
#else
	std::string ignored;
	return check(!protect_secret("x", ignored), "non-Windows secret backend should be unavailable");
#endif
}


bool test_lifecycle_policy()
{
	{
		auto d = decide_lifecycle(true, true, true, OutputState::Idle, false, false, false);
		if (!check(d.request_start && !d.stop, "linked idle endpoint should start with main stream"))
			return false;
	}
	{
		auto d = decide_lifecycle(true, true, false, OutputState::Live, false, false, true);
		if (!check(d.stop && d.cancel_start_request, "linked live endpoint should stop with main stream"))
			return false;
	}
	{
		auto d = decide_lifecycle(true, false, true, OutputState::Idle, false, false, false);
		if (!check(!d.request_start && !d.stop, "manual endpoint must not auto-start with OBS"))
			return false;
	}
	{
		auto d = decide_lifecycle(true, false, false, OutputState::Idle, false, true, false);
		if (!check(d.request_start, "manual live-edit restart request should be honored"))
			return false;
	}
	{
		auto d = decide_lifecycle(true, true, false, OutputState::Live, false, false, true);
		if (!check(d.stop, "manual-to-linked transition while OBS is stopped must stop endpoint"))
			return false;
	}
	{
		auto d = decide_lifecycle(false, true, true, OutputState::Live, false, true, true);
		if (!check(d.stop && d.cancel_start_request && !d.request_start,
		           "disabled endpoint must stop and clear pending start"))
			return false;
	}
	{
		auto d = decide_lifecycle(true, true, true, OutputState::FailedHard, false, false, true);
		if (!check(!d.request_start, "FailedHard must not be hammered by lifecycle timer"))
			return false;
	}
	return true;
}

} // namespace

int main()
{
	const bool ok =
		test_uuid() &&
		test_endpoint_roundtrip() &&
		test_legacy_and_validation() &&
		test_secret_store() &&
		test_undecryptable_secret_preservation() &&
		test_lifecycle_policy();

	if (ok)
		std::cout << "All StreamMulticast core tests passed\n";
	return ok ? 0 : 1;
}
