// SPDX-License-Identifier: MIT
// Real pthreads with deterministic creation failures around the production pool launcher.
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define LOG(...) ((void)0)
enum { ITEMS = 257 };
typedef struct { _Atomic int next, seen[ITEMS]; } Queue;
static pthread_t live[32];
static int calls, created, joined, fail_after;
static int run_precompile_workers(int requested, void *(*fn)(void *), void *arg);

static int test_create(pthread_t *thread, const pthread_attr_t *attr, void *(*fn)(void *), void *arg) {
    calls++;
    if (created == fail_after) return EAGAIN;
    int error = pthread_create(thread, attr, fn, arg);
    assert(error == 0);
    live[created++] = *thread;
    return 0;
}

static int test_join(pthread_t thread, void **value) {
    // A failed creation must not result in an invalid or duplicate join.
    int found = -1;
    for (int i = joined; i < created; i++)
        if (pthread_equal(thread, live[i])) { found = i; break; }
    assert(found >= 0);
    live[found] = live[joined];
    joined++;
    int error = pthread_join(thread, value);
    assert(error == 0);
    return error;
}

static void *drain(void *arg) {
    Queue *queue = arg;
    for (int i; (i = atomic_fetch_add(&queue->next, 1)) < ITEMS;)
        assert(atomic_fetch_add(&queue->seen[i], 1) == 0);
    return NULL;
}

static void check_case(int wanted, int fail, int expected_threads, int expected_calls) {
    Queue queue = {0};
    calls = created = joined = 0;
    fail_after = fail;
    int workers = run_precompile_workers(wanted, drain, &queue);
    assert(workers == expected_threads && calls == expected_calls && joined == created);
    for (int i = 0; i < ITEMS; i++) assert(atomic_load(&queue.seen[i]) == 1);
}

int main(void) {
    for (int repeat = 0; repeat < 8; repeat++) {
        check_case(4, 0, 1, 1);  // no thread: calling-thread fallback drains all work
        check_case(4, 2, 2, 3);  // partial pool: only successful pthread_t values are joined
        check_case(4, 32, 4, 4);
        check_case(0, 32, 1, 1);
        check_case(99, 32, 32, 32);
    }
    fprintf(stderr, "[precompile-workers] zero/partial/full pools drain exactly once and join all live workers\n");
    return 0;
}
