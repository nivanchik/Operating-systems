#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <sys/types.h>
#include <pwd.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>

#define PROC_DIR "/proc"
#define STATUS_FILE "status"
#define UID_PREFIX "Uid:"
#define MAX_LINE 256

typedef struct {
    uid_t uid;
    int count;
    char name[32];  // Будем хранить имя пользователя здесь
} uid_stat_t;

int is_numeric(const char *str) {
    if (!str || !*str) return 0;
    while (*str) {
        if (!isdigit(*str)) return 0;
        str++;
    }
    return 1;
}

uid_t get_process_uid(const char *pid_dir) {
    char path[PATH_MAX];
    FILE *fp;
    char line[MAX_LINE];
    uid_t uid = 0;
    
    snprintf(path, sizeof(path), "%s/%s/%s", PROC_DIR, pid_dir, STATUS_FILE);
    
    fp = fopen(path, "r");
    if (!fp) {
        return (uid_t)-1;  // Возвращаем специальное значение при ошибке
    }
    
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, UID_PREFIX, strlen(UID_PREFIX)) == 0) {
            sscanf(line + strlen(UID_PREFIX), "%u", &uid);
            break;
        }
    }
    
    fclose(fp);
    return uid;
}

// Получение имени пользователя по UID
void get_username(uid_t uid, char *name_buf, size_t buf_size) {
    struct passwd *pwd = getpwuid(uid);
    if (pwd && pwd->pw_name) {
        snprintf(name_buf, buf_size, "%s", pwd->pw_name);
    } else {
        // Если имя не найдено, используем числовой UID как строку
        snprintf(name_buf, buf_size, "%u", uid);
    }
}

int compare_uid_stat(const void *a, const void *b) {
    const uid_stat_t *stat_a = (const uid_stat_t *)a;
    const uid_stat_t *stat_b = (const uid_stat_t *)b;
    
    // Сначала сортируем по имени
    int name_cmp = strcmp(stat_a->name, stat_b->name);
    if (name_cmp != 0) return name_cmp;
    
    // Если имена совпадают, сортируем по UID
    return (stat_a->uid - stat_b->uid);
}

int main() {
    // Проверяем, запущен ли с правами root
    if (geteuid() != 0) {
        fprintf(stderr, "Warning: Running without root privileges. "
                        "Some processes may not be accessible.\n"
                        "Run with sudo for complete results.\n\n");
    }
    
    DIR *proc_dir;
    struct dirent *entry;
    uid_stat_t *stats = NULL;
    size_t stats_size = 0;
    size_t stats_capacity = 0;
    int skipped = 0;
    
    proc_dir = opendir(PROC_DIR);
    if (!proc_dir) {
        perror("opendir");
        return 1;
    }
    
    while ((entry = readdir(proc_dir)) != NULL) {
        if (entry->d_type == DT_DIR && is_numeric(entry->d_name)) {
            uid_t uid = get_process_uid(entry->d_name);
            
            // Пропускаем процессы с ошибкой чтения
            if (uid == (uid_t)-1) {
                skipped++;
                continue;
            }
            
            // Ищем UID в массиве
            int found = 0;
            for (size_t i = 0; i < stats_size; i++) {
                if (stats[i].uid == uid) {
                    stats[i].count++;
                    found = 1;
                    break;
                }
            }
            
            if (!found) {
                if (stats_size >= stats_capacity) {
                    stats_capacity = stats_capacity == 0 ? 64 : stats_capacity * 2;
                    uid_stat_t *new_stats = realloc(stats, 
                                                   stats_capacity * sizeof(uid_stat_t));
                    if (!new_stats) {
                        perror("realloc");
                        free(stats);
                        closedir(proc_dir);
                        return 1;
                    }
                    stats = new_stats;
                }
                
                stats[stats_size].uid = uid;
                stats[stats_size].count = 1;
                // Получаем имя пользователя сразу
                get_username(uid, stats[stats_size].name, 
                           sizeof(stats[stats_size].name));
                stats_size++;
            }
        }
    }
    
    closedir(proc_dir);
    
    if (skipped > 0) {
        fprintf(stderr, "Note: Skipped %d processes due to access restrictions\n", skipped);
    }
    
    // Сортируем по имени пользователя
    qsort(stats, stats_size, sizeof(uid_stat_t), compare_uid_stat);
    
    // Выводим результаты
    for (size_t i = 0; i < stats_size; i++) {
        printf("%s %d\n", stats[i].name, stats[i].count);
    }
    
    // Дополнительная информация
    if (stats_size > 0) {
        fprintf(stderr, "\nTotal unique users: %zu\n", stats_size);
    }
    
    free(stats);
    return 0;
}