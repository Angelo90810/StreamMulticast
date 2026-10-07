# StreamMulticast

**Current release:** v1.1.4 for OBS Studio 32.2.2 (Windows x64).

> Multi-destination streaming for OBS Studio on **Windows x64** — **per-output bitrate**, **per-output orientation** (horizontal + vertical in parallel), free, open-source, no account, no cloud.

The features Aitum Multistream charges for, built right into OBS.

---

## What it does

Stream to multiple RTMP endpoints (Twitch, YouTube, Facebook, Trovo, custom, and provider-supplied Kick URLs) simultaneously from a single OBS instance, with **independent bitrate, encoder backend, and frame orientation per output**.

**Example** — a 10 Mbit/s upload pipe can power:

| Endpoint | Encoder | Bitrate | Audio | Orientation |
|---|---|---|---|---|
| Twitch primary | NVENC h.264 | 6000 kbit/s | 160 kbit/s | Source (16:9) |
| TikTok / Shorts | x264 (CPU) | 2500 kbit/s | 96 kbit/s | Vertical 1080×1920 |
| YouTube backup | NVENC h.264 | 4500 kbit/s | 128 kbit/s | Source (16:9) |

All three run **at the same time**, from the same OBS scene.

---

## Why another multistream tool?

| Tool | Per-output bitrate | Per-output orientation | Free | Open-source | Local only |
|---|---|---|---|---|---|
| `obs-multi-rtmp` | ❌ | ❌ | ✅ | ✅ | ✅ |
| Aitum Multistream | 💰 (Pro) | 💰 (Pro / separate canvas plugin) | partial | ❌ | ✅ |
| Restream / StreamYard / Castr | ✅ | ✅ | ❌ | ❌ | ❌ (cloud re-encode) |
| **StreamMulticast** | ✅ | ✅ (stretch + true 90° rotated Program canvas) | ✅ | ✅ | ✅ |

---

## Features

### v1.0 — Core multistream
- **Multi-RTMP output** — N endpoints, no hard limit
- **Per-endpoint encoder + bitrate** — x264 / NVENC / QSV / AMF, video bitrate 500-25000 kbit/s, audio bitrate 64-320 kbit/s, keyframe interval 1-10 s
- **Live Health view** — per-output actual bitrate, dropped frames, reconnect count, uptime in a real-time grid inside the OBS dock (2 Hz polling)
- **Coexistence with OBS native streaming** — Twitch markers, replay buffer and recording stay untouched
- **Auto-reconnect with backoff** — 1s / 2s / 5s / 10s / 30s / 60s, max 10 attempts before hard-fail
- **Linked-to-main toggle** — optionally start/stop selected endpoints with OBS's main "Start Streaming"
- **Pre-defined endpoint templates** — Twitch, YouTube, Facebook, Instagram, TikTok, Trovo, custom RTMP. Instagram uses the Live Producer RTMPS ingest preset; its per-session stream key still comes from Instagram Live Producer. Kick is intentionally custom/import-only because its ingest host is provider/session dependent.

### v1.1 — Manual control, OBS inheritance, HEVC and vertical modes
- **Manual Start/Stop per endpoint** when automatic linkage to the OBS main stream is disabled
- **H.264 / H.265 (HEVC)** with auto-detected NVENC / QSV / AMF backends where available
- **Use OBS settings** mode clones the currently configured OBS streaming video/audio encoder settings while keeping each endpoint independently encoded
- **Vertical 1080×1920 — Stretch** fills the portrait frame by intentionally rescaling the full source
- **Vertical 1080×1920 — Rotate 90°** uses a dedicated `obs_view_t` and the real OBS Program transition output; viewers can turn the phone sideways to see the complete landscape composition
- Horizontal, stretched portrait and rotated portrait endpoints can run in parallel

