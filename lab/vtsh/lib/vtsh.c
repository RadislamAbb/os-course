#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>


#define MAX_INPUT 1024
#define MAX_ARGS 20

struct command {
    char *command;
    char *args[MAX_ARGS];
    int count_args;
};

struct command commands[1024] = {};

int execute_command(char **input) {
    fflush(stdout);

    if (strcmp(input[0], "./shell") == 0) {
        pid_t pid = vfork();
        if (pid == 0) {
            char *new_args[] = {"./shell", NULL};
            execv("./shell", new_args);
            _exit(127);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
            return WEXITSTATUS(status);
        }
        return -1;
    }

    pid_t pid = vfork();
    if (pid == 0) {
        if (strchr(input[0], '/') != NULL) {
            execv(input[0], input);
        }

        execvp(input[0], input);

        _exit(127);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        int exit_status = WEXITSTATUS(status);

        if (exit_status == 127) {
            printf("Command not found\n");
        }

        return exit_status;
    } else {
        return -1;
    }
}

int add_command(char *input, int i) {
    char *input_copy = strdup(input);
    char *token = strtok(input_copy, " \n\t\r");

    if (token == NULL) {
        free(input_copy);
        return -1;
    }

    commands[i].command = strdup(token);
    commands[i].args[0] = strdup(token);
    commands[i].count_args = 1;

    int arg_count = 1;
    while ((token = strtok(NULL, " \n\t\r")) != NULL) {
        commands[i].args[arg_count] = strdup(token);
        commands[i].count_args++;
        arg_count++;
    }
    commands[i].args[arg_count] = NULL;

    free(input_copy);
    return 0;
}

int parse_command(char *input, char **args) {
    char *input_copy = strdup(input);
    char *save_token;

    // чек на seq-команды
    if (strstr(input, ";") != NULL) {
        char *token = strtok_r(input_copy, ";", &save_token);
        int i = 0;
        while (token != NULL) {
            while (*token == ' ') token++;
            char *end = token + strlen(token) - 1;
            while (end > token && *end == ' ') end--;
            *(end + 1) = '\0';

            if (strlen(token) > 0) {
                add_command(token, i);
                execute_command(commands[i].args);
            }

            token = strtok_r(NULL, ";", &save_token);
            i++;
        }
        free(input_copy);
        return 0;
    } else {
        // Обычная команда без ;
        int i = 0;
        char *token = strtok_r(input_copy, " \n\t\r", &save_token);

        if (token == NULL) {
            free(input_copy);
            return 0;
        }

        while (token != NULL && i < MAX_ARGS - 1) {
            args[i++] = strdup(token);
            token = strtok_r(NULL, " \n\t\r", &save_token);
        }
        args[i] = NULL;

        int result = execute_command(args);

        for (int j = 0; j < i; j++) {
            free(args[j]);
        }

        free(input_copy);
        return result;
    }
}