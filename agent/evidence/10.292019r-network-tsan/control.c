#include <pthread.h>
static void *run(void *value) { return value; }
int main(void) { pthread_t thread; if (pthread_create(&thread, 0, run, 0)) return 2; return pthread_join(thread, 0); }
