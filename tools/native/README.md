# Phoenix Native Messaging Host (browser integration)

Hands download links from Chrome/Edge/Firefox straight to Phoenix — the intended
replacement for clipboard scraping. When you right-click a link and pick
"Send link to Phoenix", the extension asks the native host to download it.

## How it works

Chrome/Edge launch the host by calling `Phoenix.exe chrome-extension://<id>/`
— the calling extension's origin is the first argument; no extra flags are
needed (the browser doesn't support a manifest `"args"` field). The native host
detects the `chrome-extension://` prefix and switches to native-messaging mode,
speaking length-prefixed JSON over stdin/stdout. When an "add" request arrives:
- If a Phoenix window is already open, it is handed off over the single-instance
  pipe so the download appears in the visible queue.
- Otherwise the host launches a fresh Phoenix GUI carrying the `phoenix://`
  link on its command line — the window opens with the download already queued.

The host process exits when the browser closes the channel.

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

4. Restart the browser, then either:
   - right-click any link -> **Send link to Phoenix**, or
   - right-click the extension's toolbar icon -> **Catch downloads
     automatically**. Once enabled, a normal click on a download link is
     intercepted and handed to Phoenix instead of the browser. The toggle is
     off by default and remembered per browser profile.

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
the JSON dispatcher (ping / add / status / unknown). `Phoenix.exe
--native-messaging` (no browser) is kept as a manual/testing alias for the same
mode Chrome triggers via the origin argument.

## Firefox

Not wired up yet. The host protocol is identical; Firefox-specific work is:
registering under `HKCU\Software\Mozilla\NativeMessagingHosts\com.phoenix.host`
pointing at `manifest.firefox.json`, and an `allowed_extensions` entry matching
the `browser_specific_settings.gecko.id` of the extension's manifest.