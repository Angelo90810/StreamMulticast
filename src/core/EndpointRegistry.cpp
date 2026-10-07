/*
StreamMulticast — OBS Multi-RTMP Plugin with Per-Output Re-Encode
Copyright (C) 2026 Avanatro <contact@avanatro.com>

GPLv2 — see LICENSE for full text.
*/

#include "EndpointRegistry.hpp"
#include "../pipeline/OutputController.hpp"
#include "../pipeline/ControllerReaper.hpp"
#include "../plugin-support.h"

#include <algorithm>
#include <cassert>
#include <unordered_set>


namespace smulti {

namespace {

bool controller_settings_changed(const Endpoint &a, const Endpoint &b)
{
	return a.server_url != b.server_url ||
	       a.stream_key != b.stream_key ||
	       a.video_settings_mode != b.video_settings_mode ||
	       a.video_codec != b.video_codec ||
	       a.encoder_backend != b.encoder_backend ||
	       a.video_bitrate_kbps != b.video_bitrate_kbps ||
	       a.keyframe_interval_sec != b.keyframe_interval_sec ||
	       a.audio_settings_mode != b.audio_settings_mode ||
	       a.audio_bitrate_kbps != b.audio_bitrate_kbps ||
	       a.orientation != b.orientation;
}

} // namespace

/* -----------------------------------------------------------------------
 * Constructor / Destructor
 * ----------------------------------------------------------------------- */
EndpointRegistry::EndpointRegistry(ConfigStore &store, ControllerReaper &reaper)
	: m_store(store)
	, m_reaper(reaper)
{
	/* Populate from the already-loaded ConfigStore */
	auto loaded = store.endpoints();
	std::sort(loaded.begin(), loaded.end(),
	          [](const Endpoint &a, const Endpoint &b) {
		          return a.sort_order < b.sort_order;
	          });

	bool repaired_ids = false;
	std::unordered_set<std::string> seen_ids;
	for (auto &ep : loaded) {
		if (ep.id.empty() || !seen_ids.insert(ep.id).second) {
			const std::string old_id = ep.id;
			do {
				ep.id = Endpoint::generate_uuid();
			} while (!seen_ids.insert(ep.id).second);
			repaired_ids = true;
			obs_log(LOG_WARNING,
			        "EndpointRegistry: repaired duplicate/empty endpoint id '%s' -> '%s'",
			        old_id.c_str(), ep.id.c_str());
		}
		m_controllers[ep.id] = std::make_shared<OutputController>(ep, m_reaper);
		m_endpoints.push_back(std::move(ep));
	}

	if (repaired_ids)
		m_store.schedule_save(m_endpoints);

	obs_log(LOG_INFO, "EndpointRegistry: initialised with %zu endpoints", m_endpoints.size());
}

EndpointRegistry::~EndpointRegistry()
{
	/* Hand every remaining controller off to the ControllerReaper instead of
	 * destroying it here — the reaper is drained and block-joined afterwards
	 * by obs_module_unload(), off whatever thread destroys this registry.
	 *
	 * stop() first, same detach path used by update()/remove() (fix-round 2,
	 * 2026-07-07): a caller (e.g. obs_module_unload()) may have already
	 * stopped a controller, but must not be relied on to have done so for
	 * every controller — a Reconnecting controller in particular. Without
	 * this, enqueue()'s shutdown_blocking() would run with the session still
	 * fully attached: obs_output_force_stop() before the reconnect thread is
	 * joined and m_output nulled, defeating the reconnect thread's identity
	 * guard and risking a concurrent obs_output_start() vs.
	 * obs_output_force_stop() on the same obs_output_t. stop() is idempotent
	 * and cheap when already stopped, so calling it here unconditionally is
	 * safe regardless of what the caller already did. */
	for (auto &pair : m_controllers) {
		pair.second->retire();
		pair.second->stop();
		m_reaper.enqueue(std::move(pair.second));
	}
	m_controllers.clear();
}

/* -----------------------------------------------------------------------
 * Query
 * ----------------------------------------------------------------------- */
std::vector<Endpoint> EndpointRegistry::all() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_endpoints;
}

std::optional<Endpoint> EndpointRegistry::find(const std::string &id) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	for (const auto &ep : m_endpoints) {
		if (ep.id == id)
			return ep;
	}
	return std::nullopt;
}

