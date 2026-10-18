#include "../src/app/present_retry.h"
#include <stdio.h>

static int checks, failures;
#define CHECK(expr) do { checks++; if (!(expr)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #expr); } } while (0)

int main(void) {
    PresentRetry r = {0};
    CHECK(!present_retry_due(&r, 500));
    CHECK(present_retry_wait(&r, 500, 1000) == 1000);
    present_retry_fail(&r, 500);
    CHECK(r.delay_ms == 16 && r.at == 516);
    CHECK(!present_retry_due(&r, 515.5));
    CHECK(present_retry_wait(&r, 515.5, 1000) == 1); /* Never spin on a fractional millisecond. */
    CHECK(present_retry_wait(&r, 500, 4) == 4); /* An earlier JS/media deadline wins. */
    CHECK(present_retry_due(&r, 516));
    CHECK(present_retry_wait(&r, 516, 1000) == 0);
    const int delays[] = {32, 64, 128, 256, 512, 1000, 1000, 1000};
    for (unsigned i = 0; i < sizeof delays / sizeof delays[0]; i++) {
        double now = r.at;
        present_retry_fail(&r, now);
        CHECK(r.delay_ms == delays[i]);
        CHECK(r.at == now + delays[i]);
    }
    CHECK(present_retry_wait(&r, r.at - 200, 1000) == 200);
    CHECK(present_retry_wait(&r, r.at - 200, 0) == 0);
    CHECK(present_retry_due(&r, r.at + 5000));
    present_retry_clear(&r); /* Success, backgrounding or resize cancels pending work. */
    CHECK(!r.delay_ms && !r.at);
    CHECK(!present_retry_due(&r, 100000));
    CHECK(present_retry_wait(&r, 100000, 1000) == 1000);
    present_retry_fail(&r, 100000);
    CHECK(r.delay_ms == 16 && r.at == 100016); /* A new failure starts promptly. */
    printf("presentation retry: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
