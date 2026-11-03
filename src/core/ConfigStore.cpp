/*
StreamMulticast — OBS Multi-RTMP Plugin with Per-Output Re-Encode
Copyright (C) 2026 Avanatro <contact@avanatro.com>

GPLv2 — see LICENSE for full text.
*/

#include "ConfigStore.hpp"
#include "../plugin-support.h"

#include <obs-module.h>
#include <obs-data.h>
#include <util/platform.h>
#include <util/base.h>

#include <chrono>
#include <thread>

namespace smulti {

static constexpr int CURRENT_SCHEMA_VERSION = 3;
static constexpr int SAVE_DEBOUNCE_MS       = 500;

/* -----------------------------------------------------------------------
 * Constructor / Destructor
 * ----------------------------------------------------------------------- */
ConfigStore::ConfigStore()
{
	m_config_path = resolve_config_path();
	m_save_thread_running.store(true);
	m_save_thread = std::thread(&ConfigStore::save_thread_func, this);
}

ConfigStore::~ConfigStore()
{
	m_save_thread_running.store(false);
	if (m_save_thread.joinable())
		m_save_thread.join();
}

/* -----------------------------------------------------------------------
 * Path resolution
 *
 * obs_module_get_config_path() returns a bmalloc'd string (bfree it) and
 * does not create the directory, hence the os_mkdirs() below.
 * ----------------------------------------------------------------------- */
std::string ConfigStore::resolve_config_path()
{
	// obs_module_get_config_path allocates — caller must bfree()
	char *raw = obs_module_get_config_path(obs_current_module(), "config.json");
	if (!raw) {
		obs_log(LOG_WARNING, "obs_module_get_config_path returned null — using fallback path");
		return "";
	}
	std::string path(raw);
	bfree(raw);

	/* libobs paths are UTF-8 on every platform.  std::filesystem::path
	 * constructed from std::string uses the active Windows code page and can
	 * corrupt profiles with non-ASCII user names.  Stay in libobs's UTF-8
	 * path API end-to-end. */
	const size_t slash = path.find_last_of("/\\");
	if (slash != std::string::npos) {
		const std::string dir = path.substr(0, slash);
		if (!dir.empty() && os_mkdirs(dir.c_str()) == MKDIR_ERROR) {
			obs_log(LOG_WARNING, "Could not create config dir: %s", dir.c_str());
		}
	}

	return path;
}

/* -----------------------------------------------------------------------
 * preserve_unreadable_config()
 * ----------------------------------------------------------------------- */
void ConfigStore::preserve_unreadable_config(const std::string &path)
{
	const long long stamp = static_cast<long long>(
		std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch()).count());

	for (const std::string &src : {path, path + ".bak"}) {
		if (!os_file_exists(src.c_str()))
			continue;
		const std::string dst = src + ".corrupt-" + std::to_string(stamp);
		if (os_copyfile(src.c_str(), dst.c_str()) != 0)
			obs_log(LOG_WARNING, "ConfigStore: could not preserve %s", src.c_str());
	}
}

/* -----------------------------------------------------------------------
 * load()
 * ----------------------------------------------------------------------- */
bool ConfigStore::load()
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (m_config_path.empty()) {
		obs_log(LOG_WARNING, "ConfigStore: no config path — cannot load");
		return false;
	}

	const bool file_existed = os_file_exists(m_config_path.c_str());

	std::vector<Endpoint> loaded;
	if (!read_from_disk(m_config_path, loaded)) {
		if (file_existed) {
			/* Both config.json and its .bak were unreadable. The next save
			 * would replace them with an empty list, so keep a copy the
			 * user (or support) can still recover endpoints from. */
			preserve_unreadable_config(m_config_path);
			obs_log(LOG_WARNING, "ConfigStore: config.json is corrupt — starting fresh "
			                     "(a copy was kept next to it as config.json.corrupt-*)");
		} else {
			obs_log(LOG_INFO, "ConfigStore: no config.json yet — starting fresh");
		}
		m_endpoints.clear();
		return false;
	}

	m_endpoints = std::move(loaded);
	obs_log(LOG_INFO, "ConfigStore: loaded %zu endpoints from %s",
	        m_endpoints.size(), m_config_path.c_str());
	return true;
}

/* -----------------------------------------------------------------------
 * save() — synchronous (used from obs_module_unload)
 * ----------------------------------------------------------------------- */
