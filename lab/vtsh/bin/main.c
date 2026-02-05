#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "../lib/vtsh.c"

#define MAX_INPUT 1024
#define MAX_ARGS 20


int main() {
    char input[MAX_INPUT];

    while (1) {
        if (fgets(input, MAX_INPUT, stdin) == NULL) {
            break;
        } else {
            int i = 0;
            for (i = 0; i < strlen(input); i++) {
                if (input[i] == '\n') {
                    input[i] = '\0';
                    break;
                }
            }
            if (strcmp(input, "e") == 0 || strcmp(input, "exit") == 0 ||
                strcmp(input, "q") == 0 || strcmp(input, "quit") == 0) {
                break;
            }
        }

        char *args[MAX_ARGS];

        if (strlen(input) == 0) {
            continue;
        }

        if (strcmp(input, "cat") == 0) {
            char line[MAX_INPUT];
            while (fgets(line, MAX_INPUT, stdin) != NULL) {
                line[strcspn(line, "\n")] = 0;

                if (strlen(line) == 0) break;

                printf("%s\n", line);
                fflush(stdout);
            }
        } else {
            parse_command(input, args);
        }
    }

    return 0;
}