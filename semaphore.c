#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

#define SEM_KEY 1234

#define MUTEX 0        // взаимное исключение для доступа к счетчикам
#define MEN_COUNT 1    // счетчик мужчин в душевой
#define WOMEN_COUNT 2  // счетчик женщин в душевой
#define ROOM_EMPTY 3   // семафор для контроля пустоты помещения (1 - свободно, 0 - занято)
#define GENDER_MUTEX 4 // семафор для контроля смены пола (1 - можно менять, 0 - нельзя)
#define MEN_QUEUE 5    // очередь мужчин
#define WOMEN_QUEUE 6  // очередь женщин

int n_cabins = 0;
int n_men = 0;
int n_women = 0;

void P(int sem_id, int sem_num) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = -1;
    op.sem_flg = 0;
    semop(sem_id, &op, 1);
}

void V(int sem_id, int sem_num) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = 1;
    op.sem_flg = 0;
    semop(sem_id, &op, 1);
}

void print_usage(const char* program_name) {
    printf("Использование: %s <кабинки> <мужчины> <женщины>\n", program_name);
    printf("Параметры:\n");
    printf("  кабинки  - количество кабинок в душевой (положительное число)\n");
    printf("  мужчины  - количество мужчин (неотрицательное число)\n");
    printf("  женщины  - количество женщин (неотрицательное число)\n\n");
    printf("Пример: %s 3 5 4\n", program_name);
}

int parse_arguments(int argc, char* argv[]) {
    if (argc != 4) {
        print_usage(argv[0]);
        return -1;
    }

    n_cabins = atoi(argv[1]);
    if (n_cabins <= 0) {
        printf("Ошибка: количество кабинок должно быть положительным числом!\n");
        return -1;
    }

    n_men = atoi(argv[2]);
    if (n_men < 0) {
        printf("Ошибка: количество мужчин не может быть отрицательным!\n");
        return -1;
    }

    n_women = atoi(argv[3]);
    if (n_women < 0) {
        printf("Ошибка: количество женщин не может быть отрицательным!\n");
        return -1;
    }
    
    if (n_men == 0 && n_women == 0) {
        printf("Ошибка: общее количество посетителей должно быть больше нуля!\n");
        return -1;
    }
    
    return 0;
}

void man_process(int id, int sem_id) {
    printf("Мужчина %d подошел к душевой\n", id);
    
    P(sem_id, MUTEX);

    int women_count = semctl(sem_id, WOMEN_COUNT, GETVAL);
    if (women_count > 0) {
        printf("Мужчина %d ждет, пока женщины закончат\n", id);
        V(sem_id, MUTEX);
        P(sem_id, ROOM_EMPTY);
        P(sem_id, MUTEX);
    }

    int men_count = semctl(sem_id, MEN_COUNT, GETVAL);
    if (men_count == 0) {

        printf("Мужчина %d первый вошел в душевую (помещение занято мужчинами)\n", id);
        P(sem_id, GENDER_MUTEX);
    }

    if (men_count < n_cabins) {
        semctl(sem_id, MEN_COUNT, SETVAL, men_count + 1);
        printf("Мужчина %d занял кабинку (занято %d/%d)\n", id, men_count + 1, n_cabins);
        V(sem_id, MUTEX);
    } else {
        printf("Мужчина %d ждёт своей очереди (все %d кабинок заняты)\n", id, n_cabins);
        V(sem_id, MUTEX);
        
        P(sem_id, MEN_QUEUE);
        
        P(sem_id, MUTEX);
        men_count = semctl(sem_id, MEN_COUNT, GETVAL);
        semctl(sem_id, MEN_COUNT, SETVAL, men_count + 1);
        printf("Мужчина %d занял кабинку (занято %d/%d)\n", id, men_count + 1, n_cabins);
        V(sem_id, MUTEX);
    }
    
    printf("Мужчина %d принимает душ\n", id);
    sleep(1 + rand() % 3);
    
    P(sem_id, MUTEX);
    men_count = semctl(sem_id, MEN_COUNT, GETVAL);
    semctl(sem_id, MEN_COUNT, SETVAL, men_count - 1);
    printf("Мужчина %d освободил кабинку (теперь занято %d/%d)\n", id, men_count - 1, n_cabins);

    printf("Мужчина %d вышел из душа\n", id);
    
    int men_waiting = semctl(sem_id, MEN_QUEUE, GETVAL) < 0 ? 0 : n_cabins - semctl(sem_id, MEN_QUEUE, GETVAL);
    if (men_waiting > 0 && (men_count - 1) < n_cabins) {
        V(sem_id, MEN_QUEUE);
    }
    
    if (men_count - 1 == 0) {
        printf("Последний мужчина вышел, помещение свободно для женщин\n");
        V(sem_id, GENDER_MUTEX);
        V(sem_id, ROOM_EMPTY);
    }
    
    V(sem_id, MUTEX);
    
    exit(0);
}

