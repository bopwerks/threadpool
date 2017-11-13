#include <signal.h>
#include <assert.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

enum { NTHREADS = 100 };

struct work {
    int (*fn)(void *arg);
    void *arg;
    struct work *next;
};
static struct work *workhead = NULL;
static struct work *worktail = NULL;
static pthread_mutex_t queuelock;
static sem_t nwork;

struct thread_info {
    pthread_t tid;
    int id;
    int runningp;
};
struct thread_info threads[NTHREADS];

void *
threadfunc(void *arg)
{
    struct thread_info *info;
    struct work *work;
    int rval;

    assert(arg != NULL);
    info = (struct thread_info *) arg;

    printf("%2d: Started\n", info->id);
    while (info->runningp) {
        /* wait until there's work */
        printf("%2d: Waiting for work\n", info->id);
        sem_wait(&nwork);
        printf("%2d: Work received\n", info->id);
        if (!info->runningp) {
            break;
        }
        
        /* safely dequeue an item from the work queue */
        pthread_mutex_lock(&queuelock);
        assert(workhead); /* there should be work to do */
        work = workhead;
        workhead = work->next;
        if (workhead == NULL)
            worktail = NULL;
        pthread_mutex_unlock(&queuelock);
        
        rval = work->fn((void *) info->id);
        printf("%2d: Work completed with code %d\n", info->id, rval);
    }
    printf("%2d: Exiting\n", info->id);
    return 0;
}

static int runningp = 1;

static void
on_signal(int sig)
{
    runningp = 0;
    signal(sig, SIG_IGN);
}

static int
abc(void *arg)
{
    int i;
    pthread_t id;

    id = (pthread_t) arg;

    for (i = 0; i < 10; ++i) {
        printf("%2lu: %c\n", id, 'a' + i);
        sleep(1);
    }
    return 0;
}

int
main(int argc, char *argv[])
{
    pthread_attr_t ta;
    int rval;
    int i;
    void *ecode;
    struct work *work;

    signal(SIGINT, on_signal);

    memset(threads, 0, sizeof threads);

    printf(" M: Initializing queue lock\n");
    rval = pthread_mutex_init(&queuelock, NULL);
    if (rval) {
        fprintf(stderr, " M: Can't initialize queue mutex\n");
        return EXIT_FAILURE;
    }
    printf(" M: Initializing thread attributes\n");
    rval = pthread_attr_init(&ta);
    if (rval) {
        fprintf(stderr, " M: Can't initialize thread attributes\n");
        return EXIT_FAILURE;
    }
    printf(" M: Initializing nwork semaphore\n");
    sem_init(&nwork, 0, 0);
    printf(" M: Spawning %d threads\n", NTHREADS);
    for (i = 0; i < NTHREADS; ++i) {
        threads[i].id = i+1;
        threads[i].runningp = 1;
        rval = pthread_create(&threads[i].tid, &ta, threadfunc, &threads[i]);
        if (rval) {
            fprintf(stderr, " M: Can't create thread\n");
            break;
        }
    }
    printf(" M: Destroying thread attributes\n");
    pthread_attr_destroy(&ta);
    while (runningp) {
        /* generate work */
        printf(" M: Generating work\n");
        pthread_mutex_lock(&queuelock);
        work = calloc(1, sizeof *work);
        work->fn = abc;
        if (workhead == NULL) {
            workhead = worktail = work;
        } else {
            worktail->next = work;
            worktail = work;
        }
        pthread_mutex_unlock(&queuelock);
        sem_post(&nwork);
        sleep(1);
    }
    printf(" M: Stopping threads\n");
    for (i = 0; i < NTHREADS; ++i) {
        threads[i].runningp = 0;
    }
    for (i = 0; i < NTHREADS; ++i) {
        sem_post(&nwork);
    }
    printf(" M: Joining threads\n");
    for (i = 0; i < NTHREADS; ++i) {
        rval = pthread_join(threads[i].tid, &ecode);
        if (rval) {
            fprintf(stderr, " M: Can't join thread %d\n", threads[i].id);
            return EXIT_FAILURE;
        }
    }
    printf(" M: Exiting\n");
    return EXIT_SUCCESS;
}
