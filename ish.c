#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

#define MAX_ARGS 64
#define MAX_COMMAND_LENGTH 1024
#define MAX_BACKGROUND_PROCESSES 10

struct BackgroundProcess {
    pid_t pid;
    int job_id;
    char command[MAX_COMMAND_LENGTH];
    int foreground;
};

struct BackgroundProcess background_processes[MAX_BACKGROUND_PROCESSES];
int num_background_processes = 0;

void execute_command(char *command, int stdout_fd, int stderr_fd) {
    char *args[MAX_ARGS];
    char *token;
    int i = 0;

    token = strtok(command, " ");
    while (token != NULL && i < MAX_ARGS - 1) {
        args[i++] = token;
        token = strtok(NULL, " ");
    }
    args[i] = NULL;

    pid_t pid = fork();
    if (pid == 0) {
        dup2(stdout_fd, STDOUT_FILENO);
        dup2(stderr_fd, STDERR_FILENO);

        execvp(args[0], args);
        perror("execvp");
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    } else {
        perror("fork");
        exit(EXIT_FAILURE);
    }
}

void add_background_process(pid_t pid, char *command) {
    if (num_background_processes < MAX_BACKGROUND_PROCESSES) {
        background_processes[num_background_processes].pid = pid;
        background_processes[num_background_processes].job_id = num_background_processes + 1;
        strcpy(background_processes[num_background_processes].command, command);
        printf("[%d] %d %s\n", background_processes[num_background_processes].job_id,
               background_processes[num_background_processes].pid, background_processes[num_background_processes].command);
        num_background_processes++;
    } else {
        fprintf(stderr, "Maximum number of background processes reached.\n");
    }
}

char hostname[100];

void handle_signals(int signal) {
    if (signal == SIGINT) {
        printf("\n%s%% ", hostname); // Print prompt
        fflush(stdout);
    } else if (signal == SIGTSTP) {
        // Handle Ctrl+Z for suspending process
         printf("\n%s%% ", hostname);
        fflush(stdout);
        // Do not suspend the current process, let it continue in the background
    } else if (signal == SIGCONT) {
        // Handle resuming suspended process
         printf("\n%s%% ", hostname);
        fflush(stdout);
    }
}


void handle_bg(char *token) {
    int job_id = atoi(token + 1); // Skip the '%'
    //print job_id and process id
    printf("[%d] %d\n", job_id, background_processes[job_id - 1].pid);
    for (int i = 0; i < num_background_processes; i++) {
        if (background_processes[i].job_id == job_id) {
            kill(background_processes[i].pid, SIGCONT);
            printf("[%d] Continued\t%s\n", background_processes[i].job_id, background_processes[i].command);
            return;
        }
    }
    fprintf(stderr, "Job %d not found.\n", job_id);
}

void handle_setenv(char *command) {
    char *name = strtok(command, " \t");
    char *value = strtok(NULL, " \t");
    if (name == NULL || value == NULL) {
        fprintf(stderr, "Error: setenv requires two arguments.\n");
    } else {
        if (setenv(name, value, 1) == -1) {
            perror("setenv");
        }
    }
}

void handle_unsetenv(char *command) {
    char *name = strtok(command, " \t");
    if (name == NULL) {
        fprintf(stderr, "Error: unsetenv requires one argument.\n");
    } else {
        if (unsetenv(name) == -1) {
            perror("unsetenv");
        }
    }
}

void handle_fg(char *command) {
    int job_id = atoi(command + 1); // Skip the '%'
    for (int i = 0; i < num_background_processes; i++) {
        if (background_processes[i].job_id == job_id) {
            kill(background_processes[i].pid, SIGCONT);
            int status;
            waitpid(background_processes[i].pid, &status, 0);
            // Remove the background process only if it was a foreground process
            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                for (int j = i; j < num_background_processes - 1; j++) {
                    background_processes[j] = background_processes[j + 1];
                }
                num_background_processes--;
            }
            return;
        }
    }
    fprintf(stderr, "Job %d not found.\n", job_id);
}

void handle_jobs() {
    if (num_background_processes > 0) {
        for (int i = 0; i < num_background_processes; i++) {
            int status;
            pid_t result = waitpid(background_processes[i].pid, &status, WNOHANG);
            if (result == -1) {
                perror("waitpid");
                continue;
            } else if (result == 0) {
                printf("[%d] %d %s\n", background_processes[i].job_id,
                       background_processes[i].pid, background_processes[i].command);
            } else {
                printf("[%d] Done\t\t%s\n", background_processes[i].job_id, background_processes[i].command);
                for (int j = i; j < num_background_processes - 1; j++) {
                    background_processes[j] = background_processes[j + 1];
                }
                num_background_processes--;
                i--; // Adjust i since we removed an element
            }
        }
    } else {
        printf("No background processes.\n");
    }
}

