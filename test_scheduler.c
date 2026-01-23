/* Simple test to verify the scheduler handles tasks without blocking correctly */
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
int flag = 0;

void* task_with_wait(void* arg) {
    printf("Task 1: Waiting on condition variable\n");
    pthread_mutex_lock(&mutex);
    while (!flag) {
        pthread_cond_wait(&cond, &mutex);
    }
    printf("Task 1: Signaled! Exiting.\n");
    pthread_mutex_unlock(&mutex);
    return NULL;
}

void* task_without_wait(void* arg) {
    printf("Task 2: Running to completion without blocking\n");
    sleep(1);  /* Give task 1 time to wait */
    printf("Task 2: Signaling task 1\n");
    pthread_mutex_lock(&mutex);
    flag = 1;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
    printf("Task 2: Done\n");
    return NULL;
}

int main() {
    pthread_t t1, t2;
    
    printf("Starting test with 2 tasks:\n");
    printf("- Task 1: Waits on condition variable\n");
    printf("- Task 2: Runs without blocking, then signals\n\n");
    
    pthread_create(&t1, NULL, task_with_wait, NULL);
    pthread_create(&t2, NULL, task_without_wait, NULL);
    
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    
    printf("\nTest completed successfully!\n");
    return 0;
}
