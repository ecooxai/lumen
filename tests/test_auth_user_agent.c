#include "../src/net/net.h"
#include <assert.h>

int main(void) {
    for (int mode = 0; mode < UA_COUNT; mode++) {
        net_set_user_agent(mode);
        const char *ua = net_user_agent_for_url("https://accounts.google.com/v3/signin/identifier");
        assert(strstr(ua, "Lumen/"));
        assert(!strstr(ua, "Chrome/") && !strstr(ua, "Firefox/"));
        assert(strstr(net_user_agent_for_url("https://ACCOUNTS.GOOGLE.COM/"), "Lumen/"));
        assert(net_user_agent_for_url("https://accounts.google.com.example.org/") == g_user_agent);
        assert(net_user_agent_for_url("https://chatgpt.com/") == g_user_agent);
        assert(net_user_agent_for_url(NULL) == g_user_agent);
    }
    puts("Authentication browser identity passed");
}
