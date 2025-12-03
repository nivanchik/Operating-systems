#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <time.h>
#include <errno.h>

#define SHM_KEY 0x1234          
#define MAX_MESSAGE_LEN 1024    
#define SHM_SIZE 4096         
#define RT_SIGNAL SIGRTMIN     

typedef struct {
    pid_t sender_pid;         
    pid_t receiver_pid;      
    char message[MAX_MESSAGE_LEN]; 
    int is_new;             
    time_t timestamp;      
    pid_t waiting_pid;        
} Message;

int shm_id = -1;
Message* shared_message = NULL;
pid_t my_pid;
pid_t partner_pid = 0;
volatile sig_atomic_t got_signal = 0;

void signal_handler(int sig, siginfo_t* info, void* context) {
    (void)context;
    
    if (sig == RT_SIGNAL) {
        printf("[DEBUG] Получен сигнал от PID %d\n", info->si_pid);
        fflush(stdout);

        if (partner_pid == 0 && shared_message->waiting_pid == my_pid) {
            partner_pid = info->si_pid;
            printf("[INFO] Автоподключение к PID %d\n", partner_pid);
            fflush(stdout);
        }
        
        got_signal = 1; 
    }
}

void init_shared_memory() {
  
    shm_id = shmget(SHM_KEY, SHM_SIZE, IPC_CREAT | 0666);
    if (shm_id == -1) {
        perror("shmget failed");
        exit(EXIT_FAILURE);
    }

    shared_message = (Message*)shmat(shm_id, NULL, 0);
    if (shared_message == (void*)-1) {
        perror("shmat failed");
        exit(EXIT_FAILURE);
    }
    
    printf("Разделяемая память инициализирована\n");
}

void cleanup_shared_memory() {
    if (shared_message != NULL && shared_message != (void*)-1) {
        if (shared_message->waiting_pid == my_pid) {
            shared_message->waiting_pid = 0;
        }
        shmdt(shared_message);
    }

    struct shmid_ds shm_info;
    if (shmctl(shm_id, IPC_STAT, &shm_info) == 0) {
        if (shm_info.shm_nattch == 0) {
            shmctl(shm_id, IPC_RMID, NULL);
            printf("Разделяемая память удалена\n");
        }
    }
}

void setup_signal_handler() {
    struct sigaction sa;
    
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = signal_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    
    if (sigaction(RT_SIGNAL, &sa, NULL) == -1) {
        perror("sigaction failed");
        exit(EXIT_FAILURE);
    }
    
    printf("Обработчик сигналов настроен\n");
}

void send_message(pid_t target_pid, const char* text) {
    if (target_pid <= 0) {
        printf("Ошибка: не указан получатель!\n");
        return;
    }

    sigset_t block_set;
    sigemptyset(&block_set);
    sigaddset(&block_set, RT_SIGNAL);
    sigprocmask(SIG_BLOCK, &block_set, NULL);

    shared_message->sender_pid = my_pid;
    shared_message->receiver_pid = target_pid;
    strncpy(shared_message->message, text, MAX_MESSAGE_LEN - 1);
    shared_message->message[MAX_MESSAGE_LEN - 1] = '\0';
    shared_message->timestamp = time(NULL);
    shared_message->is_new = 1;

    sigprocmask(SIG_UNBLOCK, &block_set, NULL);

    printf("[DEBUG] Отправка сообщения PID %d: %s\n", target_pid, text);
    fflush(stdout);
    
    if (kill(target_pid, RT_SIGNAL) == -1) {
        if (errno == ESRCH) {
            printf("Ошибка: процесс с PID %d не найден\n", target_pid);
        } else {
            perror("kill failed");
        }
        return;
    }
    
    printf("[Вы -> PID %d]: %s\n", target_pid, text);
    fflush(stdout);
}

void check_incoming_messages() {
    if (got_signal) {
        got_signal = 0;

        if (shared_message->is_new && shared_message->receiver_pid == my_pid) {
            printf("\n=================================\n");
            printf("[PID %d -> Вы]: %s\n", 
                   shared_message->sender_pid,
                   shared_message->message);
            printf("Время: %s", ctime(&shared_message->timestamp));
            printf("=================================\n");

            if (partner_pid == 0) {
                partner_pid = shared_message->sender_pid;
                printf("[INFO] Автоматически подключен к PID %d\n", partner_pid);
            }

            shared_message->is_new = 0;

            printf("> ");
            fflush(stdout);
        }
    }
}

int main(int argc, char* argv[]) {
    my_pid = getpid();

    if (argc > 2) {
        printf("Использование: %s [PID_партнера]\n", argv[0]);
        printf("Пример: %s 1234 - подключиться к процессу с PID 1234\n", argv[0]);
        printf("        %s      - запуск в режиме ожидания\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    if (argc == 2) {
        partner_pid = atoi(argv[1]);
        if (partner_pid <= 0) {
            printf("Ошибка: некорректный PID\n");
            return EXIT_FAILURE;
        }
    }

    init_shared_memory();
    setup_signal_handler();
    
    printf("=== MyTelegram ===\n");
    printf("Ваш PID: %d\n", my_pid);

    if (partner_pid == 0) {
        shared_message->waiting_pid = my_pid;
        printf("Режим ожидания. Ожидание подключения...\n");
        printf("Сообщите этот PID другому процессу: %d\n\n", my_pid);
    } else {
        printf("Подключено к процессу: %d\n\n", partner_pid);
    }
    
    printf("Команды:\n");
    printf("  /pid - показать PID партнера\n");
    printf("  /exit - выйти\n");
    printf("  /help - показать справку\n\n");

    char input[MAX_MESSAGE_LEN];
    printf("> ");
    fflush(stdout);
    
    while (1) {
        check_incoming_messages();

        fd_set readfds;
        struct timeval tv;
        
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        
        int retval = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
        
        if (retval == -1) {
            continue;
        } else if (retval == 0) {
            continue;
        }

        if (fgets(input, sizeof(input), stdin) != NULL) {
            input[strcspn(input, "\n")] = 0;

            if (strcmp(input, "/exit") == 0 || strcmp(input, "/quit") == 0) {
                break;
            }
            else if (strcmp(input, "/pid") == 0) {
                if (partner_pid > 0) {
                    printf("PID партнера: %d\n", partner_pid);
                } else {
                    printf("Еще не подключен к партнеру\n");
                }
            }
            else if (strcmp(input, "/help") == 0) {
                printf("Просто введите текст для отправки сообщения\n");
                printf("Команды: /pid, /exit, /help\n");
            }
            else if (strlen(input) == 0) {
                printf("> ");
                fflush(stdout);
                continue;
            }
            else {
                if (partner_pid > 0) {
                    send_message(partner_pid, input);
                } else {
                    printf("Ошибка: не подключен к партнеру!\n");
                    printf("Ожидание подключения или укажите PID при запуске\n");
                }
            }
            
            printf("> ");
            fflush(stdout);
        }
    }

    cleanup_shared_memory();
    printf("\nПрограмма завершена\n");
    
    return 0;
}