// wctest.cpp -- WebCore's smoke test for the Onyx port (step 2 of the WebKit port,
// docs/08-WEBKIT-PORT.md): one page, no browser around it. It loads an HTML file into a WebCore
// Page (the "empty" clients: no window, no network), lays it out at a given width and height,
// paints it with Skia on the CPU into a bitmap and writes that as a PNG:
//
//   wctest <page.html> <out.png> [width height]        (default 800 600)
//   WCTEST_SCRIPTS=0 wctest ...                         JavaScript off
//
// and prints the document's title, the number of elements and the body's text, which the bench
// (tools/webkit/test-webcore.sh) checks. No subresources: a page's style sheets, scripts and
// images must be inline (data: URLs). Built by tools/webkit/build-wctest.sh with the flags of
// the WebCore build; it includes WebCore's private headers, and so follows that revision.
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see fetch.sh).
#include "config.h"

#include "BlobRegistry.h"
#include "CommonVM.h"
#include "DocumentInlines.h"
#include "DocumentLoader.h"
#include "DocumentView.h"
#include "DocumentWriter.h"
#include "ElementInlines.h"
#include "EmptyClients.h"
#include "FrameLoader.h"
#include "GraphicsContextSkia.h"
#include "HTMLBodyElement.h"
#include "HTMLElement.h"
#include "LocalFrame.h"
#include "LocalFrameInlines.h"
#include "LoaderStrategy.h"
#include "LocalFrameView.h"
#include "Page.h"
#include "PageConfiguration.h"
#include "PlatformStrategies.h"
#include "ResourceError.h"
#include "ResourceResponse.h"
#include "SubresourceLoader.h"
#include "ProcessWarming.h"
#include "Settings.h"
#include "SharedBuffer.h"
#include "TypedElementDescendantIteratorInlines.h"
#include <JavaScriptCore/InitializeThreading.h>
#include <pal/SessionID.h>
#include <stdio.h>
#include <stdlib.h>
#include <wtf/FileSystem.h>
#include <wtf/MainThread.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/RunLoop.h>

WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_BEGIN
#include <skia/core/SkCanvas.h>
#include <skia/core/SkPixmap.h>
#include <skia/core/SkStream.h>
#include <skia/core/SkSurface.h>
#include <skia/encode/SkPngEncoder.h>
WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_END

using namespace WebCore;

namespace {

// A loader that loads nothing: the page is given whole, its subresources (none, or data: URLs)
// are never asked from the network. WebCore still wants one to register with.
class TestLoaderStrategy final : public LoaderStrategy {
    void loadResource(LocalFrame&, CachedResource&, ResourceRequest&&, const ResourceLoaderOptions&, CompletionHandler<void(RefPtr<SubresourceLoader>&&)>&& completionHandler) final { completionHandler(nullptr); }
    void loadResourceSynchronously(FrameLoader&, ResourceLoaderIdentifier, const ResourceRequest& request, ClientCredentialPolicy, const FetchOptions&, const HTTPHeaderMap&, ResourceError& error, ResourceResponse&, Vector<uint8_t>&) final { error = cannotShowURLError(request); }
    void pageLoadCompleted(Page&) final { }
    void browsingContextRemoved(LocalFrame&) final { }
    void remove(ResourceLoader*) final { }
    void setDefersLoading(ResourceLoader&, bool) final { }
    void crossOriginRedirectReceived(ResourceLoader*, const URL&) final { }
    void servePendingRequests(ResourceLoadPriority) final { }
    void suspendPendingRequests() final { }
    void resumePendingRequests() final { }
    void preconnectTo(FrameLoader&, ResourceRequest&&, StoredCredentialsPolicy, ShouldPreconnectAsFirstParty, PreconnectCompletionHandler&&) final { }
    void setCaptureExtraNetworkLoadMetricsEnabled(bool) final { }
    bool isOnLine() const final { return false; }
    void addOnlineStateChangeListener(Function<void(bool)>&&) final { }
    void isResourceLoadFinished(CachedResource&, CompletionHandler<void(bool)>&& callback) final { callback(true); }

