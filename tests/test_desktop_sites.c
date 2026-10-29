/* "Desktop site" list: "*" covers every site, a site turned off under "*" is excluded on its own
 * (and its subdomains), turning it on again lifts that, and the saved entries restore as written. */
#include "../src/net/net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

int main(void) {
    char saved[512];
    CHECK(!net_desktop_host("example.com"));
    net_set_desktop_host("google.com", true);
    CHECK(net_desktop_host("www.google.com") && net_desktop_host("mail.google.com") && !net_desktop_host("example.com"));
    net_set_desktop_host("mail.google.com", false);   /* without "*": off drops the covering site */
    CHECK(!net_desktop_host("google.com"));
    net_set_desktop_host("*", true);
    CHECK(net_desktop_host("example.com") && net_desktop_host("*"));
    net_set_desktop_host("www.youtube.com", false);
    CHECK(!net_desktop_host("youtube.com") && !net_desktop_host("m.youtube.com") && net_desktop_host("example.com") && net_desktop_host("*"));
    net_desktop_hosts(saved, sizeof saved);
    fprintf(stderr, "saved: %s\n", saved);
    CHECK(!strcmp(saved, "* -youtube.com"));
    net_set_desktop_host("m.youtube.com", true);   /* on lifts the exclusion over it */
    CHECK(net_desktop_host("youtube.com"));
    net_set_desktop_host("youtube.com", false);
    net_set_desktop_host("*", false);   /* all off: no sites, no exclusions left */
    net_desktop_hosts(saved, sizeof saved);
    CHECK(!*saved && !net_desktop_host("example.com"));
    net_desktop_restore("*"); net_desktop_restore("-chatgpt.com");
    CHECK(net_desktop_host("example.com") && !net_desktop_host("chatgpt.com"));
    return 0;
}
