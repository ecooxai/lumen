#include <cstdio>
#include <string>
#include "../src/js/broadcast.h"

// Pairwise equivalence tests do not depend on the private key's wire format.
// Without libpsl, distinct canonical hosts must remain conservatively isolated.
struct SitePair {
  const char *name, *left, *right;
  bool psl_equal, fallback_equal;
};
int main() {
  const SitePair cases[] = {
    {"ordinary subdomains", "https://a.example.com", "https://b.example.com", true, false},
    {"registrable parent", "https://example.com", "https://a.example.com", true, false},
    {"different registrable domains", "https://example.com", "https://example.net", false, false},
    {"schemeful separation", "http://example.com", "https://example.com", false, false},
    {"default HTTPS port", "https://example.com", "https://example.com:443", true, true},
    {"nondefault ports do not change site", "https://example.com:8443", "https://example.com:9443", true, true},
    {"ASCII host case", "https://EXAMPLE.COM", "https://example.com", true, true},
    {"co.uk subdomains", "https://a.example.co.uk", "https://b.example.co.uk", true, false},
    {"co.uk tenants", "https://a.co.uk", "https://b.co.uk", false, false},
    {"PRIVATE github.io tenants", "https://alice.github.io", "https://bob.github.io", false, false},
    {"PRIVATE github.io same tenant", "https://a.alice.github.io", "https://b.alice.github.io", true, false},
    {"PRIVATE blogspot tenants", "https://alice.blogspot.com", "https://bob.blogspot.com", false, false},
    {"PRIVATE blogspot same tenant", "https://a.alice.blogspot.com", "https://b.alice.blogspot.com", true, false},
    {"ck wildcard tenant boundary", "https://a.foo.ck", "https://b.foo.ck", false, false},
    {"ck wildcard same tenant", "https://a.foo.ck", "https://b.a.foo.ck", true, false},
    {"ck exception", "https://www.ck", "https://a.www.ck", true, false},
    {"kawasaki wildcard boundary", "https://a.foo.kawasaki.jp", "https://b.foo.kawasaki.jp", false, false},
    {"kawasaki city exception", "https://a.city.kawasaki.jp", "https://b.city.kawasaki.jp", true, false},
    {"dotted subdomains", "https://a.example.com.", "https://b.example.com.", true, false},
    {"dotted and undotted remain distinct", "https://example.com.", "https://example.com", false, false},
    {"IDNA unicode and A-label", "https://b\xc3\xbc" "cher.example", "https://xn--bcher-kva.example", true, true},
    {"IDNA subdomains", "https://a.xn--bcher-kva.example", "https://b.xn--bcher-kva.example", true, false},
    {"IPv4 ports", "http://127.0.0.1:8000", "http://127.0.0.1:9000", true, true},
    {"IPv4 address separation", "http://127.0.0.1", "http://127.0.0.2", false, false},
    {"IPv6 canonical compression", "http://[::1]", "http://[0:0:0:0:0:0:0:1]", true, true},
    {"IPv6 ports", "http://[::1]:8000", "http://[::1]:9000", true, true},
    {"IPv6 address separation", "http://[::1]", "http://[::2]", false, false},
    {"localhost exact host", "http://localhost:8000", "http://localhost:9000", true, true},
    {"unknown suffix canonical host", "https://same.exampleinvalid", "https://same.exampleinvalid:8443", true, true}
  };
  const bool psl = js_broadcast_has_psl();
#ifdef LUMEN_EXPECT_PSL
  if (!psl) { std::fprintf(stderr, "FAIL: configured native build requires the embedded PSL to load\n"); return 1; }
#endif
  int passed = 0, failed = 0;
  for (const auto &c : cases) {
    const auto left = js_broadcast_site_key(c.left, "opaque-left");
    const auto right = js_broadcast_site_key(c.right, "opaque-right");
    const bool expected = psl ? c.psl_equal : c.fallback_equal;
    if (!left.empty() && !right.empty() && ((left == right) == expected)) ++passed;
    else { std::fprintf(stderr, "FAIL site invariant: %s (PSL=%d, expected equal=%d)\n", c.name, psl, expected); ++failed; }
  }
  const auto opaque_a = js_broadcast_site_key("null", "opaque-a");
  const auto opaque_a_again = js_broadcast_site_key("null", "opaque-a");
  const auto opaque_b = js_broadcast_site_key("null", "opaque-b");
  if (!opaque_a.empty() && opaque_a == opaque_a_again && opaque_a != opaque_b) ++passed;
  else { std::fprintf(stderr, "FAIL opaque nonce identity\n"); ++failed; }
  std::printf("BroadcastChannel site invariants: %d passed, %d failed; PSL %s\n", passed, failed, psl ? "available" : "unavailable: conservative host-only fallback");
  return failed ? 1 : 0;
}
