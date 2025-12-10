#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <semaphore.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

typedef struct {
    char c;
    sem_t* my_sem;
    sem_t* next_sem;
} thread_data;

void* print_char(void* arg) {
    thread_data* data = (thread_data*)arg;

    sem_wait(data->my_sem);

    printf("%c", data->c);

    if (data->next_sem != NULL) {
        sem_post(data->next_sem);
    }
    
    return NULL;
}

int main(int argc, char* argv[]) {
    char* message = "hello world";
    long len = 0;

    if (argc == 2) {
        int fd = open(argv[1], O_RDONLY);
        
        if (fd == -1) {
            perror("Error opening file");
            return 1;
        }

        struct stat st;
        if (fstat(fd, &st) == -1) {
            perror("Error getting file size");
            close(fd);
            return 1;
        }
        
        len = st.st_size;
        if (len <= 0) {
            printf("File is empty or error getting size\n");
            close(fd);
            return 1;
        }

        message = malloc(len + 1);
        if (message == NULL) {
            perror("Memory allocation failed");
            close(fd);
            return 1;
        }

        ssize_t bytes_read = read(fd, message, len);
        if (bytes_read == -1) {
            perror("Error reading file");
            free(message);
            close(fd);
            return 1;
        }
        
        message[bytes_read] = '\0';
        close(fd);

        if (bytes_read == 0) {
            len = 0;
            free(message);
            message = NULL;
        }
    } else {
        message = "hello world";
        len = strlen(message);
    }

    if (len == 0 || message == NULL) {
        printf("No message to print\n");
        return 0;
    }

    pthread_t* threads = malloc(len * sizeof(pthread_t));
    thread_data* data = malloc(len * sizeof(thread_data));
    sem_t* sems = malloc(len * sizeof(sem_t));
    
    if (threads == NULL || data == NULL || sems == NULL) {
        perror("Memory allocation failed");
        if (argc == 2) free((void*)message);
        free(threads);
        free(data);
        free(sems);
        return 1;
    }

    for (int i = 0; i < len; i++) {
        if (sem_init(&sems[i], 0, 0) != 0) {
            perror("Semaphore initialization failed");
            for (int j = 0; j < i; j++) {
                sem_destroy(&sems[j]);
            }
            if (argc == 2) free((void*)message);
            free(threads);
            free(data);
            free(sems);
            return 1;
        }
    }

    for (int i = 0; i < len; i++) {
        data[i].c = message[i];
        data[i].my_sem = &sems[i];
        data[i].next_sem = (i < len - 1) ? &sems[i + 1] : NULL;
        
        if (pthread_create(&threads[i], NULL, print_char, &data[i]) != 0) {
            perror("Thread creation failed");
            for (int j = 0; j < len; j++) {
                sem_destroy(&sems[j]);
            }
            if (argc == 2) free((void*)message);
            free(threads);
            free(data);
            free(sems);
            return 1;
        }
    }

    if (len > 0) {
        sem_post(&sems[0]);
    }

    for (int i = 0; i < len; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("\n");

    for (int i = 0; i < len; i++) {
        sem_destroy(&sems[i]);
    }
    
    if (argc == 2) {
        free((void*)message);
    }
    free(threads);
    free(data);
    free(sems);
    
    return 0;
}