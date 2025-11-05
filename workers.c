#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>

typedef struct {
    int ready;
} shared_data_t;

void sem_wait(int semid, int semnum) {
    struct sembuf op = {semnum, -1, 0};
    semop(semid, &op, 1);
}

void sem_post(int semid, int semnum) {
    struct sembuf op = {semnum, 1, 0};
    semop(semid, &op, 1);
}

void worker_with_wrench(int id, int semid, shared_data_t *shared) {
    while(1) {
        sem_wait(semid, 0);
        printf("Рабочий %d с ключом: взял гайку\n", id);
        sleep(1);
        
        printf("Рабочий %d с ключом: закручиваю гайку\n", id);
        sleep(2);
        
        sem_wait(semid, 2);
        shared->ready++;
        int current_ready = shared->ready;
        sem_post(semid, 2);
        
        printf("Рабочий %d с ключом: гайка установлена. Всего: %d/3\n", id, current_ready);
        
        if(current_ready == 3) {
            sem_wait(semid, 2);
            if(shared->ready == 3) {
                printf("Рабочий %d с ключом: УСТРОЙСТВО ГОТОВО! Заменяю...\n", id);
                sleep(2);
                
                shared->ready = 0;
                sem_post(semid, 0);
                sem_post(semid, 0);
                sem_post(semid, 1);
                printf("Рабочий %d с ключом: замена завершена\n", id);
            }
            sem_post(semid, 2);
        }
        
        sleep(1);
    }
}

void worker_with_screwdriver(int id, int semid, shared_data_t *shared) {
    while(1) {
        sem_wait(semid, 1);
        printf("Рабочий %d с отверткой: беру винт\n", id);
        sleep(1);
        
        printf("Рабочий %d с отверткой: закручиваю винт\n", id);
        sleep(2);
        
        sem_wait(semid, 2);
        shared->ready++;
        int current_ready = shared->ready;
        sem_post(semid, 2);
        
        printf("Рабочий %d с отверткой: винт установлен. Всего: %d/3\n", id, current_ready);
        
        if(shared->ready == 3) {
            sem_wait(semid, 2);
            printf("Рабочий %d с отверткой: УСТРОЙСТВО ГОТОВО! Начинаю замену...\n", id);
            sleep(2);
            
            shared->ready = 0;
            sem_post(semid, 0);
            sem_post(semid, 0);     
            sem_post(semid, 1);
            
            printf("Рабочий %d с отверткой: замена завершена\n", id);
            sem_post(semid, 2);
        }
        
        sleep(1);
    }
}

int main() {
    pid_t workers[3];
    int semid, shmid;
    shared_data_t *shared;
    
    semid = semget(IPC_PRIVATE, 3, 0666 | IPC_CREAT);
    if(semid == -1) {
        perror("semget");
        exit(1);
    }
    
    semctl(semid, 0, SETVAL, 2);
    semctl(semid, 1, SETVAL, 1);
    semctl(semid, 2, SETVAL, 1);
    
    shmid = shmget(IPC_PRIVATE, sizeof(shared_data_t), 0666 | IPC_CREAT);
    if(shmid == -1) {
        perror("shmget");
        exit(1);
    }
    
    shared = (shared_data_t*)shmat(shmid, NULL, 0);
    if(shared == (void*)-1) {
        perror("shmat");
        exit(1);
    }
    
    shared->ready = 0;
    
    printf("=== Начало работы мастерской ===\n");
    printf("Доступно: 2 гайки, 1 винт\n");
    
    for(int i = 0; i < 3; i++) {
        workers[i] = fork();
        if(workers[i] == 0) {
            if(i < 2) {
                worker_with_wrench(i+1, semid, shared);
            } else {
                worker_with_screwdriver(i+1, semid, shared);
            }
            exit(0);
        } else if(workers[i] < 0) {
            perror("fork");
            exit(1);
        }
    }
    
    sleep(30);
    
    printf("\n=== Завершение работы мастерской ===\n");
    
    for(int i = 0; i < 3; i++) {
        kill(workers[i], SIGTERM);
    }
    
    for(int i = 0; i < 3; i++) {
        waitpid(workers[i], NULL, 0);
    }
    
    shmdt(shared);
    shmctl(shmid, IPC_RMID, NULL);
    semctl(semid, 0, IPC_RMID);
    
    printf("Все процессы завершены, ресурсы освобождены\n");
    
    return 0;
}