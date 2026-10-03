// wk2test.cpp -- WebKit2 on Onyx, without a window: ONE program that is the UI process and, started
// again by WebKit with a role argument, the web process and the network process (docs/08-WEBKIT-PORT.md,
// roadmap step 1). It loads a URL through WebKit's C API and the Onyx view, waits for the load to
// end, asks the page for its text, paints the view's backing store into a PNG, and prints what it
// saw and the time each stage took.
//
//   wk2test <url | /path/page.html> <out.png> [width height]
//
// Environment: WK2TEST_TIMEOUT (seconds, default 60), WK2TEST_SETTLE (milliseconds without a new
// display before the picture is taken, default 300), WK2TEST_SCROLL (wheel notches before the picture),
// WK2TEST_GPU=1 (the page drawn by Onyx's compositor instead of the software path: the picture then
// comes from its surface; on the bench gpucomp composites on the CPU).
// Built by tools/webkit/build-wk2test.sh against libWebKit.a (build-webkit.sh); run on the bench by
// tools/webkit/test-webkit.sh.
//
// MIT License
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "config.h" // (WebKit's: it is compiled with the command of one of WebKit's sources, prefix header and all)

#include <WebKit/WKAuxiliaryProcessOnyx.h>
#include <WebKit/WKContext.h>
#include <WebKit/WKContextConfigurationRef.h>
#include <WebKit/WKErrorRef.h>
#include <WebKit/WKGeometry.h>
#include <WebKit/WKPage.h>
#include <WebKit/WKPageConfigurationRef.h>
#include <WebKit/WKPageNavigationClient.h>
#include <WebKit/WKPagePrivateOnyx.h>
#include <WebKit/WKPreferencesRef.h>
#include <WebKit/WKRunLoop.h>
#include <WebKit/WKString.h>
#include <WebKit/WKType.h>
#include <WebKit/WKURL.h>
#include <WebKit/WKView.h>
#include <WebKit/WKWebsiteDataStoreRef.h>
#include <skia/core/SkImageInfo.h>
#include <skia/core/SkPixmap.h>
#include <skia/core/SkStream.h>
#include <skia/encode/SkPngEncoder.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <time.h>
#include <unistd.h>

static double now()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static double s_start;
static bool s_finished;
static bool s_failed;
static bool s_crashed;
static bool s_scriptDone;
static unsigned s_displays;
static double s_lastDisplay;
static double s_firstDisplay;
static std::string s_scriptResult;

static std::string toStdString(WKStringRef string)
{
    if (!string)
        return { };
    size_t size = WKStringGetMaximumUTF8CStringSize(string);
    std::string result(size, '\0');
    size_t used = WKStringGetUTF8CString(string, result.data(), size);
    result.resize(used ? used - 1 : 0);
    return result;
}

static void didFinishNavigation(WKPageRef, WKNavigationRef, WKTypeRef, const void*)
{
    s_finished = true;
    printf("loaded: %.0f ms\n", (now() - s_start) * 1000);
}

static void didStartProvisionalNavigation(WKPageRef, WKNavigationRef, WKTypeRef, const void*)
{
    printf("started: %.0f ms\n", (now() - s_start) * 1000);
}

static void didCommitNavigation(WKPageRef, WKNavigationRef, WKTypeRef, const void*)
{
    printf("committed: %.0f ms\n", (now() - s_start) * 1000);
}

static void printError(const char* what, WKErrorRef error)
{
    WKStringRef domain = error ? WKErrorCopyDomain(error) : nullptr;
    WKStringRef description = error ? WKErrorCopyLocalizedDescription(error) : nullptr;
    fprintf(stderr, "wk2test: %s: %s %d: %s\n", what, toStdString(domain).c_str(), error ? WKErrorGetErrorCode(error) : 0, toStdString(description).c_str());
    if (domain)
        WKRelease(domain);
    if (description)
        WKRelease(description);
}

static void didFailProvisionalNavigation(WKPageRef, WKNavigationRef, WKErrorRef error, WKTypeRef, const void*)
{
    printError("the load failed before it began (provisional)", error);
    s_failed = true;
}

static void didFailNavigation(WKPageRef, WKNavigationRef, WKErrorRef error, WKTypeRef, const void*)
{
    printError("the load failed", error);
    s_failed = true;
}

static void webProcessDidCrash(WKPageRef, const void*)
{
    fprintf(stderr, "wk2test: the web process ended\n");
    s_crashed = true;
}

