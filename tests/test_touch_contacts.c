#include <stdio.h>
#include "../src/app/touch_contacts.h"

static int checks, failures;
#define CHECK(e) do { checks++; if (!(e)) { fprintf(stderr, "line %d: %s\n", __LINE__, #e); failures++; } } while (0)

int main(void) {
    TouchContacts c = {0};
    CHECK(touch_contacts_down(&c, 0)); /* Finger id zero is valid. */
    CHECK(c.n == 1 && !c.blocked && c.primary == 0);
    CHECK(!touch_contacts_down(&c, 0)); /* A duplicate cannot strand the gesture. */
    CHECK(c.n == 1 && !c.blocked);
    CHECK(!touch_contacts_up(&c, 9)); /* Unknown release cannot steal primary. */
    CHECK(c.n == 1);
    CHECK(touch_contacts_up(&c, 0)); CHECK(c.n == 0 && !c.blocked);

    CHECK(touch_contacts_down(&c, 42));
    CHECK(!touch_contacts_down(&c, 43)); CHECK(c.n == 2 && c.blocked);
    CHECK(!touch_contacts_up(&c, 43)); CHECK(c.n == 1 && c.blocked);
    CHECK(!touch_contacts_up(&c, 42)); CHECK(c.n == 0);
    CHECK(touch_contacts_down(&c, 44)); CHECK(!c.blocked);
    CHECK(touch_contacts_up(&c, 44));

    CHECK(touch_contacts_down(&c, 100));
    CHECK(!touch_contacts_down(&c, 101));
    CHECK(!touch_contacts_up(&c, 100)); CHECK(c.n == 1 && c.blocked);
    CHECK(!touch_contacts_up(&c, 101)); CHECK(c.n == 0);

    CHECK(touch_contacts_down(&c, 200));
    touch_contacts_cancel(&c); /* Rotation/background drops all interaction state. */
    CHECK(c.n == 0 && !c.blocked);
    CHECK(!touch_contacts_up(&c, 200));
    CHECK(touch_contacts_down(&c, 201)); CHECK(touch_contacts_up(&c, 201));

    printf("touch contacts: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
