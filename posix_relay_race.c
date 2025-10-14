#include <fcntl.h>
#include <mqueue.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/msg.h>
#include <stdarg.h>

#ifndef PAGE_SIZE
  #define PAGE_SIZE 4096
#endif

struct message_buffer {  
  long message_type;
  int sender_id;
};

#define NO_PRIORITY 0
#define BUFFER_SIZE 256

void judge_process(mqd_t* queue_descriptors, int runners_count);
void runner_process(mqd_t* queue_descriptors, int runners_count, int runner_id);
const char* get_source_name(char* input_string);

int safe_file_open(const char* filename, int open_flags, int file_mode) {
  int file_desc = 0;
  if(file_mode == 0)
    file_desc = open(filename, open_flags);
  else
    file_desc = open(filename, open_flags, file_mode);
  
  if(file_desc < 0)
    perror(filename);

  return file_desc;
}

int safe_file_close(int file_desc, const char* filename) {
  int result = close(file_desc);
  if(result ==  -1)
    perror(filename);
  
  return result;
}

ssize_t safe_file_write(int file_desc, const char* filename, char* buffer, ssize_t bytes_count)
{
  ssize_t bytes_written = 0;

  while(true) {
    buffer += bytes_written; bytes_count -= bytes_written;
    bytes_written = write(file_desc, buffer, bytes_count);
    if(bytes_written < 0) {
      if(errno != EINTR) {
          perror(filename);
          return bytes_written;
      }
      else continue;
    }
    else if(bytes_written == bytes_count) return bytes_written;
    else if(bytes_written == 0) break;
    else continue;
  }
  return 0;
}

int file_descriptor_write(int source_fd, int dest_fd, const char* source_filename,
             const char *dest_filename, char *buffer) {
  while(true) {
    ssize_t bytes_read = read(source_fd, buffer, PAGE_SIZE);
    if (bytes_read < 0){
        perror(source_filename);
        return bytes_read;
    }
    else if(bytes_read > 0) {
      int result = safe_file_write(dest_fd, dest_filename, buffer, bytes_read);
      if(result < 0)
          return result;
      continue;
    }
    else break;
  }

  return 0;
}

pid_t safe_process_fork(void) {
  pid_t process_id = fork();
  if(process_id == -1) {
    perror("fork");
    exit(-1);
  }

  return process_id;
}

int safe_pipe_create(int pipe_ends[2]) {
  int result = pipe(pipe_ends);
  if(result == - 1) {
    perror("pipe");
    exit(-1);
  }

  return result;
}

int safe_fd_duplicate(int source_fd, int target_fd) {
  int result = dup2(source_fd, target_fd);
  if(result == -1) {
    perror("dup2");
    exit(-1);
  }

  return result;
}

int safe_message_queue_get(key_t queue_key, int queue_flags) {
  int result = msgget(queue_key, queue_flags);
  if(result == -1) {
    perror("msgget");
    exit(-1);
  }

  return result;
}

int safe_message_send(int queue_id, const void* message_buffer, size_t message_size,
                int message_flags) {
  int result = msgsnd(queue_id, message_buffer, message_size, message_flags);
  if(result == -1) {
    perror("msgsnd");
    exit(-1);
  }

  return result;
}

ssize_t safe_message_receive(int queue_id, void* message_buffer, size_t message_size,
                    long message_type, int message_flags) {
  ssize_t result = msgrcv(queue_id, message_buffer, message_size, message_type, message_flags);
  if(result == -1) {
    perror("msgrcv");
    exit(-1);
  }

  return result;
}

int safe_message_control(int queue_id, int command, struct msqid_ds* buffer) {
  int result = msgctl(queue_id, command, buffer);
  if(result == -1) {
    perror("msgctl");
    exit(-1);
  }

  return result;
}   

mqd_t safe_message_queue_open(const char* queue_name, int open_flags, ...) {
  mqd_t queue_desc = {};
  if (open_flags & O_CREAT) {
    va_list arg_list;
    va_start(arg_list, open_flags);
    mode_t access_mode = va_arg(arg_list, mode_t);
    struct mq_attr* queue_attributes = va_arg(arg_list, struct mq_attr*);
    va_end(arg_list);

    queue_desc = mq_open(queue_name, open_flags, access_mode, queue_attributes);
  }
  else queue_desc = mq_open(queue_name, open_flags);

  if (queue_desc == (mqd_t) -1) {
    perror("mq_open");
    exit(-1);
  }
  return queue_desc;
}

int safe_message_queue_send(mqd_t queue_descriptor, const char *message_pointer,
                 size_t message_length, unsigned message_priority) {
  int result = mq_send(queue_descriptor, message_pointer, message_length, message_priority);
  if(result == -1) {
    perror("mq_send");
    exit(-1);
  }
  return result;
}

ssize_t safe_message_queue_receive(mqd_t queue_descriptor, char *message_pointer,
                        size_t message_length, unsigned *message_priority) {
  int result = mq_receive(queue_descriptor, message_pointer, message_length, message_priority);
  if(result == -1) {
    perror("mq_receive");
    exit(-1);
  }
  return result;
}

