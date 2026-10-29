#include "../src/net/net.h"
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

typedef struct {
    int listener;
    int status;
    const char *location;
    bool second_request;
} Server;

static void *serve(void *opaque) {
    Server *server = opaque;
    for (int index = 0; index < (server->second_request ? 2 : 1); index++) {
        int client = accept(server->listener, NULL, NULL);
        CHECK(client >= 0);
        struct timeval timeout = {5, 0};
        CHECK(setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) == 0);
        char request[8192];
        size_t used = 0;
        while (used + 1 < sizeof request) {
            ssize_t count = recv(client, request + used, sizeof request - used - 1, 0);
            CHECK(count > 0);
            used += (size_t)count;
            request[used] = 0;
            if (strstr(request, "\r\n\r\n")) break;
        }
        char response[2048];
        int length = index == 0
            ? snprintf(response, sizeof response, "HTTP/1.1 %d Redirect\r\nLocation: %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", server->status, server->location)
            : snprintf(response, sizeof response, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok");
        CHECK(length > 0 && (size_t)length < sizeof response);
        size_t sent = 0;
        while (sent < (size_t)length) {
            ssize_t count = send(client, response + sent, (size_t)length - sent, 0);
            CHECK(count > 0);
            sent += (size_t)count;
        }
        close(client);
    }
    return NULL;
}

static NetResponse *redirect(int status, const char *location, bool second_request) {
    Server server = {.listener = socket(AF_INET, SOCK_STREAM, 0), .status = status,
                     .location = location, .second_request = second_request};
    CHECK(server.listener >= 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    CHECK(bind(server.listener, (struct sockaddr *)&address, sizeof address) == 0);
    CHECK(listen(server.listener, 2) == 0);
    socklen_t size = sizeof address;
    CHECK(getsockname(server.listener, (struct sockaddr *)&address, &size) == 0);
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, serve, &server) == 0);
    char url[128];
    snprintf(url, sizeof url, "http://127.0.0.1:%u/start", ntohs(address.sin_port));
    NetRequest *request = net_request_new("GET", url);
    request->no_cookies = true;
    NetResponse *response = net_fetch_sync(request);
    CHECK(pthread_join(thread, NULL) == 0);
    close(server.listener);
    CHECK(response != NULL);
    return response;
}

int main(void) {
    alarm(15);
    char path[] = "/tmp/codex-lumen-redirect-XXXXXX";
    int descriptor = mkstemp(path);
    CHECK(descriptor >= 0);
    CHECK(write(descriptor, "private fixture", 15) == 15);
    close(descriptor);
    char url[256];
    snprintf(url, sizeof url, "file://%s", path);
    NetResponse *response = net_fetch_sync(net_request_new("GET", url));
    CHECK(response->status == 200 && response->body_len == 15);
    net_response_free(response);
    int statuses[] = {301, 302, 303, 307, 308};
    bool denied = true;
    for (size_t index = 0; index < sizeof statuses / sizeof statuses[0]; index++) {
        response = redirect(statuses[index], url, false);
        if (response->status != 0 || !response->error || response->body_len != 0) {
            fprintf(stderr, "unsafe HTTP %d redirect: status=%d bytes=%zu\n", statuses[index], response->status, response->body_len);
            denied = false;
        }
        net_response_free(response);
    }
    CHECK(unlink(path) == 0);
    CHECK(denied);
    response = redirect(302, "/destination", true);
    CHECK(response->status == 200 && response->body_len == 2 && !memcmp(response->body, "ok", 2));
    net_response_free(response);
    puts("network redirect tests ok");
    return 0;
}
