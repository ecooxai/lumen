#include "../src/net/net.h"
#include <stdio.h>
static unsigned checks, failures;
static void check(const char *label, const char *policy, bool actual, bool expected) {
    checks++;
    if (actual != expected) {
        failures++;
        fprintf(stderr, "%s (%s): expected %d got %d\n", label, policy, expected, actual);
    }
}
typedef struct {
    const char *label, *type1, *type2, *options1, *options2;
    bool script, style, javascript;
} Case;
int main(void) {
    static const Case cases[] = {
        {"missing MIME", NULL,NULL,NULL,NULL, true,true,false},
        {"empty MIME", "",NULL,NULL,NULL, true,true,false},
        {"malformed MIME", "not a type",NULL,NULL,NULL, true,true,false},
        {"classic plain text", "text/plain",NULL,NULL,NULL, true,true,false},
        {"classic HTML", "text/html",NULL,NULL,NULL, true,true,false},
        {"blocked audio", "audio/mpeg",NULL,NULL,NULL, false,true,false},
        {"blocked image", "image/png",NULL,NULL,NULL, false,true,false},
        {"blocked video", "video/mp4",NULL,NULL,NULL, false,true,false},
        {"blocked CSV", "text/csv",NULL,NULL,NULL, false,true,false},
        {"case and parameters", " \tImAgE/PNG ; charset=utf-8\t",NULL,NULL,NULL, false,true,false},
        {"parameters preserve CSV", "text/csv;a=b",NULL,NULL,NULL, false,true,false},
        {"similar type allowed", "imagex/png",NULL,NULL,NULL, true,true,false},
        {"similar subtype allowed", "text/csvx",NULL,NULL,NULL, true,true,false},
        {"invalid blocked-looking type", "image /png",NULL,NULL,NULL, true,true,false},
        {"missing subtype", "image/",NULL,NULL,NULL, true,true,false},
        {"extra slash", "image/png/extra",NULL,NULL,NULL, true,true,false},
        {"nosniff missing MIME", NULL,NULL,"nosniff",NULL, false,false,false},
        {"nosniff empty MIME", "",NULL,"nosniff",NULL, false,false,false},
        {"nosniff malformed MIME", "x",NULL,"nosniff",NULL, false,false,false},
        {"nosniff non-JS", "text/plain",NULL,"nosniff",NULL, false,false,false},
        {"nosniff JavaScript", "text/javascript",NULL,"NoSnIfF",NULL, true,false,true},
        {"nosniff CSS", "text/css",NULL,"nosniff",NULL, false,true,false},
        {"nosniff CSS parameters", "text/css;charset=utf-8",NULL,"nosniff",NULL, false,true,false},
        {"nosniff similar JS", "application/javascriptx",NULL,"nosniff",NULL, false,false,false},
        {"nosniff first token", "x/x",NULL," \tnosniff\t,other",NULL, false,false,false},
        {"later token ignored", "x/x",NULL,"other,nosniff",NULL, true,true,false},
        {"leading empty token", "x/x",NULL,",nosniff",NULL, true,true,false},
        {"first duplicate options", "x/x",NULL,"nosniff","other", false,false,false},
        {"later duplicate options", "x/x",NULL,"other","nosniff", true,true,false},
        {"empty first duplicate options", "x/x",NULL,"","nosniff", true,true,false},
        {"quoted nosniff", "x/x",NULL,"\"nosniff\"",NULL, true,true,false},
        {"single quoted nosniff", "x/x",NULL,"'nosniff'",NULL, true,true,false},
        {"form feed not HTTP space", "x/x",NULL,"nosniff\f",NULL, true,true,false},
        {"vertical tab not HTTP space", "x/x",NULL,"nosniff\v",NULL, true,true,false},
        {"last valid duplicate wins", "image/png","text/javascript",NULL,NULL, true,true,true},
        {"last valid duplicate blocked", "text/javascript","image/png",NULL,NULL, false,true,false},
        {"invalid duplicate ignored", "image/png","cannot-parse",NULL,NULL, false,true,false},
        {"wildcard duplicate ignored", "image/png","*/*",NULL,NULL, false,true,false},
        {"empty duplicate ignored", "image/png","",NULL,NULL, false,true,false},
        {"empty first content type", "","text/javascript","nosniff",NULL, true,false,true},
        {"last valid list MIME", "image/png, text/javascript",NULL,"nosniff",NULL, true,false,true},
        {"blocked list MIME", "text/javascript, video/mp4",NULL,NULL,NULL, false,true,false},
        {"quoted comma parameter", "text/javascript;note=\"image/png,video/mp4\"",NULL,"nosniff",NULL, true,false,true},
        {"escaped quote parameter", "text/javascript;note=\"one\\\",image/png\"",NULL,"nosniff",NULL, true,false,true},
        {"combined quoted field", "text/javascript;note=\"","image/png","nosniff",NULL, true,false,true},
        {"wildcard alone missing", "*/*",NULL,"nosniff",NULL, false,false,false},
        {"HTTP whitespace MIME", "\r\ntext/javascript \r\n",NULL,"nosniff",NULL, true,false,true},
        {"empty subtype before parameter", "text/;a=b",NULL,"nosniff",NULL, false,false,false},
        {"invalid parameter does not erase essence", "text/javascript;\"",NULL,"nosniff",NULL, true,false,true}
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        const Case *c = &cases[i]; NetResponse r = { .status = 200 };
        if (c->type1) headers_add(&r.headers,"Content-Type",c->type1);
        if (c->type2) headers_add(&r.headers,"cOnTeNt-TyPe",c->type2);
        if (c->options1) headers_add(&r.headers,"X-Content-Type-Options",c->options1);
        if (c->options2) headers_add(&r.headers,"x-content-type-options",c->options2);
        check(c->label,"classic script",net_response_mime_allowed(&r,NET_MIME_SCRIPT),c->script);
        check(c->label,"style",net_response_mime_allowed(&r,NET_MIME_STYLE),c->style);
        check(c->label,"strict JavaScript",net_response_mime_is_javascript(&r),c->javascript);
        headers_free(&r.headers);
    }
    static const char *const aliases[] = {
        "application/ecmascript","application/javascript","application/x-ecmascript","application/x-javascript",
        "text/ecmascript","text/javascript","text/javascript1.0","text/javascript1.1","text/javascript1.2",
        "text/javascript1.3","text/javascript1.4","text/javascript1.5","text/jscript","text/livescript",
        "text/x-ecmascript","text/x-javascript"
    };
    for (size_t i=0;i<sizeof aliases/sizeof *aliases;i++) {
        NetResponse r={0};headers_add(&r.headers,"content-type",aliases[i]);headers_add(&r.headers,"x-content-type-options","nosniff");
        check(aliases[i],"nosniff JavaScript alias",net_response_mime_allowed(&r,NET_MIME_SCRIPT),true);
        headers_free(&r.headers);
    }
    check("absent response","script",net_response_mime_allowed(NULL,NET_MIME_SCRIPT),false);
    check("absent response","strict JavaScript",net_response_mime_is_javascript(NULL),false);
    printf("MIME response policy: %u/%u checks passed\n",checks-failures,checks);
    return failures?1:0;
}
