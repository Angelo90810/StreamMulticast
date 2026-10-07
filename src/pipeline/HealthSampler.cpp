/*
StreamMulticast — OBS Multi-RTMP Plugin with Per-Output Re-Encode
Copyright (C) 2026 Avanatro <contact@avanatro.com>

GPLv2 — see LICENSE for full text.
*/

#include "HealthSampler.hpp"
#include "../core/EndpointRegistry.hpp"
#include "../plugin-support.h"

#include <numeric>
#include <chrono>
#include <thread>
#include <unordered_set>

namespace smulti {

/* -----------------------------------------------------------------------
 * Constructor / Destructor
 * ----------------------------------------------------------------------- */
HealthSampler::HealthSampler(EndpointRegistry &registry)
	: m_registry(registry)
{
}

HealthSampler::~HealthSampler()
{
	stop();
}

/* -----------------------------------------------------------------------
 * start() / stop()
 * ----------------------------------------------------------------------- */
void HealthSampler::start()
{
	if (m_running.load())
		return;
	m_running.store(true);
	m_thread = std::thread(&HealthSampler::poll_loop, this);
	obs_log(LOG_INFO, "HealthSampler: started (2 Hz)");
}

void HealthSampler::stop()
{
	if (!m_running.load())
		return;
	m_running.store(false);
	if (m_thread.joinable())
		m_thread.join();
	obs_log(LOG_INFO, "HealthSampler: stopped");
}

/* -----------------------------------------------------------------------
 * snapshot()
 * ----------------------------------------------------------------------- */
std::unordered_map<std::string, HealthSnapshot> HealthSampler::snapshot() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_snapshots;
}

HealthSnapshot HealthSampler::snapshot_for(const std::string &endpoint_id) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	auto it = m_snapshots.find(endpoint_id);
	if (it != m_snapshots.end())
		return it->second;
	return HealthSnapshot{};
}

/* -----------------------------------------------------------------------
 * poll_loop — 2-Hz background thread
 * ----------------------------------------------------------------------- */
void HealthSampler::poll_loop()
{
	while (m_running.load()) {
		auto start = std::chrono::steady_clock::now();

		/* Get all endpoints from registry (returns a copy — safe) */
		auto endpoints = m_registry.all();

		std::unordered_set<std::string> live_ids;
		live_ids.reserve(endpoints.size());

		for (const auto &ep : endpoints) {
			live_ids.insert(ep.id);

			/* Sample disabled endpoints too. stop() is asynchronous by design,
			 * so an endpoint can legitimately be disabled while its controller
			 * is still Stopping. Reporting it as instantly Offline hid that
			 * real teardown state and made the UI lie during slow RTMP closes. */
			auto ctrl = m_registry.controller_for(ep.id);
			sample_output(ep, ctrl);
		}

		/* Prune endpoints removed since the previous poll.  The table hid
		 * stale snapshots because it keys rows from the registry, but the
		 * maps themselves otherwise grew forever across add/remove cycles. */
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			for (auto it = m_snapshots.begin(); it != m_snapshots.end();) {
				if (live_ids.count(it->first) == 0)
					it = m_snapshots.erase(it);
				else
					++it;
			}
		}
		for (auto it = m_bitrate_state.begin(); it != m_bitrate_state.end();) {
			if (live_ids.count(it->first) == 0)
				it = m_bitrate_state.erase(it);
			else
				++it;
		}

		/* Sleep remainder of interval */
		auto elapsed = std::chrono::steady_clock::now() - start;
		auto sleep_time = std::chrono::milliseconds(POLL_INTERVAL_MS) - elapsed;
		if (sleep_time.count() > 0)
			std::this_thread::sleep_for(sleep_time);
	}
}

/* -----------------------------------------------------------------------
 * sample_output — collect health data for one endpoint
 *
 * All obs_output_t access goes through OutputController::sample() /
 * connected_since() — see the class doc comment in HealthSampler.hpp for
 * why this indirection exists (it is not just style: it closes a real
 * use-after-free against the ControllerReaper).
 * ----------------------------------------------------------------------- */