bool ConfigStore::save(const std::vector<Endpoint> &endpoints)
{
	if (m_writes_blocked.load()) {
		obs_log(LOG_WARNING,
		        "ConfigStore: refusing to overwrite config written by a newer schema");
		return false;
	}

	/* Serialize generation assignment with the pending snapshot itself.
	 * Incrementing the atomic before taking m_pending_mutex allowed two
	 * concurrent callers to acquire the mutex in reverse order and publish
	 * an older generation AFTER a newer one. */
	uint64_t generation = 0;
	{
		std::lock_guard<std::mutex> pending_lock(m_pending_mutex);
		generation = m_save_generation.fetch_add(1) + 1;
		m_pending_endpoints = endpoints;
		m_pending_generation = generation;
		m_save_pending.store(false);
	}

	std::lock_guard<std::mutex> lock(m_mutex);
	m_endpoints = endpoints;

	if (m_config_path.empty()) {
		obs_log(LOG_WARNING, "ConfigStore: no config path — cannot save");
		return false;
	}

	return write_to_disk(endpoints, m_config_path);
}

/* -----------------------------------------------------------------------
 * schedule_save() — debounced (used from UI on every change)
 *
 * The request timestamp is set HERE, and only here — see save_thread_func()
 * for why the wait branch must never touch it (that was the livelock bug).
 * ----------------------------------------------------------------------- */
void ConfigStore::schedule_save(const std::vector<Endpoint> &endpoints)
{
	if (m_writes_blocked.load())
		return;

	std::lock_guard<std::mutex> lock(m_pending_mutex);
	const uint64_t generation = m_save_generation.fetch_add(1) + 1;
	m_pending_endpoints   = endpoints;
	m_pending_generation  = generation;
	m_last_change_request = std::chrono::steady_clock::now();
	m_save_pending.store(true);
}

/* -----------------------------------------------------------------------
 * save_thread_func — background thread, wakes every 100 ms
 * ----------------------------------------------------------------------- */
void ConfigStore::save_thread_func()
{
	using namespace std::chrono;

	while (m_save_thread_running.load()) {
		std::this_thread::sleep_for(milliseconds(100));

		if (!m_save_pending.load())
			continue;

		std::vector<Endpoint> to_save;
		uint64_t generation = 0;
		bool should_save = false;
		{
			std::lock_guard<std::mutex> lock(m_pending_mutex);
			auto elapsed = duration_cast<milliseconds>(steady_clock::now() - m_last_change_request);
			if (elapsed.count() >= SAVE_DEBOUNCE_MS) {
				to_save = m_pending_endpoints;
				generation = m_pending_generation;
				m_save_pending.store(false);
				should_save = true;
			}
			/* else: still within the debounce window.  Previously this
			 * branch reset last_change = now, which — with a 100 ms poll
			 * interval and a 500 ms threshold — meant the elapsed time could
			 * never reach the threshold as long as schedule_save() itself
			 * wasn't called again: autosave died silently after the first
			 * save, a data-loss bug on crash.  m_last_change_request is now
			 * owned exclusively by schedule_save(); this thread only reads it. */
		}

		if (should_save) {
			std::lock_guard<std::mutex> lock(m_mutex);
			/* A newer schedule_save()/save() wins.  This check is done while
			 * holding m_mutex so an older worker snapshot can never write
			 * after a newer synchronous save. */
			if (generation != m_save_generation.load())
				continue;
			m_endpoints = to_save;
			if (!m_config_path.empty())
				write_to_disk(to_save, m_config_path);
		}
	}

	/* Final flush if something was pending when we shut down */
	if (m_save_pending.load()) {
		std::vector<Endpoint> pending;
		uint64_t generation = 0;
		{
			std::lock_guard<std::mutex> pending_lock(m_pending_mutex);
			pending = m_pending_endpoints;
			generation = m_pending_generation;
			m_save_pending.store(false);
		}
		std::lock_guard<std::mutex> main_lock(m_mutex);
		if (generation == m_save_generation.load()) {
			m_endpoints = pending;
			if (!m_config_path.empty())
				write_to_disk(pending, m_config_path);
		}
	}
}

/* -----------------------------------------------------------------------
 * endpoints() — returns a copy (thread-safe)
 * ----------------------------------------------------------------------- */
std::vector<Endpoint> ConfigStore::endpoints() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_endpoints;
}

/* -----------------------------------------------------------------------
 * write_to_disk — serialise endpoint list to JSON
 * ----------------------------------------------------------------------- */