void handle_kill(char *command) {
    int job_id = atoi(command + 1); // Skip the '%'
    for (int i = 0; i < num_background_processes; i++) {
        if (background_processes[i].job_id == job_id) {
            kill(background_processes[i].pid, SIGTERM);
            // Ensure termination by sending CONT signal
            kill(background_processes[i].pid, SIGCONT);
            for (int j = i; j < num_background_processes - 1; j++) {
                background_processes[j] = background_processes[j + 1];
            }
            num_background_processes--;
            return;
        }
    }
    fprintf(stderr, "Job %d not found.\n", job_id);
}

void read_ishrc() {
    FILE *file = fopen(".ishrc", "r");
    if (file != NULL) {
        char line[MAX_COMMAND_LENGTH];
        while (fgets(line, sizeof(line), file)) {
            line[strcspn(line, "\n")] = '\0'; // Remove newline character
            if (strcmp(line, "ls") == 0) {
                execute_command(line, STDOUT_FILENO, STDERR_FILENO);
            } else if (strncmp(line, "setenv", 6) == 0) {
                handle_setenv(line + 7); // Skip "setenv "
            }
        }
        fclose(file);
    }
}

void handle_setenv(char *command);
void handle_unsetenv(char *command);
void handle_fg(char *command);
void handle_jobs();
void handle_kill(char *command);

