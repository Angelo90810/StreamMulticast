# StreamMulticast TikTok Bridge Companion

This is a small local companion for the optional TikTok workflow.

It does not generate TikTok stream keys, perform TikTok login, or inspect other
processes. It only writes RTMP data supplied by the user, clipboard, or another
local helper into StreamMulticast's handoff file:

```text
%APPDATA%\obs-studio\plugin_config\streammulticast\tiktok_bridge.json
```

Then open the StreamMulticast endpoint dialog and click `Import TikTok Bridge`.

## Examples

Prompt for server URL and stream key:

```powershell
.\StreamMulticast.TikTokBridge.ps1
```

Read JSON or simple server/key text from the clipboard:

```powershell
.\StreamMulticast.TikTokBridge.ps1 -FromClipboard
```

For interactive use, prefer the secure prompt or clipboard import so the
stream key is not left in PowerShell command history.

For automation only, explicit values are supported:

```powershell
.\StreamMulticast.TikTokBridge.ps1 `
  -ServerUrl "rtmp://push-rtmp.tiktokcdn.com/live" `
  -StreamKey "your-temporary-key" `
  -ExpiresAt "2026-06-08T22:00:00Z"
```

The script warns when `-StreamKey` is passed on the command line because it
may be retained in shell history.

Optionally start an external helper chosen by the user:

```powershell
.\StreamMulticast.TikTokBridge.ps1 `
  -HelperPath "C:\Tools\SomeHelper.exe" `
  -FromClipboard
```

The bundled script writes the stream key using Windows DPAPI (CurrentUser), so the handoff file does not contain a reusable plaintext key:

```json
{
  "name": "TikTok Bridge",
  "server_url": "rtmp://push-rtmp.tiktokcdn.com/live",
  "stream_key_protected": "dpapi:<encrypted-hex>",
  "expires_at": "2026-06-08T22:00:00Z"
}
```

Legacy/third-party bridge files using `stream_key`/`key` are still accepted for compatibility. `server` can be used instead of `server_url`. If `expires_at` is present, StreamMulticast rejects invalid or expired timestamps instead of attempting the RTMP connection.
