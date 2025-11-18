#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

int fd1[2], fd2[2];
pid_t child_pid;
int parent_ready = 0;
int child_ready = 0;

void sigusr1_handler(int sig)
{
    child_ready = 1;
}

void sigusr2_handler(int sig)
{
    parent_ready = 1;
}

int main()
{
   size_t size;
   char resstring[20];
   char sendstring[20];

   if(pipe(fd1) < 0 || pipe(fd2) < 0)
   {
     printf("Can't open pipes\n");
     exit(-1);
   }

   signal(SIGUSR1, sigusr1_handler);
   signal(SIGUSR2, sigusr2_handler);

   child_pid = fork();

   if(child_pid < 0)
   {
      printf("Can't fork child\n");
      exit(-1);
   }
   else if (child_pid > 0)
   {
      close(fd1[0]);
      close(fd2[1]);

      strcpy(sendstring, "Hello from parent!");
      size = write(fd1[1], sendstring, strlen(sendstring) + 1);
      if(size <= 0)
      {
        printf("Can't write to pipe\n");
        exit(-1);
      }
      printf("Parent sent: %s\n", sendstring);

      while(!child_ready)
      {
          pause();
      }
      child_ready = 0;

      size = read(fd2[0], resstring, sizeof(resstring));
      if(size <= 0)
      {
         printf("Can't read from pipe\n");
         exit(-1);
      }
      printf("Parent received: %s\n", resstring);

      strcpy(sendstring, "Goodbye parent!");
      size = write(fd1[1], sendstring, strlen(sendstring) + 1);
      if(size <= 0)
      {
        printf("Can't write to pipe\n");
        exit(-1);
      }
      printf("Parent sent: %s\n", sendstring);

      while(!child_ready)
      {
          pause();
      }

      size = read(fd2[0], resstring, sizeof(resstring));
      if(size <= 0)
      {
         printf("Can't read from pipe\n");
         exit(-1);
      }
      printf("Parent received: %s\n", resstring);

      close(fd1[1]);
      close(fd2[0]);
      printf("Parent exit\n");
   }
   else
   {
      close(fd1[1]);
      close(fd2[0]);

      size = read(fd1[0], resstring, sizeof(resstring));
      if(size <= 0){
         printf("Can't read from pipe\n");
         exit(-1);
      }
      printf("Child received: %s\n", resstring);

      strcpy(sendstring, "Hello from child!");
      size = write(fd2[1], sendstring, strlen(sendstring) + 1);
      if(size <= 0){
        printf("Can't write to pipe\n");
        exit(-1);
      }
      printf("Child sent: %s\n", sendstring);

      kill(getppid(), SIGUSR1);

      size = read(fd1[0], resstring, sizeof(resstring));
      if(size <= 0){
         printf("Can't read from pipe\n");
         exit(-1);
      }
      printf("Child received: %s\n", resstring);

      strcpy(sendstring, "Goodbye child!");
      size = write(fd2[1], sendstring, strlen(sendstring) + 1);
      if(size <= 0)
      {
        printf("Can't write to pipe\n");
        exit(-1);
      }
      printf("Child sent: %s\n", sendstring);

      kill(getppid(), SIGUSR1);

      close(fd1[0]);
      close(fd2[1]);
      printf("Child exit\n");
   }

   return 0;
}