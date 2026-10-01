Jet Browser for Windows
=======================

Jet Browser, the web browser of Onyx -- the same program as on the Raspberry Pi (the same sources: NetSurf
with Onyx's changes, QuickJS, libcss, libdom, FreeType, PlutoVG / PlutoSVG, wasm3, mbedTLS, nghttp2, zstd,
brotli; its window, toolbar and menus are Onyx's wtk), built for Windows to try sites on a PC and compare
what it does with the Pi: the same layout, the same scripts, the same network code, the same caches. Only the
machine is faster (and QuickJS runs without the Pi's JIT, which is for ARM processors).

Needs: Windows 10 or 11 (64-bit). Nothing to install: keep the folder whole and double-click Jet.exe.

The folder
  Jet.exe          the browser
  res\             what the Pi has in SD:/res: Choices (the options -- see below), Messages, the style
                   sheets, ca-bundle (the trusted root certificates), fonts\ (DejaVu, Liberation, Selawik,
                   Gelasio: the fonts the pages get, as on the Pi)
  data\            the browser's own folder (on the Pi SD:/apps/jet.app/), made and written by it:
                     Cookies, History        your cookies (logins) and the pages visited
                     jet.ini                 the User-Agent sent (the whole browser, or site by site)
                     site-modes              each site's version chosen with the blue pill (below)
                     TLSSessions, HTTP1Hosts the TLS sessions and servers remembered between launches
                     ram\jet\cache, ram\jet\jscache   the disk cache and the scripts' code cache (on the Pi
                                             they are in RAM:, lost at a restart; here kept until deleted)
                     jet.log                 the log of the last launch (below)
                   Delete data\ to start afresh (keep jet.ini if you edited it).
  fonts\, etc\          the toolkit's font and theme

Using it
  Type an address in the bar (or Ctrl+L / F6 to go there) and Enter. Back / Forward: the buttons or
  Alt+Left / Alt+Right; Reload F5 (or Ctrl+R); Stop Esc; History Ctrl+H; the menus File, Navigate, Help.
  Left of the address, the padlock: green for a secure (https) page, red for one opened past a certificate
  warning ("Proceed"), grey for http; click it to see the certificate. Right of it, the blue pill: the
  version of the site -- Standard (Jet Browser's own User-Agent), Mobile (Chrome on Android), Desktop
  (Chrome on Windows); click it to choose (the page reloads; remembered per site). From the address bar,
  Tab goes to the pill, Shift+Tab to the padlock.
  The window resizes and maximises as any; the mouse wheel scrolls. Drop an .html file on the window, or
  "Open with" Jet.exe, or give an address on the command line:
      Jet.exe https://css3test.com/
  A local file: file:///C:/Users/me/page.html (or Jet.exe C:\Users\me\page.html).
  Choices (res\Choices, read at start) as on the Pi: enable_javascript, foreground_images, script_timeout,
  user_agent, accept_language, gpu_compositing (no GPU here: the processor composites, as a Pi whose GPU
  failed its test), cache_on_card:1 (the caches in data\cache and data\jscache instead of data\ram).

The log and the timings (to compare with the Pi's kernel log)
  Everything the browser prints goes to data\jet.log (a new one at each launch): NetSurf's messages, the
  files and pages fetched, the TLS refusals ("ONYX-TLS refused ...").
  Switches -- on the command line, or as an EMPTY file of that name beside Jet.exe:
      --perf      (file "perf")      the timings: ONYX-PERF lines -- the downloads (dns, tcp, tls, ttfb),
                                     the scripts' runs, styles, layout, redraws, the frames painted
      --jsdebug   (file "jsdebug")   the scripts' errors and their console.log / console.error
      --netdebug  (file "netdebug")  each connection, and the HTTP heads and HTTP/2 frames
      --console   (file "console")   the log in a console window instead of data\jet.log
      --maximised                    the window opens maximised
      --sharp                        on a scaled display (125 %, 150 %...): the page's pixels 1:1 (smaller,
                                     not blurred); otherwise Windows scales the window up
      NS_xxx=value                   any of the bench's variables (docs/06 §1, §9), e.g. NS_SCRIPT_TIMEOUT=10
  On the Pi the same: the files perf / jsdebug in SD:/apps/jet.app/, the lines in the kernel log.
  The files perf and jsdebug in data\ work too (as on the Pi in its folder).
  For example, a shortcut to: Jet.exe --perf --jsdebug --console https://css3test.com/

HTTPS
  mbedTLS, as on the Pi, with the same trusted roots (res\ca-bundle, NetSurf's list -- not Windows' store):
  a site the Pi refuses is refused here too (the certificate error page); TLS 1.3 / 1.2, HTTP/2 and
  HTTP/1.1, brotli / zstd / gzip. Behind a company proxy that re-signs HTTPS, add its root certificate
  (PEM) at the end of res\ca-bundle.

Not here: the Pi's JIT (QuickJS interprets), the GPU compositor (the processor does it), a proxy setting
(direct connections only). Windows Defender SmartScreen may ask once
("More info" > "Run anyway"): the program is not signed.

Built on Linux from the Onyx repository: sh pc/Jet/build.sh (MinGW-w64; see its header and docs/06).