### v1.0.6 — One-click import from OBS
- **Import from OBS** button in the Endpoint dialog — reads the live RTMP/RTMPS server URL and stream key from your active OBS profile, including provider-specific ingest URLs when OBS exposes them
- No OAuth flow needed inside StreamMulticast — we piggyback on OBS's own connection
- Handles OBS 28-31 config layout (`global.ini` legacy + `user.ini` modern)

### v1.0.7 — Stability & reliability release
- Fixed a UI freeze when deleting/editing an endpoint or exiting OBS while one of its servers had stopped responding — output teardown now runs on a dedicated background thread instead of blocking the dock
- Fixed config autosave silently stopping after the first save (a data-loss risk if OBS crashed before the next manual save)
- An invalid or rejected stream key now fails fast with a clear "Invalid stream key" error instead of retrying to reconnect forever
- Fixed the Health tab's uptime column, which previously always showed 0
- Internal thread-safety hardening around output start/stop/reconnect and health polling

**TikTok Bridge import**
- **Import TikTok Bridge** button in the Endpoint dialog -- reads local RTMP data from a Bridge JSON file
- Designed for TikTok's account-gated / ephemeral stream-key workflow
- StreamMulticast does not generate TikTok keys, perform TikTok login, or bundle third-party generators
- Default handoff path: `%APPDATA%\obs-studio\plugin_config\streammulticast\tiktok_bridge.json`

## Roadmap / not implemented

- Arbitrary per-output **resolution + FPS** beyond the built-in source/1080×1920 modes
- Per-output **center-crop / reframed 9:16 composition**
- Per-output **health-score** with auto-failover routing
- **Stream info push** (set title / tags / category across platforms via OAuth)
- Per-output **recording split**
- **Discord webhook alerts** on output failure
- **macOS + Linux** builds

---

## Installation

### Windows installer — recommended

