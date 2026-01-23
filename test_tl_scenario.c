/* Test that closely mirrors the tl.adb scenario:
 * - One task that waits (like EastWest with entry call)
 * - One task that runs to completion without blocking (like NorthSouth without accept)
 * This test verifies that non-blocking tasks don't corrupt the scheduler.
 */
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

pthread_mutex_t start_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t start_cond = PTHREAD_COND_INITIALIZER;
int start_called = 0;

void* north_south_task(void* arg) {
    /* This task runs to completion without blocking - no accept statement */
    printf("NorthSouth: Starting (no blocking)\n");
    printf("NorthSouth: Display - GREEN RED\n");
    printf("NorthSouth: Completed\n");
    return NULL;
}

void* east_west_task(void* arg) {
    /* This task waits for a signal (like accept Start) */
    printf("EastWest: Waiting for Start signal...\n");
    pthread_mutex_lock(&start_mutex);
    while (!start_called) {
        pthread_cond_wait(&start_cond, &start_mutex);
    }
    pthread_mutex_unlock(&start_mutex);
    
    printf("EastWest: Received Start signal\n");
    printf("EastWest: Display - RED RED\n");
    printf("EastWest: Completed\n");
    return NULL;
}

int main() {
    pthread_t ns_task, ew_task;
    
    printf("Starting Traffic Light Simulation...\n\n");
    
    /* Create both tasks */
    pthread_create(&ns_task, NULL, north_south_task, NULL);
    pthread_create(&ew_task, NULL, east_west_task, NULL);
    
    /* Simulate the main program calling EastWest.Start */
    sleep(1);  /* Give tasks time to start */
    printf("\nMain: Calling EastWest.Start\n");
    pthread_mutex_lock(&start_mutex);
    start_called = 1;
    pthread_cond_signal(&start_cond);
    pthread_mutex_unlock(&start_mutex);
    
    /* Wait for both tasks to complete */
    pthread_join(ns_task, NULL);
    pthread_join(ew_task, NULL);
    
    printf("\n=== Test completed successfully! ===\n");
    return 0;
}