int safe_message_queue_close(mqd_t queue_id) {
  int result = mq_close(queue_id);
  if(result == -1) {
    perror("mq_close");
    exit(-1);
  }
  return result;
}

int safe_message_queue_unlink(const char* queue_name) {
  int result = mq_unlink(queue_name);
  if(result == -1) {
    perror("mq_unlink");
    exit(-1);
  }
  return result;
}

int main(int argument_count, char** argument_values){

  if(argument_count != 2) {
    fprintf(stderr, "неверное количество аргументов\n");
    exit(-1);
  }

  int runners_count = atoi(argument_values[1]);

  struct mq_attr queue_attributes = {
    .mq_flags = 0,
    .mq_maxmsg = 10,
    .mq_msgsize = BUFFER_SIZE,
  };
  char temp_buffer[BUFFER_SIZE] = {};
  mqd_t queue_descriptors[runners_count + 1] = {};
  for(int i = 0; i < runners_count + 1; ++i) {
    sprintf(temp_buffer, "/queue_%d", i);
    queue_descriptors[i] = safe_message_queue_open(temp_buffer, O_CREAT | O_RDWR, 0666, &queue_attributes);
  }
  
  pid_t judge_pid = safe_process_fork();
  if(judge_pid == 0) {
    judge_process(queue_descriptors, runners_count);
    exit(0);
  }

  for(int i = 0; i < runners_count; ++i) {
    pid_t runner_pid = safe_process_fork();
    if(runner_pid == 0) {
      runner_process(queue_descriptors, runners_count, i + 1);
      exit(0);
    }
  }

  for(int i = 0; i < runners_count + 1; ++i) {
    wait(NULL);
    safe_message_queue_close(queue_descriptors[i]);
    sprintf(temp_buffer, "/queue_%d", i);
    safe_message_queue_unlink(temp_buffer);
  }
}

void judge_process(mqd_t* queue_descriptors, int runners_count) {
  {
  printf("-Судья:    "
          "Привет! Я судья. Теперь я буду ждать %d бегунов...\n",
    runners_count); }

  char message_buffer[BUFFER_SIZE] = {};
  for (int i = 0; i < runners_count; ++i) {
    safe_message_queue_receive(queue_descriptors[0], message_buffer, BUFFER_SIZE, NO_PRIORITY);
    printf("-Судья:    "
          "Я дождался %s бегуна\n", message_buffer);
  }

  printf("-Судья:    "
          "Начинаю передавать эстафетную палочку первому бегуну\n");
  printf("-Судья:    " "На старт, внимание, марш!" "\n");
  struct timeval start_time, end_time;
  gettimeofday(&start_time, 0);

  sprintf(message_buffer, "судья");
  safe_message_queue_send(queue_descriptors[1], message_buffer, BUFFER_SIZE, NO_PRIORITY);

  safe_message_queue_receive(queue_descriptors[0], message_buffer, BUFFER_SIZE, NO_PRIORITY);
  gettimeofday(&end_time, 0);

  printf("-Судья:    "
      "Я получил эстафетную палочку от %s\n", get_source_name(message_buffer));
  double elapsed_time = (double)(end_time.tv_usec - start_time.tv_usec
                      + (1000000)*(end_time.tv_sec - start_time.tv_sec));
  printf("-Судья:    " "Эстафета завершена!\n\n");
  printf("Затраченное_время = %lf микросекунд" "\n", elapsed_time);
}

void runner_process(mqd_t* queue_descriptors, int runners_count, int runner_id) {
  printf("-Бегун %d: "
          "Я готов бежать\n", runner_id);

  char message_buffer[256] = {};
  sprintf(message_buffer, "%d", runner_id);
  safe_message_queue_send(queue_descriptors[0], message_buffer, 256, NO_PRIORITY);

  safe_message_queue_receive(queue_descriptors[runner_id], message_buffer, BUFFER_SIZE, NO_PRIORITY);
  printf("-Бегун %d: "
        "Я получил эстафетную палочку от %s "
        "и теперь начинаю бежать\n", runner_id, get_source_name(message_buffer));

  sprintf(message_buffer, "%d", runner_id);
  mqd_t target_queue = queue_descriptors[runner_id + 1];
  if(runner_id == runners_count) target_queue = queue_descriptors[0]; 
  safe_message_queue_send(target_queue, message_buffer, BUFFER_SIZE, NO_PRIORITY);
}

const char* get_source_name(char* input_string) {
  if (!strncmp(input_string, "судья", 5))  return "судьи";
  else if (!strncmp(input_string, "1\0", 2)) return "1-го бегуна";
  else if (!strncmp(input_string, "2\0", 2)) return "2-го бегуна";
  else if (!strncmp(input_string, "3\0", 2)) return "3-го бегуна";
  else {
    static char formatted_buffer[256] = {};
    sprintf(formatted_buffer, "%s-го бегуна", input_string);
    return formatted_buffer;
  }
  return input_string;
}

