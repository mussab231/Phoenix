# Phoenix Native Messaging Host (browser integration)

Hands download links from Chrome/Edge/Firefox straight to Phoenix — the intended
replacement for clipboard scraping. When you right-click a link and pick
"Send link to Phoenix", the extension asks the native host to download it.

## How it works

The browser launches `Phoenix.exe --native-messaging` and speaks
[length-prefixed JSON](https://developer.chrome.com/docs/extensions/develop/concepts/native-messaging)
over stdin/stdout. If a Phoenix window is already open, incoming links are
forwarded to it over the single-instance pipe (so the download appears in the
visible queue); otherwise the host process queues the download itself, then
exits when the browser closes the channel.

## Install (Chrome / Edge)

1. Build Phoenix: `cmake --build build` (creates `build\Phoenix.exe`) and register
   the `phoenix://` protocol once (`Phoenix.exe --register`).
2. Load the extension in the browser:
   - `chrome://extensions` -> **Developer mode** -> **Load unpacked** ->
     select `tools\native\extension`.
   - Copy the extension's ID shown on that page.
3. Register the host (needs no admin, writes HKCU only):

   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\native\install.ps1 -ExtensionId <your-extension-id>
   ```

   Pass `-PhoenixExe C:\path\to\Phoenix.exe` if it wasn't found automatically,
   and/or `-Browsers Edge` to register for Edge alone.

4. Restart the browser, then right-click any link -> **Send link to Phoenix**.

Uninstall:

```powershell
powershell -ExecutionPolicy Bypass -File tools\native\uninstall.ps1
```

## Protocol

Frames are `uint32` little-endian length + UTF-8 JSON (one object per frame).

Browser -> host:

- `{"type":"add","url":"https://...","fileName":"optional.pdf"}` — queue a download
- `{"type":"status"}` — list current queue items
- `{"type":"ping"}` — liveness probe

Host -> browser:

- `{"ok":true,"id":N}` or `{"ok":true,"method":"handoff"}` (add)
- `{"ok":true,"items":[{id,file,url,receivedBytes,totalBytes,speedBps,state,status}]}`
- `{"ok":true,"pong":true}`

## Self-test

`Phoenix.exe --self-test-native` exercises the frame protocol on a real pipe and
the JSON dispatcher (ping / add / status / unknown).

## Firefox

Not wired up yet. The host protocol is identical; Firefox-specific work is:
registering under `HKCU\Software\Mozilla\NativeMessagingHosts\com.phoenix.host`
pointing at `manifest.firefox.json`, and an `allowed_extensions` entry matching
the `browser_specific_settings.gecko.id` of the extension's manifest.