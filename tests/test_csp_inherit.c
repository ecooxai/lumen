/* Inherited local-document policy retains its original self origin while each
 * later child policy uses the same CSP-list self-origin, not its DOM origin. */
#include "../src/net/net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, failures;
#define CHECK(name, cond) do { ++checks; if (!(cond)) { ++failures; printf("FAIL %s\n", name); } } while (0)
int main(void) {
    static const char *origins[] = {"http://source.test/path", "http://source.test:8080/path", "https://source.test/path",
        "https://source.test:8443/path", "http://[::1]/path", "https://[::1]:8443/path", "ftp://source.test/path"};
    static const char *targets[] = {"http://source.test/", "http://source.test:8080/", "http://source.test:443/",
        "https://source.test/", "https://source.test:80/", "https://source.test:8080/", "https://source.test:8443/",
        "http://other.test/", "https://source.test.evil/", "http://[::1]/", "https://[::1]/", "https://[::1]:80/",
        "https://[::1]:8443/", "data:text/plain,hello", "ftp://source.test/", "file:///source.test"};
    for (size_t i=0; i<sizeof(origins)/sizeof(*origins); ++i) {
        const char *policy="script-src 'self'; connect-src 'self'; img-src 'self' https://images.test";
        char *bound=csp_inherit(policy,origins[i]); CHECK("a real tuple policy is copied",bound != NULL);
        for(size_t j=0;j<sizeof(targets)/sizeof(*targets);++j) {
            CHECK("connect-src self origin semantics survive opaque navigation",
                csp_allows_url(policy,origins[i],targets[j],"connect-src") == csp_allows_url(bound,"data:text/html,child",targets[j],"connect-src"));
            CHECK("script-src self origin semantics survive opaque navigation",
                csp_allows_script(policy,origins[i],targets[j],NULL,NULL,0,true) == csp_allows_script(bound,"data:text/html,child",targets[j],NULL,NULL,0,true));
        }
        free(bound);
    }
    char *bound=csp_inherit("script-src 'self'; connect-src 'self'\nscript-src 'none'","https://source.test");
    CHECK("multiple policy intersection still blocks script",!csp_allows_script(bound,"data:text/html,child","https://source.test/x.js",NULL,NULL,0,true));
    CHECK("independent connect policy retains source origin",csp_allows_url(bound,"data:text/html,child","https://source.test/","connect-src"));
    char *meta=csp_inherit("connect-src 'self'","https://source.test");bound=csp_append(bound,meta);free(meta);
    CHECK("new child meta policy uses the inherited list self origin",csp_allows_url(bound,"data:text/html,child","https://source.test/","connect-src"));
    bound=csp_append(bound,"connect-src 'none'");
    CHECK("new meta none still intersects inherited allowances",!csp_allows_url(bound,"data:text/html,child","https://source.test/","connect-src"));free(bound);
    const char *other="report-to 'self'; sandbox 'self'; unknown-src 'self'; script-src 'nonce-self' 'unsafe-inline'";
    bound=csp_inherit(other,"https://source.test");CHECK("non-source directives and token substrings unchanged",bound&&!strcmp(bound,other));free(bound);
    bound=csp_inherit("ScRiPt-SrC\t'SeLf' 'nonce-abc'","https://source.test");
    CHECK("token matching preserves case-insensitive keyword semantics",csp_allows_script(bound,"data:text/html,child","https://source.test/x.js",NULL,NULL,0,true));
    CHECK("nonce remains valid across inheritance",csp_allows_script(bound,"data:text/html,child",NULL,"abc","safe",4,true));
    CHECK("other inline script remains blocked",!csp_allows_script(bound,"data:text/html,child",NULL,NULL,"bad",3,true));free(bound);
    static const char *opaque[]={"data:text/html,opaque","file:///tmp/source.html","about:blank","https://*.example.test/","https://source.test;script-src*/"};
    for(size_t i=0;i<sizeof(opaque)/sizeof(*opaque);++i){bound=csp_inherit("script-src 'self'",opaque[i]);
        CHECK("opaque or unrepresentable literal self never grants a wildcard",!csp_allows_script(bound,"data:text/html,child","https://source.test/x.js",NULL,NULL,0,true));free(bound);}
    CHECK("empty policy stays empty",csp_inherit(NULL,"https://source.test")==NULL);
    printf("RESULTS %u/%u checks passed\n",checks-failures,checks);return failures?1:0;
}
