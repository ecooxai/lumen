#include "../src/net/net.h"
#include <assert.h>

/* Sign-in pages of Google, Apple and Microsoft see Firefox whatever the user agent setting;
   other sites keep the setting. */
int main(void) {
    static const char *const logins[] = {
        "https://accounts.google.com/v3/signin/identifier", "https://ACCOUNTS.GOOGLE.COM/",
        "https://accounts.youtube.com/accounts/SetSID", "https://appleid.apple.com/sign-in",
        "https://idmsa.apple.com/appleauth/auth", "https://login.microsoftonline.com/common/oauth2/v2.0/authorize",
        "https://login.live.com/login.srf",
    };
    for (int mode = 0; mode < UA_COUNT; mode++) {
        net_set_user_agent(mode);
        for (size_t i = 0; i < sizeof logins / sizeof *logins; i++) {
            const char *ua = net_user_agent_for_url(logins[i]);
            assert(strstr(ua, " Firefox/150.0") && strstr(ua, "rv:150.0) Gecko/"));
            assert(!strstr(ua, "Chrome/") && !strstr(ua, "Lumen/") && !strstr(ua, "Safari/"));
        }
        assert(net_user_agent_for_url("https://accounts.google.com.example.org/") == g_user_agent);
        assert(net_user_agent_for_url("https://google.com/") == g_user_agent);
        assert(net_user_agent_for_url("https://www.apple.com/") == g_user_agent);
        assert(net_user_agent_for_url("https://chatgpt.com/") == g_user_agent);
        assert(net_user_agent_for_url(NULL) == g_user_agent);
    }
    net_set_desktop_host("google.com", true);   /* Desktop site: a desktop Firefox */
    const char *ua = net_user_agent_for_url("https://accounts.google.com/");
    assert(strstr(ua, "Firefox/") && !strstr(ua, "Android"));
    assert(net_login_host("LOGIN.LIVE.COM") && !net_login_host("live.com") && !net_login_host(NULL));
    puts("Authentication browser identity passed");
}