std::shared_ptr<OutputController> EndpointRegistry::controller_for(const std::string &id)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	auto it = m_controllers.find(id);
	return it != m_controllers.end() ? it->second : nullptr;
}

size_t EndpointRegistry::count() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_endpoints.size();
}

/* -----------------------------------------------------------------------
 * add()
 * ----------------------------------------------------------------------- */
void EndpointRegistry::add(Endpoint ep)
{
	/* Assign identity/order under the same lock so a malformed import cannot
	 * create duplicate UUIDs and alias two endpoints to one controller. */
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto id_exists = [&](const std::string &id) {
			return std::any_of(m_endpoints.begin(), m_endpoints.end(),
			                   [&](const Endpoint &e) { return e.id == id; });
		};
		if (ep.id.empty() || id_exists(ep.id)) {
			do {
				ep.id = Endpoint::generate_uuid();
			} while (id_exists(ep.id));
		}

		int max_order = -1;
		for (const auto &e : m_endpoints)
			max_order = std::max(max_order, e.sort_order);
		ep.sort_order = max_order + 1;

		m_controllers[ep.id] = std::make_shared<OutputController>(ep, m_reaper);
		m_endpoints.push_back(ep);
	}

	obs_log(LOG_INFO, "EndpointRegistry: added endpoint '%s' (id=%s)",
	        ep.name.c_str(), ep.id.c_str());

	notify_observers(ChangeKind::Added, ep);
	persist();
}

/* -----------------------------------------------------------------------
 * update()
 * ----------------------------------------------------------------------- */
void EndpointRegistry::update(const Endpoint &ep)
{
	bool controller_changed = false;
	bool old_was_active = false;
	bool old_handoff_pending = false;
	bool disable_existing = false;
	std::shared_ptr<OutputController> old_ctrl;
	std::shared_ptr<OutputController> replacement;

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = std::find_if(m_endpoints.begin(), m_endpoints.end(),
		                       [&](const Endpoint &e) { return e.id == ep.id; });
		if (it == m_endpoints.end()) {
			obs_log(LOG_WARNING, "EndpointRegistry::update — id not found: %s", ep.id.c_str());
			return;
		}

		const Endpoint previous = *it;
		controller_changed = controller_settings_changed(previous, ep);
		disable_existing = previous.enabled && !ep.enabled;
		*it = ep;

		auto ctrl_it = m_controllers.find(ep.id);
		if (ctrl_it == m_controllers.end()) {
			/* Self-heal a partially corrupted registry. */
			replacement = std::make_shared<OutputController>(ep, m_reaper);
			m_controllers[ep.id] = replacement;
		} else if (!controller_changed) {
			/* Name, linked-to-main, enabled and ordering do not require a
			 * destructive RTMP/encoder rebuild. In particular, simply
			 * opening Edit and pressing Save must not interrupt a live
			 * stream. */
			ctrl_it->second->set_enabled(ep.enabled);
		} else {
			old_was_active = ctrl_it->second->has_active_session();
			old_handoff_pending = ctrl_it->second->start_blocked() ||
			                      ctrl_it->second->state() == OutputState::Stopping;
			old_ctrl = std::move(ctrl_it->second);

			replacement = std::make_shared<OutputController>(ep, m_reaper);
			if (old_was_active || old_handoff_pending)
				replacement->begin_handoff_wait();
			ctrl_it->second = replacement;
		}
	}

	if (old_ctrl) {
		old_ctrl->retire();
		old_ctrl->stop();
		m_reaper.enqueue(old_ctrl);

		if (replacement && (old_was_active || old_handoff_pending)) {
			/* ControllerReaper is FIFO. This task therefore runs only after
			 * every teardown job queued for the prior controller(s), which
			 * makes a rapid sequence of edits safe as well. */
			m_reaper.enqueue([replacement]() { replacement->finish_handoff_wait(); });
		}
	} else if (disable_existing) {
		/* Enabled is intentionally a non-structural setting. Stop the same
		 * controller so re-enabling it cannot lose track of an older teardown
		 * session that still owns an NVENC/RTMP output. */
		auto ctrl = controller_for(ep.id);
		if (ctrl)
			ctrl->stop();
	}

	notify_observers(ChangeKind::Updated, ep);
	persist();
}

/* -----------------------------------------------------------------------
 * remove()
 * ----------------------------------------------------------------------- */
