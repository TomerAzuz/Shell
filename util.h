#ifndef SHELL_UTIL_H
#define SHELL_UTIL_H

#define ACTIVE      0
#define STOPPED     1
#define COMPLETED   2

int is_sleep_cmd(node_t *node);
int create_pipes(int pipes[], int num_pipes);
int close_pipes(int pipes[], int num_pipes);
char* get_username();
char *get_hostname();
char *get_cwd();
void replace_substring(char **src, char **str, char **dest, int *len);
void move_to_fg(pid_t pid);

#endif