static void setViewNeedsDisplay(WKViewRef, WKRect, const void*)
{
    s_lastDisplay = now();
    if (!s_displays++)
        s_firstDisplay = s_lastDisplay;
}

static void scriptResult(WKTypeRef result, WKErrorRef error, void*)
{
    if (!error && result && WKGetTypeID(result) == WKStringGetTypeID())
        s_scriptResult = toStdString(static_cast<WKStringRef>(result));
    else
        fprintf(stderr, "wk2test: the script gave no string\n");
    s_scriptDone = true;
}

// The run loop turned (the embedder owns the thread: WKRunLoop.h's second way) until done() or the time is up.
template<typename Done> static bool turnUntil(double deadline, Done done)
{
    while (!done()) {
        if (s_failed || s_crashed || now() > deadline)
            return false;
        WKRunLoopCycleMain();
        usleep(1000);
    }
    return true;
}

int main(int argc, char** argv)
{
    if (WKIsAuxiliaryProcessOnyx(argc, argv))
        return WKAuxiliaryProcessMainOnyx(argc, argv);

    if (argc < 3) {
        fprintf(stderr, "usage: wk2test <url | /path/page.html> <out.png> [width height]\n");
        return 2;
    }
    int width = argc > 4 ? atoi(argv[3]) : 800;
    int height = argc > 4 ? atoi(argv[4]) : 600;
    if (width < 1 || height < 1 || width > 8192 || height > 8192) {
        fprintf(stderr, "wk2test: bad size\n");
        return 2;
    }
    double timeout = getenv("WK2TEST_TIMEOUT") ? atof(getenv("WK2TEST_TIMEOUT")) : 60;
    double settle = (getenv("WK2TEST_SETTLE") ? atof(getenv("WK2TEST_SETTLE")) : 300) / 1000;
    std::string url = argv[1];
    if (url.find("://") == std::string::npos && url.rfind("about:", 0) && url.rfind("data:", 0))
        url = "file://" + url;

    s_start = now();
    WKRunLoopInitializeMain();

    WKContextConfigurationRef contextConfiguration = WKContextConfigurationCreate();
    WKContextRef context = WKContextCreateWithConfiguration(contextConfiguration);
    WKPageConfigurationRef pageConfiguration = WKPageConfigurationCreate();
    WKPageConfigurationSetContext(pageConfiguration, context);
    // Nothing kept on the card by a test.
    WKWebsiteDataStoreRef dataStore = WKWebsiteDataStoreCreateNonPersistentDataStore();
    WKPageConfigurationSetWebsiteDataStore(pageConfiguration, dataStore);
    if (const char* gpu = getenv("WK2TEST_GPU"); gpu && atoi(gpu)) {
        WKPreferencesRef preferences = WKPreferencesCreate();
        WKPreferencesSetCompositingEnabledOnyx(preferences, true);
        WKPageConfigurationSetPreferences(pageConfiguration, preferences);
        WKRelease(preferences);
        printf("compositing: on\n");
    }

    WKViewRef view = WKViewCreate(pageConfiguration);
    WKViewClientV0 viewClient;
    memset(&viewClient, 0, sizeof(viewClient));
    viewClient.base.version = 0;
    viewClient.setViewNeedsDisplay = setViewNeedsDisplay;
    WKViewSetViewClient(view, &viewClient.base);
    WKViewSetSize(view, WKSizeMake(width, height));
    WKViewSetVisible(view, true);
    WKViewSetActive(view, true);
    WKViewSetFocus(view, true);

    WKPageRef page = WKViewGetPage(view);
    WKPageNavigationClientV0 navigationClient;
    memset(&navigationClient, 0, sizeof(navigationClient));
    navigationClient.base.version = 0;
    navigationClient.didStartProvisionalNavigation = didStartProvisionalNavigation;
    navigationClient.didCommitNavigation = didCommitNavigation;
    navigationClient.didFinishNavigation = didFinishNavigation;
    navigationClient.didFailProvisionalNavigation = didFailProvisionalNavigation;
    navigationClient.didFailNavigation = didFailNavigation;
    navigationClient.webProcessDidCrash = webProcessDidCrash;
    WKPageSetPageNavigationClient(page, &navigationClient.base);
    printf("ready: %.0f ms\n", (now() - s_start) * 1000);
    setvbuf(stdout, nullptr, _IOLBF, 0);

    WKURLRef wkURL = WKURLCreateWithUTF8CString(url.c_str());
    WKPageLoadURL(page, wkURL);
    WKRelease(wkURL);

    double deadline = s_start + timeout;
    if (!turnUntil(deadline, [] { return s_finished; })) {
        fprintf(stderr, "wk2test: %s was not loaded (%s)\n", url.c_str(), s_failed ? "failed" : s_crashed ? "the web process ended" : "time out");
        return 1;
    }

    WKStringRef title = WKPageCopyTitle(page);
    printf("title: %s\n", toStdString(title).c_str());
    if (title)
        WKRelease(title);

    WKStringRef script = WKStringCreateWithUTF8CString(
        "String(document.getElementsByTagName('*').length) + '\\n' + (document.body ? document.body.innerText.replace(/\\s+/g, ' ').trim() : '')");
    WKPageEvaluateJavaScriptInMainFrame(page, script, nullptr, scriptResult);
    WKRelease(script);
    if (!turnUntil(deadline, [] { return s_scriptDone; })) {
        fprintf(stderr, "wk2test: the script did not answer\n");
        return 1;
    }
    size_t newline = s_scriptResult.find('\n');
    if (newline != std::string::npos) {
        printf("elements: %s\n", s_scriptResult.substr(0, newline).c_str());
        printf("text: %s\n", s_scriptResult.substr(newline + 1).c_str());
    }

    // WK2TEST_SCROLL=n: n wheel notches down over the page's middle, one every 100 ms (the scrolling path).
    if (const char* scroll = getenv("WK2TEST_SCROLL")) {
        int notches = atoi(scroll);
        for (int i = 0; i < notches; i++) {
            WKPoint p = WKPointMake(width / 2, height / 2);
            WKPageHandleWheelEvent(page, WKWheelEventMake(p, p, WKSizeMake(0, -120), WKSizeMake(0, -1), 0));
            double next = now() + 0.1;
            turnUntil(next + 1, [next] { return now() > next; });
        }
        printf("scrolled: %d notches\n", notches);
    }

    // The picture once the page has stopped drawing for a moment.
    if (!turnUntil(deadline, [settle] { return s_displays && now() - s_lastDisplay > settle; })) {
        fprintf(stderr, "wk2test: nothing was displayed (%u display requests)\n", s_displays);
        return 1;
    }
    printf("displayed: first after %.0f ms, %u requests\n", (s_firstDisplay - s_start) * 1000, s_displays);

    size_t stride = static_cast<size_t>(width) * 4;
    unsigned char* pixels = static_cast<unsigned char*>(calloc(height, stride));
    if (!pixels) {
        fprintf(stderr, "wk2test: no memory for the picture\n");
        return 1;
    }
    WKPagePaint(page, pixels, WKSizeMake(width, height), static_cast<uint32_t>(stride), WKRectMake(0, 0, width, height));

    SkPixmap pixmap(SkImageInfo::Make(width, height, kBGRA_8888_SkColorType, kPremul_SkAlphaType), pixels, stride);
    SkDynamicMemoryWStream stream;
    if (!SkPngEncoder::Encode(&stream, pixmap, { })) {
        fprintf(stderr, "wk2test: cannot encode the picture\n");
        return 1;
    }
    sk_sp<SkData> png = stream.detachAsData();
    FILE* file = fopen(argv[2], "wb");
    if (!file || fwrite(png->data(), 1, png->size(), file) != png->size()) {
        fprintf(stderr, "wk2test: cannot write %s\n", argv[2]);
        return 1;
    }
    fclose(file);
    printf("painted: %d x %d -> %s (%zu bytes)\n", width, height, argv[2], png->size());
    printf("total: %.0f ms\n", (now() - s_start) * 1000);
    fflush(stdout);

    // The view released: its page closes, the web and network processes see their connections end.
    free(pixels);
    // The page closed first, while everything still exists; the run loop turned so that the web process's
    // answers arrive; then the objects released.
    WKPageClose(page);
    double closed = now() + 0.3;
    while (now() < closed) {
        WKRunLoopCycleMain();
        usleep(1000);
    }
    WKRelease(view);
    WKRelease(pageConfiguration);
    WKRelease(dataStore);
    WKRelease(context);
    WKRelease(contextConfiguration);
    double end = now() + 0.5;
    while (now() < end) {
        WKRunLoopCycleMain();
        usleep(1000);
    }
    return 0;
}
