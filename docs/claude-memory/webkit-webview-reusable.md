---
name: webkit-webview-reusable
description: "The WebKit web view should be a separate, embeddable component (host window + attached web view), reusable by the Mail client — the user's direction"
metadata:
  node_type: memory
  type: project
  originSessionId: 001d9be7-ac4e-42a7-a2f2-d967f5762ebd
  modified: 2026-10-02T20:54:23.994Z
---

The user's direction for the WebKit browser's architecture (2026-10-02, while planning step 3):

- **Start-up**: look at a lazy, file-backed ELF loader in the kernel (pages read on first touch,
  code pages shared between processes); and split **a small front end (the host window)** from
  **the web view (the page and the toolbar)**: the host appears at once and starts the web view,
  which attaches to its window "like an applet". The split was first given as the fallback if lazy
  loading is not possible; then the user said that if both are feasible, do both.
- **The web view is to be reusable**: the user expects the **Mail client** to embed it (HTML mail).

**Why:** an 80 MB WebCore test program takes 5–6 s to start on the Pi, mostly loading; the user
wants the window to seem instant and one web engine shared by several apps.

**How to apply:** design step 3's embedding as a stand-alone web-view service with a small host
API (load a URL or given HTML, back / forward / reload, title and URL changes, a navigation policy
hook), an optional toolbar (the browser wants it, Mail does not), and host-controlled restrictions
(Mail: no remote content until allowed, scripts off, links opened in the browser). Do not bake the
browser's UI into the web view. Check how a process draws into another's window on Onyx (an
existing applet mechanism, or a canvas in v76 shared memory) and how events reach it. See
[[webkit-browser-no-tabs]] and [[webkit-graphics-roadmap]].
