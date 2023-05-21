#ifndef SHELL_H
#define SHELL_H

#include "parser/ast.h"

struct tree_node;

typedef struct process {
    struct process *next;
    pid_t pid;
    pid_t pgid;
    int status;
    int foreground;
} process;


/*
 * Any value assigned to this will be displayed when asking the user for a
 * command. Do not assign any value to this if its value is NULL, as this
 * indicates the session is not interactive.
 */
extern char *prompt;

/* Called once when the shell starts. */
void initialize(void);

/* Called when a command has been read from the user. */
void run_command(node_t *node);
void parse_cmd(node_t *node);
int exec_redirect(node_t *node, int fd, int mode);
int handle_redirect(node_t *node);
int set_envvar(char *arg, int set);
void run_detached_command(node_t *node);

#endif