    static ResourceError error(const URL& url, ASCIILiteral what) { return ResourceError { "wctest"_s, 1, url, what }; }
    ResourceError cancelledError(const ResourceRequest& request) const final { return ResourceError { "wctest"_s, 2, request.url(), "cancelled"_s, ResourceError::Type::Cancellation }; }
    ResourceError blockedError(const ResourceRequest& request) const final { return error(request.url(), "blocked"_s); }
    bool isBlockedError(const ResourceError&) const final { return false; }
    ResourceError blockedByContentBlockerError(const ResourceRequest& request) const final { return error(request.url(), "blocked by a content blocker"_s); }
    ResourceError cannotShowURLError(const ResourceRequest& request) const final { return error(request.url(), "no network in wctest"_s); }
    ResourceError interruptedForPolicyChangeError(const ResourceRequest& request) const final { return error(request.url(), "interrupted"_s); }
#if ENABLE(CONTENT_FILTERING)
    ResourceError blockedByContentFilterError(const ResourceRequest& request) const final { return error(request.url(), "blocked by a content filter"_s); }
#endif
    ResourceError cannotShowMIMETypeError(const ResourceResponse& response) const final { return error(response.url(), "cannot show this type"_s); }
    ResourceError fileDoesNotExistError(const ResourceResponse& response) const final { return error(response.url(), "no such file"_s); }
    ResourceError httpsUpgradeRedirectLoopError(const ResourceRequest& request) const final { return error(request.url(), "redirect loop"_s); }
    ResourceError httpNavigationWithHTTPSOnlyError(const ResourceRequest& request) const final { return error(request.url(), "HTTPS only"_s); }
    bool isHttpNavigationWithHTTPSOnlyError(const ResourceError&) const final { return false; }
    ResourceError pluginWillHandleLoadError(const ResourceResponse& response) const final { return error(response.url(), "plug-in"_s); }
};

// No pasteboard or media strategy: this test has no clipboard or media.
class TestStrategies final : public PlatformStrategies {
    LoaderStrategy* createLoaderStrategy() final
    {
        static NeverDestroyed<TestLoaderStrategy> loaderStrategy;
        return &loaderStrategy.get();
    }
    PasteboardStrategy* createPasteboardStrategy() final { return nullptr; }
    MediaStrategy* createMediaStrategy() final { return nullptr; }
    BlobRegistry* createBlobRegistry() final { return nullptr; }
};

}

int main(int argc, char** argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: wctest <page.html> <out.png> [width height]\n");
        return 2;
    }
    int width = argc > 4 ? atoi(argv[3]) : 800;
    int height = argc > 4 ? atoi(argv[4]) : 600;
    const char* scripts = getenv("WCTEST_SCRIPTS");

    WTF::initializeMainThread();
    JSC::initialize();
    static NeverDestroyed<TestStrategies> strategies;
    setPlatformStrategies(&strategies.get());
    ProcessWarming::initializeNames();

    auto contents = FileSystem::readEntireFile(String::fromUTF8(argv[1]));
    if (!contents) {
        fprintf(stderr, "wctest: cannot read %s\n", argv[1]);
        return 2;
    }

    auto pageConfiguration = pageConfigurationWithEmptyClients(std::nullopt, PAL::SessionID::defaultSessionID());
    // (the "empty clients" page is made for SVG images: fully sandboxed, scripts refused)
    if (auto* parameters = std::get_if<PageConfiguration::LocalMainFrameCreationParameters>(&pageConfiguration.mainFrameCreationParameters))
        parameters->effectiveSandboxFlags = { };
    Ref page = Page::create(WTF::move(pageConfiguration));
    page->settings().setScriptEnabled(!scripts || atoi(scripts));
    page->settings().setAcceleratedCompositingEnabled(false);

    RefPtr frame = page->localMainFrame();
    if (!frame) {
        fprintf(stderr, "wctest: no main frame\n");
        return 1;
    }
    frame->setView(LocalFrameView::create(*frame));
    frame->init();
    RefPtr view = frame->view();
    view->resize(IntSize(width, height));

    RefPtr documentLoader = frame->loader().activeDocumentLoader();
    if (!documentLoader) {
        fprintf(stderr, "wctest: no document loader\n");
        return 1;
    }
    documentLoader->writer().setMIMEType("text/html"_s);
    documentLoader->writer().begin(URL { "file:///wctest.html"_s });
    documentLoader->writer().addData(SharedBuffer::create(WTF::move(*contents)));
    documentLoader->writer().end();

    RefPtr document = frame->document();
    if (!document) {
        fprintf(stderr, "wctest: no document\n");
        return 1;
    }
    document->updateLayoutIgnorePendingStylesheets();
    view->updateLayoutAndStyleIfNeededRecursive();

    unsigned elements = 0;
    for ([[maybe_unused]] auto& element : descendantsOfType<Element>(*document))
        elements++;
    printf("title: %s\n", document->title().utf8().data());
    printf("elements: %u\n", elements);
    if (RefPtr body = document->body())
        printf("text: %s\n", body->innerText().simplifyWhiteSpace(isASCIIWhitespace).utf8().data());

    auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(width, height));
    if (!surface) {
        fprintf(stderr, "wctest: no surface\n");
        return 1;
    }
    surface->getCanvas()->clear(SK_ColorWHITE);
    {
        GraphicsContextSkia context(*surface->getCanvas(), RenderingMode::Unaccelerated, RenderingPurpose::Unspecified);
        view->paint(context, IntRect(0, 0, width, height));
    }

    SkPixmap pixmap;
    SkDynamicMemoryWStream stream;
    if (!surface->peekPixels(&pixmap) || !SkPngEncoder::Encode(&stream, pixmap, { })) {
        fprintf(stderr, "wctest: cannot encode the picture\n");
        return 1;
    }
    auto png = stream.detachAsData();
    FILE* file = fopen(argv[2], "wb");
    if (!file || fwrite(png->data(), 1, png->size(), file) != png->size()) {
        fprintf(stderr, "wctest: cannot write %s\n", argv[2]);
        return 1;
    }
    fclose(file);
    printf("painted: %d x %d -> %s (%zu bytes)\n", width, height, argv[2], png->size());
    fflush(stdout);
    // (No teardown: the process ends here, as a web process does.)
    _exit(0);
}
