/*
StreamMulticast — OBS Multi-RTMP Plugin with Per-Output Re-Encode
GPLv2 — see LICENSE for full text.
*/
#pragma once

#include "OutputController.hpp"

namespace smulti {

struct LifecycleDecision {
	bool cancel_start_request = false;
	bool request_start = false;
	bool stop = false;
};

/**
 * Pure lifecycle policy used by the module-level coordinator.
 * Keeping this decision logic separate makes the linked/manual invariants
 * regression-testable without creating libobs outputs.
 */
inline LifecycleDecision decide_lifecycle(bool enabled,
                                          bool linked_to_main,
                                          bool main_stream_active,
                                          OutputState state,
                                          bool start_blocked,
                                          bool start_requested,
                                          bool has_session_resources)
{
	LifecycleDecision d;

	if (!enabled) {
		d.cancel_start_request = true;
		d.stop = has_session_resources;
		return d;
	}

	if (linked_to_main) {
		if (!main_stream_active) {
			d.cancel_start_request = true;
			d.stop = has_session_resources;
			return d;
		}

		if (state == OutputState::Idle ||
		    state == OutputState::Stopping ||
		    start_blocked ||
		    start_requested) {
			d.request_start = true;
		}
		return d;
	}

	/* Manual endpoints never auto-start merely because OBS is live. The one
	 * exception is a start request explicitly preserved from a previously
	 * live controller across a structural edit. */
	if (start_requested && !start_blocked && state != OutputState::Stopping)
		d.request_start = true;

	return d;
}

} // namespace smulti
