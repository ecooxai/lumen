#include "../src/net/net.h"
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

static NetResponse *fetch(const char *url) {
    NetResponse *response = net_fetch_sync(net_request_new("GET", url));
    CHECK(response != NULL);
    return response;
}

int main(void) {
    char directory[] = "/tmp/codex-lumen-files-XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    char path[1024], url[1200];
    snprintf(path, sizeof path, "%s/a <&\"#.PNG", directory);
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    CHECK(fwrite("\211PNG\0data", 1, 9, file) == 9);
    fclose(file);
    snprintf(url, sizeof url, "file://%s/a%%20%%3C%%26%%22%%23.PNG", directory);
    NetResponse *response = fetch(url);
    CHECK(response->status == 200);
    CHECK(response->body_len == 9);
    CHECK(!strcmp(headers_get(&response->headers, "content-type"), "image/png"));
    net_response_free(response);
    snprintf(url, sizeof url, "file://%s/", directory);
    response = fetch(url);
    CHECK(response->status == 200);
    CHECK(strstr(headers_get(&response->headers, "content-type"), "text/html"));
    CHECK(strstr(response->body, "a &lt;&amp;&quot;#.PNG"));
    CHECK(strstr(response->body, "a%20%3C%26%22%23.PNG"));
    CHECK(!strstr(response->body, "a <&\"#.PNG"));
    net_response_free(response);
    snprintf(url, sizeof url, "file://not-localhost%s/a%%20%%3C%%26%%22%%23.PNG", directory);
    response = fetch(url);
    CHECK(response->status == 403);
    net_response_free(response);
    snprintf(url, sizeof url, "file://%s/a%%20%%3C%%26%%22%%23.PNG%%00.txt", directory);
    response = fetch(url);
    CHECK(response->status == 400);
    net_response_free(response);
    CHECK(unlink(path) == 0);
    snprintf(path, sizeof path, "%s/pipe", directory);
    CHECK(mkfifo(path, 0600) == 0);
    snprintf(url, sizeof url, "file://%s/pipe", directory);
    response = fetch(url);
    CHECK(response->status == 403);
    net_response_free(response);
    CHECK(unlink(path) == 0);
    CHECK(rmdir(directory) == 0);
    puts("file tests ok");
    return 0;
}
