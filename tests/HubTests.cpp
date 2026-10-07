/*
StreamMulticast Broadcast Hub regression tests
*/

#include "../src/core/IngestUrl.hpp"

#include <iostream>
#include <string>

using namespace smulti;

namespace {

bool check(bool condition, const char *message)
{
	if (!condition)
		std::cerr << "FAIL: " << message << "\n";
	return condition;
}

bool test_facebook_secure_ingest_split()
{
	const auto split = split_rtmp_ingest_url(
		"rtmps://live-api-s.facebook.com:443/rtmp/123456789?ds=1&a=ABC%2FDEF");

	return check(split.ok, "Facebook secure ingest should split") &&
	       check(split.server_url == "rtmps://live-api-s.facebook.com:443/rtmp/",
	             "Facebook server URL mismatch") &&
	       check(split.stream_key == "123456789?ds=1&a=ABC%2FDEF",
	             "Facebook auth query must remain in stream key");
}

bool test_plain_ingest_split()
{
	const auto split = split_rtmp_ingest_url(
		"rtmps://example.invalid:443/live2/stream-key");

	return check(split.ok, "Plain RTMPS ingest should split") &&
	       check(split.server_url == "rtmps://example.invalid:443/live2/",
	             "Plain server URL mismatch") &&
	       check(split.stream_key == "stream-key",
	             "Plain stream key mismatch");
}

bool test_invalid_ingest_rejected()
{
	return check(!split_rtmp_ingest_url("https://example.invalid/live/key").ok,
	             "HTTPS URL must not be accepted as RTMP ingest") &&
	       check(!split_rtmp_ingest_url("rtmps://example.invalid/live/").ok,
	             "Missing stream key must be rejected");
}

} // namespace

int main()
{
	const bool ok =
		test_facebook_secure_ingest_split() &&
		test_plain_ingest_split() &&
		test_invalid_ingest_rejected();

	if (ok)
		std::cout << "All StreamMulticast Hub tests passed\n";
	return ok ? 0 : 1;
}
