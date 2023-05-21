#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pwd.h>

#include "parser/ast.h"
#include "shell.h"
#include "util.h"


extern struct process *head;

int is_sleep_cmd(node_t *node)  {
    return node->type == NODE_COMMAND &&
           strcmp(node->command.argv[0], "sleep") == 0;
}

int create_pipes(int pipes[], int num_pipes)  {
    for(int i = 0; i < num_pipes / 2; i++)   {
        if(pipe(pipes + i*2) != 0)    {
            printf("pipe failed\n");
            return -1;
        }
    }
    return 0;
}

int close_pipes(int pipes[], int num_pipes)  {
    for(int i = 0; i < num_pipes; i++)  {
        if(close(pipes[i]) < 0) {
            return -1;
        }
    }
    return 0;
}

char* get_username() {
    struct passwd *p = getpwuid(getuid());
    if(!p)  {
        perror("getpwuid failed\n");
        return NULL;
    }
    return p->pw_name;
}

char *get_hostname()    {
    char *hostname = calloc(20, sizeof(char));
    if(gethostname(hostname, 20) != 0) {
        perror("failed to get hostname\n");
        return NULL;
    }
    return hostname;
}

char *get_cwd() {
    char *cwd = getcwd(NULL, 0);
    if(!cwd)    {
        perror("failed to get cwd\n");
        return NULL;
    }
    return cwd;
}

void replace_substring(char **src, char **str, char **dest, int *len) {
    memcpy(*dest + *len, *str, strlen(*str));
    *len += strlen(*str);
    *src += 2;
}

void move_to_fg(pid_t pid)   {
    pid_t pgid = getpgid(pid);
    tcsetpgrp(STDIN_FILENO, pgid);
}
