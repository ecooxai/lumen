#include "../src/net/net.h"
#include <stdio.h>
#include <string.h>

/* Independent native-path controls. fetch() also has a JavaScript local-URL
 * implementation, so its successful tests alone cannot validate this path. */
typedef struct { const char *name, *url, *mime; const unsigned char *bytes; size_t length; } Case;
static const Case cases[] = {
    {"empty", "data:;base64,", "text/plain;charset=US-ASCII", (const unsigned char *)"", 0},
    {"one byte padded", "data:;base64,YQ==", "text/plain;charset=US-ASCII", (const unsigned char *)"a", 1},
    {"one byte unpadded", "data:;base64,YQ", "text/plain;charset=US-ASCII", (const unsigned char *)"a", 1},
    {"nonzero discarded bits", "data:;base64,YR", "text/plain;charset=US-ASCII", (const unsigned char *)"a", 1},
    {"two bytes padded", "data:;base64,YWI=", "text/plain;charset=US-ASCII", (const unsigned char *)"ab", 2},
    {"two bytes unpadded", "data:;base64,YWI", "text/plain;charset=US-ASCII", (const unsigned char *)"ab", 2},
    {"three bytes", "data:;base64,YWJj", "text/plain;charset=US-ASCII", (const unsigned char *)"abc", 3},
    {"standard alphabet", "data:;base64,+/8=", "text/plain;charset=US-ASCII", (const unsigned char *)"\373\377", 2},
    {"zero byte", "data:;base64,AA==", "text/plain;charset=US-ASCII", (const unsigned char *)"\000", 1},
    {"single digit", "data:;base64,A", NULL, NULL, 0},
    {"remainder one", "data:;base64,AAAAA", NULL, NULL, 0},
    {"lone equals", "data:;base64,=", NULL, NULL, 0},
    {"two equals", "data:;base64,==", NULL, NULL, 0},
    {"three equals", "data:;base64,===", NULL, NULL, 0},
    {"four equals", "data:;base64,====", NULL, NULL, 0},
    {"short padding", "data:;base64,YQ=", NULL, NULL, 0},
    {"excess padding", "data:;base64,YQ===", NULL, NULL, 0},
    {"padding after block", "data:;base64,YWJj=", NULL, NULL, 0},
    {"internal padding", "data:;base64,Y=Q=", NULL, NULL, 0},
    {"leading padding", "data:;base64,=YQ=", NULL, NULL, 0},
    {"padding then alphabet", "data:;base64,YQ==A", NULL, NULL, 0},
    {"URL-safe minus", "data:;base64,-w==", NULL, NULL, 0},
    {"URL-safe underscore", "data:;base64,_w==", NULL, NULL, 0},
    {"vertical tab", "data:;base64,Y%0BQ==", NULL, NULL, 0},
    {"NUL prefix", "data:;base64,%00YQ==", NULL, NULL, 0},
    {"NUL suffix", "data:;base64,YQ==%00", NULL, NULL, 0},
    {"nonASCII whitespace", "data:;base64,Y%C2%A0Q==", NULL, NULL, 0},
    {"punctuation", "data:;base64,Y!Q==", NULL, NULL, 0},
    {"invalid percent", "data:;base64,Y%GGQ==", NULL, NULL, 0},
    {"all ASCII whitespace", "data:;base64,%09Y%0AQ%0C%0D%20=%09=", "text/plain;charset=US-ASCII", (const unsigned char *)"a", 1},
    {"binary plain NUL", "data:application/octet-stream,a%00b%FF", "application/octet-stream", (const unsigned char *)"a\000b\377", 4},
    {"plain plus retained", "data:,a+b", "text/plain;charset=US-ASCII", (const unsigned char *)"a+b", 3},
    {"encoded percent not twice decoded", "data:,%2523%00%25", "text/plain;charset=US-ASCII", (const unsigned char *)"%23\000%", 5},
    {"malformed percent unchanged", "data:,x%Q0%2", "text/plain;charset=US-ASCII", (const unsigned char *)"x%Q0%2", 6},
    {"fragment excluded", "data:,a#fragment,ignored", "text/plain;charset=US-ASCII", (const unsigned char *)"a", 1},
    {"encoded fragment retained", "data:,a%23b", "text/plain;charset=US-ASCII", (const unsigned char *)"a#b", 3},
    {"query retained", "data:,a?x=1", "text/plain;charset=US-ASCII", (const unsigned char *)"a?x=1", 5},
    {"no comma", "data:text/html", NULL, NULL, 0},
    {"comma only in fragment", "data:text/html#x,y", NULL, NULL, 0},
    {"invalid data authority port", "data://host:word/,x", NULL, NULL, 0},
    {"mixed case scheme and marker", "DaTa:;BaSe64,YQ==", "text/plain;charset=US-ASCII", (const unsigned char *)"a", 1},
    {"spaced marker", "data:  ;  base64   ,YQ==", "text/plain;charset=US-ASCII", (const unsigned char *)"a", 1},
    {"marker must be suffix", "data:x/x;base64;p=v,WA", "x/x;p=v", (const unsigned char *)"WA", 2},
    {"marker parameter is not switch", "data:x/x;base64=x,WA", "x/x;base64=x", (const unsigned char *)"WA", 2},
    {"marker prefix is not switch", "data:x/x;base64x,WA", "x/x", (const unsigned char *)"WA", 2},
    {"repeated marker", "data:x/x;base64;base64,WA", "x/x", (const unsigned char *)"X", 1},
    {"repeated marker without mime", "data:;base64;base64,WA", "text/plain", (const unsigned char *)"X", 1},
    {"encoded marker not switch", "data:;%62ase64,WA", "text/plain", (const unsigned char *)"WA", 2},
    {"mime omitted type", "data:;charset=UTF-8,hello", "text/plain;charset=UTF-8", (const unsigned char *)"hello", 5},
    {"mime case and quote", "data:IMAGE/SVG+XML;Charset=\"UTF-8\",x", "image/svg+xml;charset=UTF-8", (const unsigned char *)"x", 1},
    {"mime invalid fallback", "data:text / html,x", "text/plain;charset=US-ASCII", (const unsigned char *)"x", 1},
    {"mime subtype space trim", "data:text/plain   ,x", "text/plain", (const unsigned char *)"x", 1},
    {"mime quoted empty parameter", "data:text/plain;a=\"\",x", "text/plain;a=\"\"", (const unsigned char *)"x", 1},
    {"mime empty unquoted ignored", "data:text/plain;a=;b=x,x", "text/plain;b=x", (const unsigned char *)"x", 1},
    {"mime first parameter wins", "data:text/plain;A=one;a=two,x", "text/plain;a=one", (const unsigned char *)"x", 1},
    {"mime invalid name ignored", "data:text/plain;a =one;b=two,x", "text/plain;b=two", (const unsigned char *)"x", 1},
    {"mime quoted semicolon", "data:text/plain;a=\"x;y\",x", "text/plain;a=\"x;y\"", (const unsigned char *)"x", 1},
    {"mime escaped quote", "data:text/plain;a=\"x\\\"y\",x", "text/plain;a=\"x\\\"y\"", (const unsigned char *)"x", 1},
    {"mime quoted suffix ignored", "data:text/plain;a=\"x\"junk;b=y,x", "text/plain;a=x;b=y", (const unsigned char *)"x", 1},
    {"mime leading value whitespace", "data:text/plain;a= x ,x", "text/plain;a=\" x\"", (const unsigned char *)"x", 1},
    {"mime lowercase percent escapes", "data:TEXT/PLAIN%0C,x", "text/plain%0c", (const unsigned char *)"x", 1},
    {"empty body", "data:,", "text/plain;charset=US-ASCII", (const unsigned char *)"", 0},
    {"binary base64 length 1", "data:application/octet-stream;base64,Cw==", "application/octet-stream", (const unsigned char *)"\013", 1},
    {"binary base64 length 2", "data:application/octet-stream;base64,CzA=", "application/octet-stream", (const unsigned char *)"\0130", 2},
    {"binary base64 length 3", "data:application/octet-stream;base64,CzBV", "application/octet-stream", (const unsigned char *)"\0130U", 3},
    {"binary base64 length 255", "data:application/octet-stream;base64,CzBVep/E6Q4zWH2ix+wRNluApcrvFDleg6jN8hc8YYar0PUaP2SJrtP4HUJnjLHW+yBFao+02f4jSG2St9wBJktwlbrfBClOc5i94gcsUXabwOUKL1R5nsPoDTJXfKHG6xA1Wn+kye4TOF2Cp8zxFjtgharP9Bk+Y4it0vccQWaLsNX6H0RpjrPY/SJHbJG22wAlSm+Uud4DKE1yl7zhBitQdZq/5AkuU3idwucMMVZ7oMXqDzRZfqPI7RI3XIGmy/AVOl+Eqc7zGD1ih6zR9htAZYqv1PkeQ2iNstf8IUZrkLXa/yRJbpO43QInTHGWu+AFKk90mb7jCC1Sd5zB", "application/octet-stream", (const unsigned char *)"\0130Uz\237\304\351\0163X}\242\307\354\0216[\200\245\312\357\0249^\203\250\315\362\027<a\206\253\320\365\032?d\211\256\323\370\035Bg\214\261\326\373 Ej\217\264\331\376#Hm\222\267\334\001&Kp\225\272\337\004)Ns\230\275\342\007,Qv\233\300\345\012/Ty\236\303\350\0152W|\241\306\353\0205Z\177\244\311\356\0238]\202\247\314\361\026;`\205\252\317\364\031>c\210\255\322\367\034Af\213\260\325\372\037Di\216\263\330\375\042Gl\221\266\333\000%Jo\224\271\336\003(Mr\227\274\341\006+Pu\232\277\344\011.Sx\235\302\347\0141V{\240\305\352\0174Y~\243\310\355\0227\134\201\246\313\360\025:_\204\251\316\363\030=b\207\254\321\366\033@e\212\257\324\371\036Ch\215\262\327\374!Fk\220\265\332\377$In\223\270\335\002'Lq\226\273\340\005*Ot\231\276\343\010-Rw\234\301", 255},
    {"binary base64 length 256", "data:application/octet-stream;base64,CzBVep/E6Q4zWH2ix+wRNluApcrvFDleg6jN8hc8YYar0PUaP2SJrtP4HUJnjLHW+yBFao+02f4jSG2St9wBJktwlbrfBClOc5i94gcsUXabwOUKL1R5nsPoDTJXfKHG6xA1Wn+kye4TOF2Cp8zxFjtgharP9Bk+Y4it0vccQWaLsNX6H0RpjrPY/SJHbJG22wAlSm+Uud4DKE1yl7zhBitQdZq/5AkuU3idwucMMVZ7oMXqDzRZfqPI7RI3XIGmy/AVOl+Eqc7zGD1ih6zR9htAZYqv1PkeQ2iNstf8IUZrkLXa/yRJbpO43QInTHGWu+AFKk90mb7jCC1Sd5zB5g==", "application/octet-stream", (const unsigned char *)"\0130Uz\237\304\351\0163X}\242\307\354\0216[\200\245\312\357\0249^\203\250\315\362\027<a\206\253\320\365\032?d\211\256\323\370\035Bg\214\261\326\373 Ej\217\264\331\376#Hm\222\267\334\001&Kp\225\272\337\004)Ns\230\275\342\007,Qv\233\300\345\012/Ty\236\303\350\0152W|\241\306\353\0205Z\177\244\311\356\0238]\202\247\314\361\026;`\205\252\317\364\031>c\210\255\322\367\034Af\213\260\325\372\037Di\216\263\330\375\042Gl\221\266\333\000%Jo\224\271\336\003(Mr\227\274\341\006+Pu\232\277\344\011.Sx\235\302\347\0141V{\240\305\352\0174Y~\243\310\355\0227\134\201\246\313\360\025:_\204\251\316\363\030=b\207\254\321\366\033@e\212\257\324\371\036Ch\215\262\327\374!Fk\220\265\332\377$In\223\270\335\002'Lq\226\273\340\005*Ot\231\276\343\010-Rw\234\301\346", 256},
    {"binary base64 length 257", "data:application/octet-stream;base64,CzBVep/E6Q4zWH2ix+wRNluApcrvFDleg6jN8hc8YYar0PUaP2SJrtP4HUJnjLHW+yBFao+02f4jSG2St9wBJktwlbrfBClOc5i94gcsUXabwOUKL1R5nsPoDTJXfKHG6xA1Wn+kye4TOF2Cp8zxFjtgharP9Bk+Y4it0vccQWaLsNX6H0RpjrPY/SJHbJG22wAlSm+Uud4DKE1yl7zhBitQdZq/5AkuU3idwucMMVZ7oMXqDzRZfqPI7RI3XIGmy/AVOl+Eqc7zGD1ih6zR9htAZYqv1PkeQ2iNstf8IUZrkLXa/yRJbpO43QInTHGWu+AFKk90mb7jCC1Sd5zB5gs=", "application/octet-stream", (const unsigned char *)"\0130Uz\237\304\351\0163X}\242\307\354\0216[\200\245\312\357\0249^\203\250\315\362\027<a\206\253\320\365\032?d\211\256\323\370\035Bg\214\261\326\373 Ej\217\264\331\376#Hm\222\267\334\001&Kp\225\272\337\004)Ns\230\275\342\007,Qv\233\300\345\012/Ty\236\303\350\0152W|\241\306\353\0205Z\177\244\311\356\0238]\202\247\314\361\026;`\205\252\317\364\031>c\210\255\322\367\034Af\213\260\325\372\037Di\216\263\330\375\042Gl\221\266\333\000%Jo\224\271\336\003(Mr\227\274\341\006+Pu\232\277\344\011.Sx\235\302\347\0141V{\240\305\352\0174Y~\243\310\355\0227\134\201\246\313\360\025:_\204\251\316\363\030=b\207\254\321\366\033@e\212\257\324\371\036Ch\215\262\327\374!Fk\220\265\332\377$In\223\270\335\002'Lq\226\273\340\005*Ot\231\276\343\010-Rw\234\301\346\013", 257},
    {"all percent-decoded bytes", "data:application/octet-stream,%00%01%02%03%04%05%06%07%08%09%0A%0B%0C%0D%0E%0F%10%11%12%13%14%15%16%17%18%19%1A%1B%1C%1D%1E%1F%20%21%22%23%24%25%26%27%28%29%2A%2B%2C%2D%2E%2F%30%31%32%33%34%35%36%37%38%39%3A%3B%3C%3D%3E%3F%40%41%42%43%44%45%46%47%48%49%4A%4B%4C%4D%4E%4F%50%51%52%53%54%55%56%57%58%59%5A%5B%5C%5D%5E%5F%60%61%62%63%64%65%66%67%68%69%6A%6B%6C%6D%6E%6F%70%71%72%73%74%75%76%77%78%79%7A%7B%7C%7D%7E%7F%80%81%82%83%84%85%86%87%88%89%8A%8B%8C%8D%8E%8F%90%91%92%93%94%95%96%97%98%99%9A%9B%9C%9D%9E%9F%A0%A1%A2%A3%A4%A5%A6%A7%A8%A9%AA%AB%AC%AD%AE%AF%B0%B1%B2%B3%B4%B5%B6%B7%B8%B9%BA%BB%BC%BD%BE%BF%C0%C1%C2%C3%C4%C5%C6%C7%C8%C9%CA%CB%CC%CD%CE%CF%D0%D1%D2%D3%D4%D5%D6%D7%D8%D9%DA%DB%DC%DD%DE%DF%E0%E1%E2%E3%E4%E5%E6%E7%E8%E9%EA%EB%EC%ED%EE%EF%F0%F1%F2%F3%F4%F5%F6%F7%F8%F9%FA%FB%FC%FD%FE%FF", "application/octet-stream", (const unsigned char *)"\000\001\002\003\004\005\006\007\010\011\012\013\014\015\016\017\020\021\022\023\024\025\026\027\030\031\032\033\034\035\036\037 !\042#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\134]^_`abcdefghijklmnopqrstuvwxyz{|}~\177\200\201\202\203\204\205\206\207\210\211\212\213\214\215\216\217\220\221\222\223\224\225\226\227\230\231\232\233\234\235\236\237\240\241\242\243\244\245\246\247\250\251\252\253\254\255\256\257\260\261\262\263\264\265\266\267\270\271\272\273\274\275\276\277\300\301\302\303\304\305\306\307\310\311\312\313\314\315\316\317\320\321\322\323\324\325\326\327\330\331\332\333\334\335\336\337\340\341\342\343\344\345\346\347\350\351\352\353\354\355\356\357\360\361\362\363\364\365\366\367\370\371\372\373\374\375\376\377", 256},
};

