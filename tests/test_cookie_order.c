/* Cookie retrieval ordering controls, frozen before the production patch.
 * No live profile or network; all cookie names and values are synthetic.
 * --observe adds out-of-scope storage-mutation comparisons without claiming them as passes. */
#include "../src/net/net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { int http; const char *cookie; } Step;
typedef struct { const char *name, *path; Step steps[6]; int n; const char *http, *document; int in_scope; } Case;
static const Case cases[] = {
{"short_before_long", "/a/b/page", {{0, "root=r; Path=/"}, {0, "mid=m; Path=/a"}, {0, "deep=d; Path=/a/b"}}, 3, "deep=d; mid=m; root=r", "deep=d; mid=m; root=r", 1},
{"long_before_short", "/a/b/page", {{0, "deep=d; Path=/a/b"}, {0, "mid=m; Path=/a"}, {0, "root=r; Path=/"}}, 3, "deep=d; mid=m; root=r", "deep=d; mid=m; root=r", 1},
{"same_name_paths", "/a/b/page", {{0, "key=root; Path=/"}, {0, "key=mid; Path=/a"}, {0, "key=deep; Path=/a/b"}}, 3, "key=deep; key=mid; key=root", "key=deep; key=mid; key=root", 1},
{"equal_path_insert", "/a/b/page", {{0, "z=first; Path=/"}, {0, "a=second; Path=/"}, {0, "m=third; Path=/"}}, 3, "z=first; a=second; m=third", "z=first; a=second; m=third", 1},
{"unchanged_replacement_observe", "/a/b/page", {{0, "z=first; Path=/"}, {0, "a=second; Path=/"}, {0, "m=third; Path=/"}, {0, "z=first; Path=/"}}, 4, NULL, NULL, 0},
{"changed_replacement_observe", "/a/b/page", {{0, "z=first; Path=/"}, {0, "a=second; Path=/"}, {0, "m=third; Path=/"}, {0, "z=new; Path=/"}}, 4, NULL, NULL, 0},
{"delete_observe", "/a/b/page", {{0, "z=first; Path=/"}, {0, "a=second; Path=/"}, {0, "m=third; Path=/"}, {0, "z=gone; Path=/; Max-Age=0"}}, 4, NULL, NULL, 0},
{"delete_reinsert_observe", "/a/b/page", {{0, "z=first; Path=/"}, {0, "a=second; Path=/"}, {0, "m=third; Path=/"}, {0, "z=gone; Path=/; Max-Age=0"}, {0, "z=new; Path=/"}}, 5, NULL, NULL, 0},
{"httponly_filter", "/a/b/page", {{0, "root=r; Path=/"}, {1, "hidden=h; Path=/a/b; HttpOnly"}, {0, "mid=m; Path=/a"}}, 3, "hidden=h; mid=m; root=r", "mid=m; root=r", 1},
{"path_boundary", "/ab/page", {{0, "root=r; Path=/"}, {0, "mid=m; Path=/a"}}, 2, "root=r", "root=r", 1},
{"trailing_slash_length", "/a/b/page", {{0, "root=r; Path=/"}, {0, "plain=p; Path=/a/b"}, {0, "slash=s; Path=/a/b/"}}, 3, "slash=s; plain=p; root=r", "slash=s; plain=p; root=r", 1},
{"stable_equal_length_groups", "/a/b/page", {{0, "root=r; Path=/"}, {0, "first=one; Path=/a"}, {0, "second=two; Path=/a"}, {0, "deep=d; Path=/a/b"}}, 4, "deep=d; first=one; second=two; root=r", "deep=d; first=one; second=two; root=r", 1},
{"expired_longer_filtered", "/a/b/page", {{0, "root=r; Path=/"}, {0, "expired=e; Path=/a/b; Max-Age=-1"}, {0, "mid=m; Path=/a"}}, 3, "mid=m; root=r", "mid=m; root=r", 1}
};
static int checks, failed;
static char *wire(const char *address) { URL u; if (!url_parse(address, &u)) abort(); char *s = cookies_get(&u, true); url_free(&u); return s ? s : xstrdup(""); }
static void check(const char *name, const char *mode, const char *got, const char *want) { checks++; int pass=!strcmp(got,want); failed+=!pass; printf("{\"case\":\"%s\",\"mode\":\"%s\",\"pass\":%s}\n",name,mode,pass?"true":"false"); }
int main(int argc, char **argv) {
 int observe=argc>1&&!strcmp(argv[1],"--observe"); const char *save_path=argc>1&&!observe?argv[1]:NULL;
 for (size_t i=0;i<sizeof cases/sizeof *cases;i++) { const Case *c=&cases[i]; if (!c->in_scope&&!observe) continue; cookies_clear();
  for(int j=0;j<c->n;j++) { if(c->steps[j].http){URL u;if(!url_parse("http://127.0.0.1/a/b/page",&u))abort();cookies_set_from_header(&u,c->steps[j].cookie);url_free(&u);}else cookies_set_document("http://127.0.0.1/a/b/page",c->steps[j].cookie); }
  char url[256];snprintf(url,sizeof url,"http://127.0.0.1%s",c->path);char *http=wire(url),*doc=cookies_get_document(url);
  if(observe)printf("{\"observation\":\"%s\",\"http\":\"%s\",\"document\":\"%s\",\"in_scope\":%s}\n",c->name,http,doc,c->in_scope?"true":"false");
  if(c->in_scope){check(c->name,"http",http,c->http);check(c->name,"document",doc,c->document);char *again=wire(url);check(c->name,"repeat",again,http);free(again);}
  if(save_path&&c->in_scope){if(!cookies_save(save_path))abort();cookies_clear();cookies_load(save_path);char *saved=wire(url);check(c->name,"roundtrip",saved,c->http);free(saved);}
  free(http);free(doc);
 }
 if(save_path)remove(save_path);cookies_clear();printf("{\"checks\":%d,\"passed\":%d,\"failed\":%d,\"synthetic_only\":true}\n",checks,checks-failed,failed);return failed?1:0;
}
