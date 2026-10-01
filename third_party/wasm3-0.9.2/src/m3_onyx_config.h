//
//  m3_onyx_config.h
//
//  Onyx: the settings of the wasm3 build NetSurf links (third_party/wasm3-0.9.2/README.onyx).
//  Included first by m3_config.h, so the library and the code that reads its structures
//  (NetSurf's quickjs/qjs_wasm.c includes m3_env.h) see the same layout.
//

#ifndef m3_onyx_config_h
#define m3_onyx_config_h

// The stack switching proposal, the snapshots built on it and the typed function
// references it needs: nothing a web page's module uses yet, and the largest parts of
// the interpreter. Gas metering is not needed either (the browser bounds a runaway
// module through m3_Yield: qjs_wasm.c).
#define d_m3HasTypedRefs            0
#define d_m3HasStackSwitching       0
#define d_m3HasSnapshots            0
#define d_m3HasGasMetering          0

// No guarded memories: on the PC bench they would reserve 8 GiB of address space per
// memory and take the fault signal; the Pi build (no mmap) never had them. The bounds
// are checked on each access instead, the same on both.
#define d_m3GuardedMemory           0

// The native stack Wasm code may use below the point it was called from: the NetSurf
// app has 8 MB (its app.txt), of which QuickJS may take 4 MB; a module's recursion
// traps (a RangeError in JavaScript) past 1.5 MB.
#define d_m3MaxNativeStack          (1536 * 1024)

// A linear memory is at most 1 GiB here (the page's runtime is capped lower still:
// m3_SetResourceLimit in qjs_wasm.c).
#define d_m3MaxLinearMemoryPages    16384

#endif // m3_onyx_config_h