void EndpointRegistry::remove(const std::string &id)
{
	Endpoint removed_ep;
	std::shared_ptr<OutputController> old_ctrl;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = std::find_if(m_endpoints.begin(), m_endpoints.end(),
		                       [&](const Endpoint &e) { return e.id == id; });
		if (it == m_endpoints.end()) {
			obs_log(LOG_WARNING, "EndpointRegistry::remove — id not found: %s", id.c_str());
			return;
		}
		removed_ep = *it;

		/* Extract the controller WHILE HOLDING m_mutex; stop()/enqueue() it
		 * to the ControllerReaper AFTER releasing the lock (below) — stop()
		 * itself must never run under this lock either, not just the reaper
		 * hand-off (see item 6 of the fix-round 1 decision log entry). */
		auto ctrl_it = m_controllers.find(id);
		if (ctrl_it != m_controllers.end()) {
			old_ctrl = std::move(ctrl_it->second);
			m_controllers.erase(ctrl_it);
		}

		m_endpoints.erase(it);
	}

	if (old_ctrl) {
		old_ctrl->retire();
		/* stop() is cheap and non-blocking now — call it unconditionally
		 * (previously gated on is_running(), which excludes Reconnecting;
		 * that gap let a Reconnecting controller reach the reaper's
		 * shutdown_blocking() with its reconnect thread still live and no
		 * signal-disconnect done first). */
		old_ctrl->stop();
		m_reaper.enqueue(old_ctrl);
	}

	obs_log(LOG_INFO, "EndpointRegistry: removed endpoint '%s'", removed_ep.name.c_str());
	notify_observers(ChangeKind::Removed, removed_ep);
	persist();
}

/* -----------------------------------------------------------------------
 * reorder()
 * ----------------------------------------------------------------------- */
void EndpointRegistry::reorder(const std::vector<std::string> &ordered_ids)
{
	std::vector<Endpoint> updated;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		std::unordered_set<std::string> seen;
		int next_order = 0;

		/* Apply valid unique ids supplied by the UI first. */
		for (const auto &id : ordered_ids) {
			if (!seen.insert(id).second)
				continue;
			auto it = std::find_if(m_endpoints.begin(), m_endpoints.end(),
			                       [&](const Endpoint &e) { return e.id == id; });
			if (it != m_endpoints.end()) {
				it->sort_order = next_order++;
				updated.push_back(*it);
			}
		}

		/* Never lose/duplicate order values if a caller supplied a partial
		 * or stale list. Append every unmentioned endpoint deterministically. */
		for (auto &ep : m_endpoints) {
			if (seen.insert(ep.id).second) {
				ep.sort_order = next_order++;
				updated.push_back(ep);
			}
		}

		std::sort(m_endpoints.begin(), m_endpoints.end(),
		          [](const Endpoint &a, const Endpoint &b) {
			          return a.sort_order < b.sort_order;
		          });
	}

	for (const auto &ep : updated)
		notify_observers(ChangeKind::Updated, ep);
	persist();
}

/* -----------------------------------------------------------------------
 * Observer management
 * ----------------------------------------------------------------------- */
int EndpointRegistry::register_observer(RegistryObserver obs)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	int token = m_next_observer_token++;
	m_observers.emplace_back(token, std::move(obs));
	return token;
}

void EndpointRegistry::unregister_observer(int token)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_observers.erase(
		std::remove_if(m_observers.begin(), m_observers.end(),
		               [token](const auto &pair) { return pair.first == token; }),
		m_observers.end());
}

/* -----------------------------------------------------------------------
 * notify_observers — always called WITHOUT the mutex held
 * ----------------------------------------------------------------------- */
void EndpointRegistry::notify_observers(ChangeKind kind, const Endpoint &ep)
{
	/* Snapshot observers list to avoid holding lock during callbacks */
	std::vector<RegistryObserver> snapshot;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		snapshot.reserve(m_observers.size());
		for (const auto &pair : m_observers)
			snapshot.push_back(pair.second);
	}

	for (const auto &obs : snapshot)
		obs(kind, ep);
}

/* -----------------------------------------------------------------------
 * persist — triggers debounced ConfigStore save
 * ----------------------------------------------------------------------- */
void EndpointRegistry::persist()
{
	/* Schedule a debounced save — cheap, non-blocking */
	std::lock_guard<std::mutex> lock(m_mutex);
	m_store.schedule_save(m_endpoints);
}

} // namespace smulti