int main() {
    char command[MAX_COMMAND_LENGTH];

    if (gethostname(hostname, sizeof(hostname)) == -1) {
        perror("gethostname");
        exit(EXIT_FAILURE);
    }

      // Signal handling
    signal(SIGINT, handle_signals);
    signal(SIGTSTP, handle_signals);
    signal(SIGCONT, handle_signals);

    read_ishrc(); // Read .ishrc file if it exists
    while (1) {
        printf("%s%% ", hostname);
        fflush(stdout);

        fgets(command, sizeof(command), stdin);
        command[strcspn(command, "\n")] = '\0';

        // Check for exit command
        if (strcmp(command, "exit") == 0) {
            printf("Exiting shell...\n");
            break;
        }

        // Check for cd command
        if (strncmp(command, "cd", 2) == 0) {
            char *dir = strtok(command + 2, " \t\n");
            if (dir == NULL) {
                fprintf(stderr, "Error: No directory specified for cd command.\n");
            } else {
                if (chdir(dir) == -1) {
                    perror("chdir");
                }
            }
            continue; // Skip executing other commands
        }

        if (strncmp(command, "setenv", 6) == 0) {
            handle_setenv(command + 7); // Skip "setenv "
            continue;
        } else if (strncmp(command, "unsetenv", 8) == 0) {
            handle_unsetenv(command + 9); // Skip "unsetenv "
            continue;
        } else if (strncmp(command, "bg", 2) == 0) {
            handle_bg(command + 3); // Skip "bg "
            continue;
        } else if (strncmp(command, "fg", 2) == 0) {
            handle_fg(command + 3); // Skip "fg "
            continue;
        } else if (strncmp(command, "jobs", 4) == 0) {
            handle_jobs();
            continue;
        } else if (strncmp(command, "kill", 4) == 0) {
            handle_kill(command + 5); // Skip "kill "
            continue;
        }
        // Handle other commands
        char *token = strtok(command, ";");
        while (token != NULL) {
            char *pipe_token = strchr(token, '|');
            if (pipe_token != NULL) {
                *pipe_token = '\0';
                char *command1 = token;
                char *command2 = pipe_token + 1;

                int pipe_fd[2];
                if (pipe(pipe_fd) == -1) {
                    perror("pipe");
                    exit(EXIT_FAILURE);
                }

                pid_t pid1 = fork();
                if (pid1 == 0) {
                    dup2(pipe_fd[1], STDOUT_FILENO);
                    close(pipe_fd[0]);
                    close(pipe_fd[1]);
                    execute_command(command1, STDOUT_FILENO, STDERR_FILENO);
                    exit(EXIT_SUCCESS);
                } else if (pid1 > 0) {
                    pid_t pid2 = fork();
                    if (pid2 == 0) {
                        dup2(pipe_fd[0], STDIN_FILENO);
                        close(pipe_fd[0]);
                        close(pipe_fd[1]);
                        execute_command(command2, STDOUT_FILENO, STDERR_FILENO);
                        exit(EXIT_SUCCESS);
                    } else if (pid2 > 0) {
                        close(pipe_fd[0]);
                        close(pipe_fd[1]);
                        waitpid(pid1, NULL, 0);
                        waitpid(pid2, NULL, 0);
                    } else {
                        perror("fork");
                        exit(EXIT_FAILURE);
                    }
                } else {
                    perror("fork");
                    exit(EXIT_FAILURE);
                }
            } else {
                int background = 0;
                if (token[strlen(token) - 1] == '&') {
                    background = 1;
                    token[strlen(token) - 1] = '\0';
                }

                pid_t pid = fork();
                if (pid == 0) {
                    // Check for input redirection
                    char *redirect_in = strchr(token, '<');
                    if (redirect_in != NULL) {
                        *redirect_in = '\0';
                        char *filename = strtok(redirect_in + 1, " ");
                        while (filename != NULL && (*filename == ' ' || *filename == '\t')) {
                            filename++;
                        }
                        if (filename != NULL && *filename != '\0') {
                            int fd = open(filename, O_RDONLY);
                            if (fd == -1) {
                                perror("open");
                                exit(EXIT_FAILURE);
                            }
                            dup2(fd, STDIN_FILENO);
                            close(fd);
                            execute_command(token, STDOUT_FILENO, STDERR_FILENO);
                        } else {
                            fprintf(stderr, "Error: No input file specified after '<'\n");
                            exit(EXIT_FAILURE);
                        }
                    } else if (strstr(token, "&>") != NULL) {
                        // Handle &> redirection
                        char *cmd = strtok(token, "&>");
                        char *filename = strtok(NULL, "&>");
                        while (filename != NULL && (*filename == ' ' || *filename == '\t')) {
                            filename++;
                        }
                        if (filename != NULL && *filename != '\0') {
                            int fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0666);
                            if (fd == -1) {
                                perror("open");
                                exit(EXIT_FAILURE);
                            }
                            dup2(fd, STDOUT_FILENO); // Redirect stdout
                            dup2(fd, STDERR_FILENO); // Redirect stderr
                            close(fd);
                            execute_command(cmd, STDOUT_FILENO, STDERR_FILENO); // Execute the command
                        } else {
                            fprintf(stderr, "Error: No output file specified after '&>'\n");
                            exit(EXIT_FAILURE);
                        }
                    } else if (strstr(token, ">>&") != NULL) {
                        // Handle stdout/stderr append
                        char *cmd = strtok(token, ">>&");
                        char *filename = strtok(NULL, ">>&");
                        while (filename != NULL && (*filename == ' ' || *filename == '\t')) {
                            filename++;
                        }
                        if (filename != NULL && *filename != '\0') {
                            int fd = open(filename, O_CREAT | O_WRONLY | O_APPEND, 0666);
                            if (fd == -1) {
                                perror("open");
                                exit(EXIT_FAILURE);
                            }
                            dup2(fd, STDOUT_FILENO); // Redirect stdout
                            dup2(fd, STDERR_FILENO); // Redirect stderr
                            close(fd);
                            execute_command(cmd, STDOUT_FILENO, STDERR_FILENO); // Execute the command
                        } else {
                            fprintf(stderr, "Error: No output file specified after '>>&'\n");
                            exit(EXIT_FAILURE);
                        }
                    } else if (strstr(token, ">>") != NULL) {
                        // Handle stdout append
                        char *cmd = strtok(token, ">>");
                        char *filename = strtok(NULL, ">>");
                        while (filename != NULL && (*filename == ' ' || *filename == '\t')) {
                            filename++;
                        }
                        if (filename != NULL && *filename != '\0') {
                            int fd = open(filename, O_CREAT | O_WRONLY | O_APPEND, 0666);
                            if (fd == -1) {
                                perror("open");
                                exit(EXIT_FAILURE);
                            }
                            dup2(fd, STDOUT_FILENO); // Redirect stdout
                            close(fd);
                            execute_command(cmd, STDOUT_FILENO, STDERR_FILENO); // Execute the command
                        } else {
                            fprintf(stderr, "Error: No output file specified after '>>'\n");
                            exit(EXIT_FAILURE);
                        }
                    } else if (strstr(token, ">") != NULL) {
                        // Handle stdout redirection
                        char *cmd = strtok(token, ">");
                        char *filename = strtok(NULL, ">");
                        while (filename != NULL && (*filename == ' ' || *filename == '\t')) {
                            filename++;
                        }
                        if (filename != NULL && *filename != '\0') {
                            int fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0666);
                            if (fd == -1) {
                                perror("open");
                                exit(EXIT_FAILURE);
                            }
                            dup2(fd, STDOUT_FILENO); // Redirect stdout
                            close(fd);
                            execute_command(cmd, STDOUT_FILENO, STDERR_FILENO); // Execute the command
                        } else {
                            fprintf(stderr, "Error: No output file specified after '>'\n");
                            exit(EXIT_FAILURE);
                        }
                    } else {
                        execute_command(token, STDOUT_FILENO, STDERR_FILENO);
                    }
                    exit(EXIT_SUCCESS);
                } else if (pid > 0) {
                    if (!background) {
                        int status;
                        waitpid(pid, &status, 0);
                    } else {
                        add_background_process(pid, token);
                    }
                } else {
                    perror("fork");
                    exit(EXIT_FAILURE);
                }
            }

            token = strtok(NULL, ";");
        }
        // Check for finished background processes
        int i = 0;
        while (i < num_background_processes) {
            pid_t pid = waitpid(background_processes[i].pid, NULL, WNOHANG);
            if (pid > 0) {
                printf("[%d]+  Done\t\t%s\n", background_processes[i].job_id, background_processes[i].command);
                for (int j = i; j < num_background_processes - 1; j++) {
                    background_processes[j] = background_processes[j + 1];
                }
                num_background_processes--;
            } else if (pid == -1) {
                perror("waitpid");
            } else {
                i++;
            }
        }
    }

    return 0;
}