bool ConfigStore::write_to_disk(const std::vector<Endpoint> &endpoints, const std::string &path)
{
	obs_data_t *root = obs_data_create();
	obs_data_set_int(root, "schema_version", CURRENT_SCHEMA_VERSION);

	bool secret_protection_failed = false;
	obs_data_array_t *arr = obs_data_array_create();
	for (const auto &ep : endpoints) {
		obs_data_t *ep_data = ep.serialize(&secret_protection_failed);
		obs_data_array_push_back(arr, ep_data);
		obs_data_release(ep_data); // array addref'd — safe to release our handle
	}
	obs_data_set_array(root, "endpoints", arr);
	obs_data_array_release(arr);

	if (secret_protection_failed) {
		/* Writing now would either leak a key (plaintext) or lose it. Keep
		 * the previous file untouched and report the failure instead. */
		obs_log(LOG_ERROR, "ConfigStore: could not protect a stream key with DPAPI — "
		                   "config.json was not updated");
		obs_data_release(root);
		m_last_write_failed.store(true);
		return false;
	}

	/* The safe variant writes to .tmp then renames, preventing corruption
	 * on crash, and rotates the previous file into .bak. */
	bool ok = obs_data_save_json_safe(root, path.c_str(), "tmp", "bak");
	if (!ok)
		obs_log(LOG_WARNING, "ConfigStore: obs_data_save_json_safe failed for %s", path.c_str());

	if (ok && m_flush_legacy_backup) {
		/* The .bak now holds the pre-v3 file with plaintext keys. Save once
		 * more so the backup is the protected copy we just wrote. */
		if (obs_data_save_json_safe(root, path.c_str(), "tmp", "bak")) {
			m_flush_legacy_backup = false;
			obs_log(LOG_INFO, "ConfigStore: replaced legacy plaintext backup with protected copy");
		}
	}

	obs_data_release(root);
	m_last_write_failed.store(!ok);
	return ok;
}

/* -----------------------------------------------------------------------
 * read_from_disk — deserialise endpoint list from JSON
 * ----------------------------------------------------------------------- */
bool ConfigStore::read_from_disk(const std::string &path, std::vector<Endpoint> &out)
{
	if (path.empty() || !os_file_exists(path.c_str()))
		return false;

	/* Pair safe writes with safe reads so a truncated/corrupt primary file
	 * automatically falls back to the .bak generated by save_json_safe(). */
	obs_data_t *root = obs_data_create_from_json_file_safe(path.c_str(), "bak");
	if (!root)
		return false;

	int schema_version = static_cast<int>(obs_data_get_int(root, "schema_version"));

	if (schema_version > CURRENT_SCHEMA_VERSION) {
		/* Forward-compatible read-only mode: deserialize fields this build
		 * understands so the user can still inspect/use endpoints, but
		 * block every write so unknown fields from the newer schema can
		 * never be silently destroyed by a downgrade. */
		m_writes_blocked.store(true);
		obs_log(LOG_WARNING,
		        "ConfigStore: config schema v%d is newer than supported v%d; "
		        "loading known fields read-only and preserving the original file",
		        schema_version, CURRENT_SCHEMA_VERSION);
	}

	obs_data_array_t *arr = obs_data_get_array(root, "endpoints");
	if (!arr) {
		obs_data_release(root);
		return false;
	}

	size_t count = obs_data_array_count(arr);
	out.reserve(count);
	for (size_t i = 0; i < count; ++i) {
		obs_data_t *ep_data = obs_data_array_item(arr, i);
		Endpoint ep = Endpoint::deserialize(ep_data);
		obs_data_release(ep_data);
		if (!ep.id.empty())
			out.push_back(std::move(ep));
	}

	obs_data_array_release(arr);

	if (schema_version < CURRENT_SCHEMA_VERSION) {
#ifdef _WIN32
		for (const auto &ep : out) {
			if (!ep.stream_key.empty()) {
				m_flush_legacy_backup = true;
				break;
			}
		}
#endif
		migrate(schema_version, out);
	}

	obs_data_release(root);
	return true;
}

/* -----------------------------------------------------------------------
 * migrate — for future schema changes
 * ----------------------------------------------------------------------- */
void ConfigStore::migrate(int from_version, std::vector<Endpoint> &/*endpoints*/)
{
	obs_log(LOG_INFO, "ConfigStore: migrating config from schema v%d to v%d",
	        from_version, CURRENT_SCHEMA_VERSION);
	/* Endpoint::deserialize() supplies compatibility defaults for v1/v2.
	 * v3 adds protected stream-key storage on Windows; old plaintext keys
	 * are read normally and migrate on the next successful save. */
}

} // namespace smulti
