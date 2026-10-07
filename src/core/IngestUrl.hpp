/*
StreamMulticast — RTMP ingest URL helpers
Copyright (C) 2026 Avanatro <contact@avanatro.com>
GPLv2 — see LICENSE for full text.
*/

#pragma once

#include <string>

namespace smulti {

struct SplitIngestUrl {
	bool ok = false;
	std::string server_url;
	std::string stream_key;
};

/**
 * Split a full RTMP/RTMPS publish URL into the server portion expected by
 * obs_service("rtmp_custom") and the stream-key portion. Query parameters
 * attached to the final path segment are part of the stream key and must be
 * preserved (Facebook Graph secure_stream_url relies on this).
 */
SplitIngestUrl split_rtmp_ingest_url(const std::string &full_url);

} // namespace smulti
