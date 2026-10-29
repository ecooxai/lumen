#include "../src/net/net.h"
int main(int argc, char **argv) {
    net_init(4);
    for (int i = 1; i < argc; i++) {
        NetRequest *rq = net_request_new("GET", argv[i]);
        double t = now_ms();
        NetResponse *r = net_fetch_sync(rq);
        printf("%s -> %d %s (%zu bytes, %.0f ms) ct=%s enc=%s err=%s\n", r->url, r->status, r->status_text ? r->status_text : "", r->body_len, now_ms() - t,
               headers_get(&r->headers, "content-type"), headers_get(&r->headers, "content-encoding"), r->error ? r->error : "-");
        if (getenv("DUMP")) fwrite(r->body, 1, r->body_len < 600 ? r->body_len : 600, stdout), puts("");
        net_response_free(r);
    }
    return 0;
}
