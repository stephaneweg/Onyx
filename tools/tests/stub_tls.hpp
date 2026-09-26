// stub_tls.hpp -- host test stand-in for tls/onyx_tls.hpp (plain FTP only: TLS fails)
namespace onyx_tls {
struct Session { int dummy; };
static inline int start (Session &, int, const char *) { return -1; }
static inline int send (Session &, const void *, int) { return -1; }
static inline int recv (Session &, void *, int) { return -1; }
static inline void stop (Session &) {}
}