void woman_process(int id, int sem_id) {
    printf("Женщина %d подошла к душевой\n", id);
    
    P(sem_id, MUTEX);

    int men_count = semctl(sem_id, MEN_COUNT, GETVAL);
    if (men_count > 0) {
        printf("Женщина %d ждет, пока мужчины закончат\n", id);
        V(sem_id, MUTEX);
        P(sem_id, GENDER_MUTEX); 
        V(sem_id, GENDER_MUTEX);
        P(sem_id, MUTEX);
    }

    int women_count = semctl(sem_id, WOMEN_COUNT, GETVAL);
    if (women_count == 0) {
        printf("Женщина %d первая вошла в душевую (помещение занято женщинами)\n", id);
        P(sem_id, GENDER_MUTEX);
    }

    if (women_count < n_cabins) {
        semctl(sem_id, WOMEN_COUNT, SETVAL, women_count + 1);
        printf("Женщина %d заняла кабинку (занято %d/%d)\n", id, women_count + 1, n_cabins);
        V(sem_id, MUTEX);
    } else {
        printf("Женщина %d ждёт своей очереди (все %d кабинок заняты)\n", id, n_cabins);
        V(sem_id, MUTEX);

        P(sem_id, WOMEN_QUEUE);

        P(sem_id, MUTEX);
        women_count = semctl(sem_id, WOMEN_COUNT, GETVAL);
        semctl(sem_id, WOMEN_COUNT, SETVAL, women_count + 1);
        printf("Женщина %d заняла кабинку (занято %d/%d)\n", id, women_count + 1, n_cabins);
        V(sem_id, MUTEX);
    }

    printf("Женщина %d принимает душ\n", id);
    sleep(1 + rand() % 3);

    P(sem_id, MUTEX);
    women_count = semctl(sem_id, WOMEN_COUNT, GETVAL);
    semctl(sem_id, WOMEN_COUNT, SETVAL, women_count - 1);
    printf("Женщина %d освободила кабинку (теперь занято %d/%d)\n", id, women_count - 1, n_cabins);

    printf("Женщина %d вышла из душа\n", id);

    int women_waiting = semctl(sem_id, WOMEN_QUEUE, GETVAL) < 0 ? 0 : n_cabins - semctl(sem_id, WOMEN_QUEUE, GETVAL);
    if (women_waiting > 0 && (women_count - 1) < n_cabins) {
        V(sem_id, WOMEN_QUEUE);
    }

    if (women_count - 1 == 0) {
        printf("Последняя женщина вышла, помещение свободно для мужчин\n");
        V(sem_id, GENDER_MUTEX);
        V(sem_id, ROOM_EMPTY);
    }
    
    V(sem_id, MUTEX);
    
    exit(0);
}

int main(int argc, char* argv[]) {
    srand(time(NULL));
    
    if (parse_arguments(argc, argv) != 0) {
        return 1;
    }
    
    printf("=== Симуляция душевого помещения ===\n");
    printf("Кабинки: %d, Мужчины: %d, Женщины: %d\n\n", n_cabins, n_men, n_women);

    int sem_id = semget(SEM_KEY, 7, IPC_CREAT | 0666);
    if (sem_id == -1) {
        perror("Ошибка при создании семафоров");
        exit(1);
    }
    
    union semun arg;
    
    arg.val = 1;
    if (semctl(sem_id, MUTEX, SETVAL, arg) == -1) {
        perror("Ошибка инициализации MUTEX");
        exit(1);
    }
    
    arg.val = 0;
    if (semctl(sem_id, MEN_COUNT, SETVAL, arg) == -1) {
        perror("Ошибка инициализации MEN_COUNT");
        exit(1);
    }
    
    arg.val = 0;
    if (semctl(sem_id, WOMEN_COUNT, SETVAL, arg) == -1) {
        perror("Ошибка инициализации WOMEN_COUNT");
        exit(1);
    }
    
    arg.val = 1;
    if (semctl(sem_id, ROOM_EMPTY, SETVAL, arg) == -1) {
        perror("Ошибка инициализации ROOM_EMPTY");
        exit(1);
    }
    
    arg.val = 1;
    if (semctl(sem_id, GENDER_MUTEX, SETVAL, arg) == -1) {
        perror("Ошибка инициализации GENDER_MUTEX");
        exit(1);
    }
    
    arg.val = 0;
    if (semctl(sem_id, MEN_QUEUE, SETVAL, arg) == -1) {
        perror("Ошибка инициализации MEN_QUEUE");
        exit(1);
    }
    
    arg.val = 0;
    if (semctl(sem_id, WOMEN_QUEUE, SETVAL, arg) == -1) {
        perror("Ошибка инициализации WOMEN_QUEUE");
        exit(1);
    }

    pid_t pid;
    int i;

    for (i = 0; i < n_men; i++) {
        pid = fork();
        if (pid == 0) {
            man_process(i + 1, sem_id);
            exit(0);
        } else if (pid < 0) {
            perror("Ошибка при создании процесса");
            exit(1);
        }
        usleep(100000 + rand() % 200000);
    }
    
    for (i = 0; i < n_women; i++) {
        pid = fork();
        if (pid == 0) {
            woman_process(i + 1, sem_id);
            exit(0);
        } else if (pid < 0) {
            perror("Ошибка при создании процесса");
            exit(1);
        }
        usleep(100000 + rand() % 200000);
    }
    
    for (i = 0; i < n_men + n_women; i++) {
        wait(NULL);
    }
    
    printf("\nВсе %d посетителей прошли через душ!\n", n_men + n_women);
    
    if (semctl(sem_id, 0, IPC_RMID) == -1) {
        perror("Ошибка при удалении семафоров");
        exit(1);
    }
    
    return 0;
}