void HealthSampler::sample_output(const Endpoint &ep, const std::shared_ptr<OutputController> &ctrl)
{
	if (!ctrl) {
		const int target =
			ep.video_settings_mode == EncoderSettingsMode::Custom &&
			ep.audio_settings_mode == EncoderSettingsMode::Custom
				? ep.video_bitrate_kbps + ep.audio_bitrate_kbps
				: -1;
		write_inactive_snapshot(ep, "", OutputState::Idle, target, 0);
		return;
	}

	/* One locked controller snapshot keeps state/error/stats internally
	 * consistent instead of sampling them across four separate lock windows. */
	OutputController::SampleData sample = ctrl->sample();

	if (!sample.active ||
	    sample.state == OutputState::Idle ||
	    sample.state == OutputState::FailedHard ||
	    sample.state == OutputState::Stopping) {
		write_inactive_snapshot(ep, sample.last_error, sample.state,
		                        sample.target_bitrate_kbps,
		                        sample.reconnect_count);
		return;
	}

	HealthSnapshot snap;
	snap.endpoint_id = ep.id;
	snap.target_bitrate = sample.target_bitrate_kbps;
	snap.last_error = sample.last_error;
	snap.state = sample.state;
	snap.reconnect_count = sample.reconnect_count;

	const uint64_t total_bytes = sample.total_bytes;
	snap.dropped_frames = static_cast<uint64_t>(sample.frames_dropped);
	const auto now = std::chrono::steady_clock::now();

	BitrateState &bstate = m_bitrate_state[ep.id];
	if (bstate.last_bytes > 0) {
		/* Counters may reset across an extremely fast reconnect without a
		 * polling tick observing the inactive window. Never unsigned-wrap. */
		if (total_bytes >= bstate.last_bytes) {
			const uint64_t byte_diff = total_bytes - bstate.last_bytes;
			const double elapsed_sec =
				std::chrono::duration<double>(now - bstate.last_time).count();
			if (elapsed_sec > 0.0) {
				const double kbps =
					(static_cast<double>(byte_diff) * 8.0) / elapsed_sec / 1000.0;
				bstate.samples.push_back(kbps);
				if (static_cast<int>(bstate.samples.size()) > ROLLING_SAMPLES)
					bstate.samples.erase(bstate.samples.begin());
			}
		} else {
			bstate.samples.clear();
		}
	}
	bstate.last_bytes = total_bytes;
	bstate.last_time = now;

	if (!bstate.samples.empty()) {
		const double sum =
			std::accumulate(bstate.samples.begin(), bstate.samples.end(), 0.0);
		snap.actual_bitrate = sum / static_cast<double>(bstate.samples.size());
	}

	if (sample.connected_since.time_since_epoch().count() != 0) {
		const auto uptime =
			std::chrono::duration_cast<std::chrono::seconds>(
				now - sample.connected_since).count();
		snap.uptime_sec = uptime > 0 ? uptime : 0;
	}

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_snapshots[ep.id] = snap;
	}
}

/* -----------------------------------------------------------------------
 * write_inactive_snapshot — cheap zeroed snapshot + bitrate-window reset
 * for any endpoint that is not currently streaming.  See the class doc
 * comment in HealthSampler.hpp: never dereferences obs_output_t itself —
 * `last_error`/`state` are either OutputController::last_error()/state()'s
 * already-locked return values, or defaults for an endpoint with no
 * controller lookup at all (M3's disabled-endpoint fast path).
 * ----------------------------------------------------------------------- */
void HealthSampler::write_inactive_snapshot(const Endpoint &ep, const std::string &last_error,
                                             OutputState state, int target_bitrate,
                                             int reconnect_count)
{
	HealthSnapshot snap;
	snap.endpoint_id     = ep.id;
	snap.target_bitrate  = target_bitrate;
	snap.last_error      = last_error;
	snap.state           = state;
	snap.actual_bitrate  = 0.0;
	snap.dropped_frames  = 0;
	snap.reconnect_count = reconnect_count;
	snap.uptime_sec      = 0;

	std::lock_guard<std::mutex> lock(m_mutex);
	m_snapshots[ep.id] = snap;
	/* Clear bitrate rolling window on every transition to inactive — see the
	 * M1 fix note above sample_output()'s `!sample.active` branch. */
	m_bitrate_state.erase(ep.id);
}

} // namespace smulti
