#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

typedef struct {char dir; int train_id;} thread_args;


pthread_mutex_t rng_mutex;

int thread_safe_rng(int min, int max) {
    pthread_mutex_lock(&rng_mutex);
    int r = rand();
    pthread_mutex_unlock(&rng_mutex);
    return min + r % max;
}

// NW, SW, SE, NE
pthread_mutex_t lock[4];
pthread_cond_t  cond[4];
volatile int inter[4] = {-1, -1, -1, -1};
pthread_mutex_t deadlock;
volatile int phantom_count[4] = {0, 0, 0, 0};
pthread_t train_threads[5000100];

char* id_to_dir(int id) {
    char* dir[4] = {"North", "West", "South", "East"};
    return dir[id];
}


void arriveLane(int inter_id, int train_id) {
    pthread_mutex_lock(&lock[inter_id]);
    while (inter[inter_id] != -1) {
        pthread_cond_wait(&cond[inter_id], &lock[inter_id]);
    }
    inter[inter_id] = inter_id;
    pthread_mutex_unlock(&lock[inter_id]);
}

int crossLane(int inter_id1, int inter_id2, int train_id) {
    pthread_mutex_lock(&lock[inter_id2]);
    while (inter[inter_id2] != -1) {
        pthread_cond_wait(&cond[inter_id2], &lock[inter_id2]);
    }
    pthread_mutex_lock(&deadlock);
    int is_phantom = 0;
    if (phantom_count[inter_id1] > 0) {
        phantom_count[inter_id1]--;
        is_phantom = 1;
        pthread_cond_broadcast(&cond[inter_id2]);
    } else {
        inter[inter_id2] = inter_id1;
    }
    pthread_mutex_unlock(&deadlock);
    pthread_mutex_unlock(&lock[inter_id2]);
    usleep(1000 * thread_safe_rng(500, 1000)); // take 500-1000 ms to cross the lane
    return is_phantom;
}

void exitLane(int inter_id1, int inter_id2) {
    pthread_mutex_lock(&lock[inter_id1]);
    inter[inter_id1] = -1;
    pthread_cond_signal(&cond[inter_id1]);
    pthread_mutex_unlock(&lock[inter_id1]);

    pthread_mutex_lock(&lock[inter_id2]);
    inter[inter_id2] = -1;
    pthread_cond_signal(&cond[inter_id2]);
    pthread_mutex_unlock(&lock[inter_id2]);
}

void trainThreadFunction(void* arg) {
    usleep(thread_safe_rng(0, 10000)); // start at random time
    thread_args* args = (thread_args*) arg;
    char trainDir = args->dir;
    int train_id = args->train_id;

    int inter_id = -1;
    if (trainDir == 'N') {
        inter_id = 0;
    } else if (trainDir == 'W') {
        inter_id = 1;
    } else if (trainDir == 'S') {
        inter_id = 2;
    } else if (trainDir == 'E') {
        inter_id = 3;
    }
    char* direction = id_to_dir(inter_id);
    arriveLane(inter_id, train_id);
    printf("Train Arrived at the lane from the %s direction\n", direction);

    int inter_id2 = (inter_id + 1) % 4;
    int is_phantom = crossLane(inter_id, inter_id2, train_id);
    if (!is_phantom){
        printf("Train Exited the lane from the %s direction\n", direction);
        exitLane(inter_id, inter_id2);
    }
}

void deadLockResolverThreadFunction(void * arg) {
    /* TODO extract arguments from the `void* arg` */
    while (1) {
        pthread_mutex_lock(&lock[0]);
        pthread_mutex_lock(&lock[1]);
        pthread_mutex_lock(&lock[2]);
        pthread_mutex_lock(&lock[3]);
        int deadLockDetected = 1; 
        for (int i = 0; i < 4; i++) {
            for (int j = i + 1; j < 4; j++){
                if (inter[i] == inter[j] || inter[i] == -1 || inter[j] == -1) {
                    deadLockDetected = 0;
                    break;
                }
            }
        }
        pthread_mutex_unlock(&lock[3]);
        pthread_mutex_unlock(&lock[2]);
        pthread_mutex_unlock(&lock[1]);
        pthread_mutex_unlock(&lock[0]);

        if (deadLockDetected) {
            printf("Deadlock detected. Resolving deadlock...\n");
            pthread_mutex_lock(&deadlock);
            int random_id = thread_safe_rng(0, 4);
            pthread_mutex_lock(&lock[random_id]);
            inter[random_id] = -1;
            phantom_count[random_id]++;
            printf("Train Exited the lane from the %s direction\n", id_to_dir(random_id));
            pthread_cond_signal(&cond[random_id]);
            pthread_mutex_unlock(&lock[random_id]);
            pthread_mutex_unlock(&deadlock);
        }
        usleep(1000 * 500); // sleep for 500 ms
    }
}


int main(int argc, char *argv[]) {
    srand(time(NULL));
    if (argc != 2) {
        printf("Usage: ./main <train dirs: [NSWE]+>\n");
        return 1;
    }
    pthread_mutex_init(&rng_mutex, NULL);
    pthread_mutex_init(&deadlock, NULL);
    pthread_mutex_init(&lock[0], NULL);
    pthread_mutex_init(&lock[1], NULL);
    pthread_mutex_init(&lock[2], NULL);
    pthread_mutex_init(&lock[3], NULL);

    pthread_cond_init(&cond[0], NULL);
    pthread_cond_init(&cond[1], NULL);
    pthread_cond_init(&cond[2], NULL);
    pthread_cond_init(&cond[3], NULL);

    pthread_t deadlock_resolver_thread;
    pthread_create(&deadlock_resolver_thread, NULL, (void*) deadLockResolverThreadFunction, NULL);

    char* train = argv[1];
    int num_trains = 0;

    while (train[num_trains] != '\0') {
        char trainDir = train[num_trains];
        if (trainDir != 'N' && trainDir != 'S' && trainDir != 'E' && trainDir != 'W') {
            printf("Invalid train direction: %c\n", trainDir);
            printf("Usage: ./main <train dirs: [NSEW]+>\n");
            return 1;
        }
        thread_args* args = malloc(sizeof(thread_args*));
        args->dir = trainDir;
        args->train_id = num_trains;
        pthread_create(&train_threads[num_trains], NULL, (void *)trainThreadFunction, args);
        num_trains++;
    }
    for (int i = 0; i < num_trains; i++) {
        pthread_join(train_threads[i], NULL);
    }
    return 0;
}