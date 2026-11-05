/* Synthetic mutation ordering controls; reference-derived oracle v2 before implementation.
 * Two expired-unread controls were calibrated to fresh Chromium; original inputs/results are retained.
 * Uses no real profile; optional persistence file belongs to this test only. */
#include "../src/net/net.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
typedef struct { int kind; const char *cookie; int wait_ms; } Step;
typedef struct { const char *name; Step steps[10]; int n; const char *http, *document; } Case;
static const Case cases[] = {
{"unchanged_value_retains_position", {{0, "z=first; Path=/", 0}, {0, "a=second; Path=/", 0}, {0, "m=third; Path=/", 0}, {0, "z=first; Path=/", 0}}, 4, "z=first; a=second; m=third", "z=first; a=second; m=third"},
{"changed_value_moves_after_stable_survivors", {{0, "z=first; Path=/", 0}, {0, "a=second; Path=/", 0}, {0, "m=third; Path=/", 0}, {0, "z=new; Path=/", 0}}, 4, "a=second; m=third; z=new", "a=second; m=third; z=new"},
{"delete_first_preserves_survivors", {{0, "z=first; Path=/", 0}, {0, "a=second; Path=/", 0}, {0, "m=third; Path=/", 0}, {0, "z=gone; Path=/; Max-Age=0", 0}}, 4, "a=second; m=third", "a=second; m=third"},
{"delete_middle_preserves_survivors", {{0, "z=first; Path=/", 0}, {0, "a=second; Path=/", 0}, {0, "m=third; Path=/", 0}, {0, "a=gone; Path=/; Max-Age=0", 0}}, 4, "z=first; m=third", "z=first; m=third"},
{"delete_last_preserves_survivors", {{0, "z=first; Path=/", 0}, {0, "a=second; Path=/", 0}, {0, "m=third; Path=/", 0}, {0, "m=gone; Path=/; Max-Age=0", 0}}, 4, "z=first; a=second", "z=first; a=second"},
{"delete_reinsert_is_new_position", {{0, "z=first; Path=/", 0}, {0, "a=second; Path=/", 0}, {0, "m=third; Path=/", 0}, {0, "z=gone; Path=/; Max-Age=0", 0}, {0, "z=new; Path=/", 0}}, 5, "a=second; m=third; z=new", "a=second; m=third; z=new"},
{"same_value_expiry_attribute_refresh", {{0, "z=first; Path=/; Max-Age=3600", 0}, {0, "a=second; Path=/", 0}, {0, "m=third; Path=/", 0}, {0, "z=first; Path=/; Max-Age=7200", 0}}, 4, "z=first; a=second; m=third", "z=first; a=second; m=third"},
{"same_value_expired_input_deletes", {{0, "z=first; Path=/", 0}, {0, "a=second; Path=/", 0}, {0, "m=third; Path=/", 0}, {0, "z=first; Path=/; Max-Age=0", 0}}, 4, "a=second; m=third", "a=second; m=third"},
{"same_value_http_adds_httponly", {{0, "z=first; Path=/", 0}, {0, "a=second; Path=/", 0}, {0, "m=third; Path=/", 0}, {1, "z=first; Path=/; HttpOnly", 0}}, 4, "z=first; a=second; m=third", "a=second; m=third"},
{"same_value_http_removes_httponly", {{1, "z=first; Path=/; HttpOnly", 0}, {0, "a=second; Path=/", 0}, {0, "m=third; Path=/", 0}, {1, "z=first; Path=/", 0}}, 4, "z=first; a=second; m=third", "z=first; a=second; m=third"},
{"script_cannot_remove_httponly_same_value", {{1, "z=first; Path=/; HttpOnly", 0}, {0, "a=second; Path=/", 0}, {0, "m=third; Path=/", 0}, {0, "z=first; Path=/", 0}}, 4, "z=first; a=second; m=third", "a=second; m=third"},
{"script_cannot_overwrite_httponly_changed_value", {{1, "z=first; Path=/; HttpOnly", 0}, {0, "a=second; Path=/", 0}, {0, "m=third; Path=/", 0}, {0, "z=new; Path=/", 0}}, 4, "z=first; a=second; m=third", "a=second; m=third"},
{"expired_old_cookie_filtered", {{0, "z=first; Path=/; Max-Age=1", 0}, {0, "a=second; Path=/", 0}, {0, "m=third; Path=/", 0}, {2, NULL, 2200}}, 4, "a=second; m=third", "a=second; m=third"},
{"expired_old_unread_same_value_update", {{0, "z=first; Path=/; Max-Age=1", 0}, {0, "a=second; Path=/", 0}, {0, "m=third; Path=/", 0}, {2, NULL, 2200}, {0, "z=first; Path=/", 0}}, 5, "z=first; a=second; m=third", "z=first; a=second; m=third"},
{"expired_httponly_unread_blocks_document_update", {{1, "z=first; Path=/; Max-Age=1; HttpOnly", 0}, {0, "a=second; Path=/", 0}, {0, "m=third; Path=/", 0}, {2, NULL, 2200}, {0, "z=new; Path=/", 0}}, 5, "a=second; m=third", "a=second; m=third"},
{"site_clear_retains_other_site_order", {{0, "z=first; Path=/", 0}, {3, "noise1=n; Path=/", 0}, {0, "a=second; Path=/", 0}, {3, "noise2=n; Path=/", 0}, {0, "m=third; Path=/", 0}, {4, NULL, 0}}, 6, "z=first; a=second; m=third", "z=first; a=second; m=third"},
{"reused_slots_preserve_survivors", {{0, "z=first; Path=/", 0}, {0, "a=second; Path=/", 0}, {0, "m=third; Path=/", 0}, {0, "a=gone; Path=/; Max-Age=0", 0}, {0, "b=new; Path=/", 0}, {0, "z=gone; Path=/; Max-Age=0", 0}, {0, "c=last; Path=/", 0}, {0, "b=new; Path=/", 0}}, 8, "m=third; b=new; c=last", "m=third; b=new; c=last"}
};
static int checks, failed;
static void put(const char *url, const char *cookie, int http) {
 if(!http){cookies_set_document(url,cookie);return;}URL u;if(!url_parse(url,&u))abort();cookies_set_from_header(&u,cookie);url_free(&u);
}
static char *wire(void) { URL u;if(!url_parse("http://127.0.0.1/a/b/page",&u))abort();char *s=cookies_get(&u,true);url_free(&u);return s?s:xstrdup(""); }
static void check(const char *name,const char *mode,const char *got,const char *want){int pass=!strcmp(got,want);checks++;failed+=!pass;printf("{\"case\":\"%s\",\"mode\":\"%s\",\"pass\":%s}\n",name,mode,pass?"true":"false");}
int main(int argc,char **argv){const char *path=argc==2?argv[1]:NULL;
 for(size_t i=0;i<sizeof cases/sizeof *cases;i++){const Case *c=&cases[i];cookies_clear();
  for(int j=0;j<c->n;j++){const Step *s=&c->steps[j];if(s->kind==2){struct timespec t={s->wait_ms/1000,(s->wait_ms%1000)*1000000L};while(nanosleep(&t,&t)&&errno==EINTR){}}else if(s->kind==4){if(cookies_site("other.test",true)!=2)abort();}else put(s->kind==3?"http://other.test/":"http://127.0.0.1/a/b/page",s->cookie,s->kind==1);}
  char *http=wire(),*doc=cookies_get_document("http://127.0.0.1/a/b/page");printf("{\"observation\":\"%s\",\"http\":\"%s\",\"document\":\"%s\"}\n",c->name,http,doc);check(c->name,"http",http,c->http);check(c->name,"document",doc,c->document);char *again=wire();check(c->name,"repeat",again,http);free(again);
  if(path){if(!cookies_save(path))abort();cookies_clear();cookies_load(path);char *saved=wire();check(c->name,"roundtrip",saved,c->http);free(saved);}free(http);free(doc);
 }
 if(path)remove(path);cookies_clear();printf("{\"checks\":%d,\"passed\":%d,\"failed\":%d,\"synthetic_only\":true}\n",checks,checks-failed,failed);return failed?1:0;
}
