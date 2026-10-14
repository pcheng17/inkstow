# Webview shell bridge (#91, C++ component)

`inkstow-webview` is a separate executable; `inkstow` and its Qt tests remain unchanged. Its sole trusted UI origin is `src/webview/index.html`, embedded at CMake configure time (reconfigure after editing it). `set_html` needs no network or runtime asset paths. The current page is deliberately only a placeholder, **not** an editor. The frontend owner may replace it with a self-contained locally built HTML/JS/CSS bundle and adjust the CSP for that bundle; it must not fetch a remote editor or navigate untrusted pages. The shell does not provide an open-folder picker yet: supply an absolute UTF-8 vault folder string from the frontend. Use disposable vaults only.

## Contract v1

The native `webview_bind` exposes `window.inkstowRequest(request)` returning a Promise resolving to an object. The binding supplies a JSON array containing exactly one object to `Bridge::handle`; never call `Bridge::handle` with the bare request. Requests are serialized (single window/UI thread), with exact fields and case-sensitive operation names:

| operation | additional field | result |
| --- | --- | --- |
| `open` | `folder`: absolute UTF-8 folder path | opens vault; resets selected note; returns sorted list |
| `list` | none | returns cached note list (not a filesystem refresh) |
| `select` | `path`: vault-relative UTF-8 Markdown path from list | loads decoded editor text |
| `edit` | `content`: complete new UTF-8 editor text | updates unsaved text; no disk write |
| `save` | none | writes selected note through portable `Session` |

Every successful response is `{ "ok": true, "notes": ["path.md"], "path": "path.md", "content": "text", "dirty": false }`. `path` and `content` are empty when no note is selected. Failed operations normally return the same snapshot with `ok: false` and a human-readable `error`; failed open/select/save preserve selection and edits. Invalid JSON/UTF-8 may yield only `{ "ok": false, "error": "Invalid JSON or UTF-8 in request or response." }`. Always check `ok` and display `error` visibly, never discard unsaved `content` on error. Errors resolve the Promise rather than reject it. An `edit` must precede save; editing does not merge incremental deltas. A successful `save` clears `dirty`. Opening or selecting while dirty is refused by `Session`.

Unknown fields, missing/wrong-type fields and unknown operations fail; the complete binding argument JSON is capped at 8 MiB, `content` at 4 MiB, and path/folder at 4096 UTF-8 bytes. The portable core validates paths, note availability, UTF-8, BOM preservation and guarded saves. There is no bridge operation for filesystem creation, rename, delete, folder picking, conflict resolution or file watching. List is a snapshot from the last successful open. These constraints are deliberate scope limits, not silent alternate implementations.

Dependencies: CMake FetchContent retrieves checksum-pinned `webview/webview` 0.12.0 (SHA256 `e2c8d0bed3fcd13e624074448b7d3b1d0b0d03546a15839f17ee0013f2cba1db`) and `nlohmann/json` 3.12.0 (SHA256 `4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187`) at first configure; subsequent builds use the CMake cache, and a clean offline configure needs pre-populated FetchContent archives. Linux uses system WebKitGTK 4.1/GTK3; macOS uses WKWebView, Windows WebView2 (Evergreen runtime required). Only Linux build/test is verified in this component; macOS and Windows packaging and graphical smoke are outstanding. Neither the placeholder page nor the bridge is a full #91 acceptance test.
