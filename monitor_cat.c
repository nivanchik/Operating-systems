#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>

#define BUFFER_SIZE 4096
#define MAX_FILES 10

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    char** filenames;
    int file_count;
    int current_file;
    int active_threads;
} FileMonitor;

typedef struct {
    FileMonitor* monitor;
    int thread_id;
} ThreadData;

void init_monitor(FileMonitor* monitor, char** filenames, int file_count) {
    pthread_mutex_init(&monitor->mutex, NULL);
    pthread_cond_init(&monitor->condition, NULL);
    monitor->filenames = filenames;
    monitor->file_count = file_count;
    monitor->current_file = 0;
    monitor->active_threads = 0;
}

void destroy_monitor(FileMonitor* monitor) {
    pthread_mutex_destroy(&monitor->mutex);
    pthread_cond_destroy(&monitor->condition);
}

int get_next_file(FileMonitor* monitor) {
    pthread_mutex_lock(&monitor->mutex);
    
    int file_index = -1;
    
    if (monitor->current_file < monitor->file_count) {
        file_index = monitor->current_file;
        monitor->current_file++;
        monitor->active_threads++;
    }
    
    pthread_mutex_unlock(&monitor->mutex);
    return file_index;
}

void mark_file_complete(FileMonitor* monitor) {
    pthread_mutex_lock(&monitor->mutex);
    monitor->active_threads--;

    if (monitor->active_threads == 0 && monitor->current_file >= monitor->file_count) {
        pthread_cond_broadcast(&monitor->condition);
    }
    
    pthread_mutex_unlock(&monitor->mutex);
}

void process_file(const char* filename) {
    FILE* file;
    
    if (strcmp(filename, "-") == 0) {
        file = stdin;
    } else {
        file = fopen(filename, "r");
        if (file == NULL) {
            fprintf(stderr, "cat: %s: Нет такого файла или каталога\n", filename);
            return;
        }
    }
    
    char buffer[BUFFER_SIZE];
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        fwrite(buffer, 1, bytes_read, stdout);

        if (ferror(stdout)) {
            fprintf(stderr, "cat: Ошибка записи в stdout\n");
            break;
        }
    }

    if (ferror(file)) {
        fprintf(stderr, "cat: Ошибка чтения файла %s\n", filename);
    }
    
    if (file != stdin) {
        fclose(file);
    }
}

void* worker_thread(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    FileMonitor* monitor = data->monitor;
    
    while (1) {
        int file_index = get_next_file(monitor);
        
        if (file_index == -1) {
            break;
        }

        process_file(monitor->filenames[file_index]);
        
        mark_file_complete(monitor);
    }
    
    free(data);
    return NULL;
}

int main(int argc, char* argv[]) {
    char* filenames[MAX_FILES];
    int file_count = 0;
    
    if (argc == 1) {
        filenames[file_count++] = "-";
    } else {
        for (int i = 1; i < argc && file_count < MAX_FILES; i++) {
            filenames[file_count++] = argv[i];
        }
    }

    FileMonitor monitor;
    init_monitor(&monitor, filenames, file_count);

    int num_threads = 1;
    
    pthread_t threads[num_threads];
    ThreadData* thread_data;

    for (int i = 0; i < num_threads; i++) {
        thread_data = malloc(sizeof(ThreadData));
        thread_data->monitor = &monitor;
        thread_data->thread_id = i;
        
        if (pthread_create(&threads[i], NULL, worker_thread, thread_data) != 0) {
            fprintf(stderr, "Ошибка создания потока\n");
            exit(1);
        }
    }

    pthread_mutex_lock(&monitor.mutex);
    while (monitor.active_threads > 0 || monitor.current_file < monitor.file_count) {
        pthread_cond_wait(&monitor.condition, &monitor.mutex);
    }
    pthread_mutex_unlock(&monitor.mutex);

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    destroy_monitor(&monitor);

    if (fflush(stdout) != 0) {
        fprintf(stderr, "cat: Ошибка сброса буфера stdout\n");
        return 1;
    }
    
    return 0;
}