1. Download the latest `StreamMulticast-<version>-Windows-Installer.exe` from the [Releases page](https://github.com/Angelo90810/StreamMulticast/releases)
2. Close OBS if it is running
3. Run the installer as administrator
4. The installer detects the OBS Studio folder from the Windows Registry, validates that `obs64.exe` exists and requires OBS Studio 32+
5. Start OBS, then open **View → Docks → Multistream**

No manual file copying is required. The installer also registers StreamMulticast in **Installed apps / Add or remove programs**, so uninstalling the plugin does not require hunting DLLs by hand.

The Windows installer follows the established OBS plugin packaging pattern used by [Source Dock](https://github.com/exeldro/obs-source-dock), adapted for StreamMulticast and validated in CI.

### ZIP — manual/portable fallback

A ZIP is still published for portable OBS installations or manual deployment. Close OBS and extract `StreamMulticast-windows-x64-RelWithDebInfo.zip` directly into the OBS root folder, typically `C:\Program Files\obs-studio\`.

Building from source is **only** for contributors who want to modify the code — see the [collapsed section at the bottom](#build-from-source-contributors-only).

---


## Broadcast Hub (v2 development on main)

The first v2 slice is now under development in the `Hub` tab:

- **YouTube stays native in OBS**: the Hub uses Google OAuth (desktop loopback + PKCE), finds the reusable YouTube Live stream whose stream key matches the one already configured in OBS, creates the scheduled broadcast and binds it to that existing stream. No duplicate YouTube encoder/output is created.
- **Facebook uses the official Graph API flow**: device login, managed Page selection, Live creation, automatic ingest URL import into a `Facebook (Hub)` endpoint, then an explicit publish step after preview.
- **Instagram stays manual** through Instagram Live Producer + the Instagram endpoint template.
- **Prepare YouTube + Facebook** prepares both connected destinations from one title/description/schedule/privacy form. YouTube uses auto-start/auto-stop on the OBS-native stream; Facebook remains unpublished until its explicit Publish button is pressed after preview.
- Hub refresh/Page tokens are stored with Windows DPAPI, like StreamMulticast stream keys.

The YouTube flow requires a Google Cloud **Desktop OAuth client** with YouTube Data API v3 enabled. The OAuth callback is a random local `127.0.0.1` port and uses PKCE; only the refresh token is persisted, protected by Windows DPAPI. The Facebook flow requires a Meta developer app with Device Login enabled and the Page/Live permissions available for the account: `pages_show_list`, `pages_read_engagement`, `pages_manage_posts`, and `publish_video`. Production use may require Meta App Review. Until those app credentials exist, the existing manual endpoints remain fully supported.

---
## Usage

1. Open the **Multistream** dock (`View → Docks → Multistream`)
2. **Configure** tab → **+ Add Endpoint**
3. Either click **Import from OBS** to pull the server URL + stream key from your active OBS profile, OR pick a template and paste the stream key manually
4. For TikTok, optionally click **Import TikTok Bridge** to read a local bridge handoff file
5. Choose **Canvas mode**:
   - `Source / match OBS canvas` — normal horizontal output
   - `Vertical 1080×1920 — Stretch to full screen` — fills the portrait frame, intentionally changing aspect ratio
   - `Vertical 1080×1920 — Rotate landscape 90°` — preserves the whole landscape Program output rotated inside a portrait stream
6. Choose **Use OBS streaming encoder settings** or **Custom settings**. Custom mode supports H.264 and hardware HEVC when available.
7. Choose whether the endpoint starts/stops with OBS. When automatic linkage is disabled, use the endpoint's own **Start/Stop** button.
8. Save, then use the **Health** tab to monitor state, bitrate, drops, reconnects and errors.

**Parallel example:** YouTube can remain on OBS native output while Facebook and Instagram run as independent StreamMulticast endpoints with their own encoder/bitrate/canvas settings.

Pair with [Stream Health Doctor](https://tools.avanatro.com/stream-health/) for deeper per-output telemetry in a separate browser window on a second monitor.

### TikTok Bridge handoff format

Optional helper tools can pass TikTok RTMP data to StreamMulticast with a local JSON handoff. The bundled Windows helper writes the stream key as a **DPAPI-protected** `stream_key_protected` value tied to the current Windows user. Legacy/third-party handoffs using `stream_key`/`key` remain readable for compatibility.

The importer also validates an optional ISO-8601 `expires_at` field and rejects expired credentials instead of attempting a doomed RTMP connection.

A minimal Windows companion script lives in `tools/tiktok-bridge/`. It can write the protected handoff file from prompts, clipboard content, or explicit command-line values, and can optionally start a user-chosen external helper.

---

## Build from source (contributors only)

> **You probably don't need this.** End users should use the pre-built Windows installer from [Releases](https://github.com/Angelo90810/StreamMulticast/releases) — see [Installation](#installation) above. This section exists for people who want to modify the code, audit it, or build for an unsupported platform.

<details>
<summary><strong>Show build instructions</strong></summary>

### Prerequisites

If you already have these installed (typical for C++ developers), the first build takes ~5 minutes:

- **Visual Studio 2022** with *Desktop development with C++* workload
- **CMake 3.28+**
- **Qt 6.5+** (path-discoverable by CMake; obs-deps' Qt6 also works automatically)
- **Git**

If you're starting from scratch (no toolchain): allow ~2-3 h for downloads (VS workload ~5 GB, Qt ~1 GB, libobs build deps ~500 MB). Unattended Qt install via `pip install aqtinstall && aqt install-qt windows desktop 6.5.3 win64_msvc2019_64 --outputdir C:\Qt`.

### Build + install

```powershell
git clone https://github.com/Angelo90810/StreamMulticast.git
cd StreamMulticast
cmake --preset windows-x64                                  # downloads libobs deps on first run
cmake --build --preset windows-x64 --config RelWithDebInfo  # ~30 sec on warm cache
cmake --install build_x64 --config RelWithDebInfo --prefix "C:\Program Files\obs-studio"   # admin
```

Output: `build_x64\RelWithDebInfo\streammulticast.dll` (≈230 KB). The install step requires admin privileges.

### CI

GitHub Actions builds the currently supported target, **Windows x64**, on every push to `main`. It runs the C++ core regression suite, validates the TikTok bridge helper, checks the OBS package layout, builds a Source Dock-style Inno Setup installer plus ZIP fallback, generates SHA-256 checksums, and publishes a release from an explicit `publish-v*` commit after the build succeeds. Third-party Actions are pinned to immutable commit SHAs. macOS/Linux presets remain contributor scaffolding and are not advertised as supported builds.

</details>

---

## Architecture

Native C++17 plugin against `libobs`, Qt 6 for the dock. Single-source / multi-encoder design: the OBS main video mix is tapped once, then N parallel `obs_encoder_t` instances feed N parallel `obs_output_t` RTMP outputs. Per-endpoint bitrate, audio bitrate, encoder backend and orientation are independent.

For portrait stretch, scaling is configured on the encoder with `obs_encoder_set_scaled_size()`; encoded outputs never call the raw-output-only `obs_output_set_video_conversion()`. Rotated portrait mode creates a dedicated 1080×1920 `obs_view_t`, renders OBS output channel 0 (the real Program transition source) through a 90° transformed private scene, and binds that video mix to the endpoint encoder.

Threading: a background `HealthSampler` thread polls each output at 2 Hz; the Qt UI reads thread-safe snapshots on the main thread. Start/stop reconciliation is module-owned and independent of the dock, so closing or failing to register the UI cannot strand automatic/deferred output lifecycle transitions.

```
src/
├── plugin-main.cpp        Plugin entry, frontend-event handler, dock registration
├── plugin-support.{c,h}   obs_log helper (generated from .c.in via configure_file)
├── core/
│   ├── Endpoint           Per-endpoint POD + serialise/deserialise
│   ├── ConfigStore        Versioned JSON persist + forward-schema protection
│   ├── SecretStore        Windows DPAPI at-rest stream-key protection
│   ├── EndpointRegistry   In-memory list + observer pattern
│   ├── ObsServiceImport   Read active OBS profile's stream key (v1.0.6)
│   └── TikTokBridgeImport Read local TikTok Bridge handoff JSON (v1.0.7)
├── pipeline/
│   ├── EncoderFactory     obs_encoder_create for x264 / NVENC / QSV / AMF
│   ├── OutputController   1× per endpoint, state machine, reconnect backoff
│   └── HealthSampler      2-Hz polling thread, telemetry snapshots
└── ui/
    ├── MultistreamDock    Qt dock with two tabs
    ├── HealthTab          Live grid (QTableView)
    ├── ConfigTab          Endpoint cards
    └── EndpointDialog     Modal settings (incl. Import from OBS + Output Orientation)
```

---

## Privacy

StreamMulticast does **not** collect telemetry, contact any servers other than the RTMP endpoints you configure, store data in any cloud, or require an account.

On Windows, StreamMulticast persists endpoint stream keys using **Windows DPAPI (CurrentUser)** in `%APPDATA%\obs-studio\plugin_config\streammulticast\config.json`; the bundled TikTok Bridge helper uses the same protection. Existing plaintext v1/v2 configs are read for compatibility and migrate to protected storage on the next successful save. DPAPI-protected keys are intentionally tied to the Windows user that created them.

---

## License

GPL-2.0-or-later (because libobs is GPL-2.0). See [`LICENSE`](LICENSE).

---

## Author

Built by [Avanatro](https://avanatro.com). Part of the [Avanatro Streamer Tools](https://tools.avanatro.com/) suite:

- [Stream Health Doctor](https://tools.avanatro.com/stream-health/) — free web-based OBS diagnostic HUD
- **StreamMulticast** — this plugin

Contact: contact@avanatro.com — bug reports + feature requests via [GitHub Issues](https://github.com/Angelo90810/StreamMulticast/issues).