/* A repeated known quartet exercises a large body without embedding a large
 * generated asset or duplicating the production decoder in the test. */
static bool large_payload(void) {
    const char *prefix = "data:application/octet-stream;base64,";
    const size_t blocks = 16384, prefix_len = strlen(prefix);
    char *url = xmalloc(prefix_len + blocks * 4 + 1);
    memcpy(url, prefix, prefix_len);
    for (size_t i = 0; i < blocks; i++) memcpy(url + prefix_len + i * 4, "AAEC", 4);
    url[prefix_len + blocks * 4] = 0;
    NetResponse *r = net_fetch_sync(net_request_new("GET", url));
    bool good = r && r->status == 200 && !r->error && r->body_len == blocks * 3;
    for (size_t i = 0; good && i < r->body_len; i++) good = (unsigned char)r->body[i] == i % 3;
    if (!good) fprintf(stderr, "FAIL data URL: large repeated binary payload\n");
    net_response_free(r); free(url);
    return good;
}

int main(void) {
    size_t passed = 0;
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        const Case *c = &cases[i];
        NetResponse *r = net_fetch_sync(net_request_new("GET", c->url));
        bool good = r != NULL;
        if (good && !c->mime) good = r->status == 0 && r->error && r->body_len == 0;
        else if (good) {
            const char *mime = headers_get(&r->headers, "content-type");
            good = r->status == 200 && !r->error && r->body_len == c->length &&
                   (!c->length || (r->body && !memcmp(r->body, c->bytes, c->length))) &&
                   mime && !strcmp(mime, c->mime);
        }
        if (good) passed++;
        else fprintf(stderr, "FAIL data URL: %s (status=%d bytes=%zu type=%s)\n", c->name,
                     r ? r->status : -1, r ? r->body_len : 0,
                     r && headers_get(&r->headers, "content-type") ? headers_get(&r->headers, "content-type") : "none");
        net_response_free(r);
    }
    if (large_payload()) passed++;
    const size_t total = sizeof cases / sizeof cases[0] + 1;
    printf("data URL native controls: %zu/%zu PASS\n", passed, total);
    return passed == total ? 0 : 1;
}
