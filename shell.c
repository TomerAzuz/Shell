#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <fcntl.h>
#include <pwd.h>

#include "parser/ast.h"
#include "shell.h"
#include "util.h"

pid_t curr_job = 0;

int gen_prompt(char *ps1) {
    char *prompt = calloc(1024, sizeof(char));

    char *userflag = strstr(ps1, "\\u");
    char *hostflag = strstr(ps1, "\\h");
    char *cwdflag = strstr(ps1, "\\w");
    int i = 0;

    while(*ps1 != '\0' && i < 1024)  {
        if(ps1 == userflag) {
            char *username = get_username();
            if(!username)   {
                return -1;
            }
            replace_substring(&ps1, &username, &prompt, &i);
            userflag = strstr(ps1, "\\u");
        }
        else if(ps1 == hostflag) {
            char *hostname = get_hostname();
            if(!hostname)   {
                return -1;
            }
            replace_substring(&ps1, &hostname, &prompt, &i);
            hostflag = strstr(ps1, "\\h");
            free(hostname);
        }
        else if(ps1 == cwdflag) {
            char *cwd = getcwd(NULL, 0);
            if(!cwd)   {
                return -1;
            }
            replace_substring(&ps1, &cwd, &prompt, &i);
            cwdflag = strstr(ps1, "\\w");
            free(cwd);
        }
        else    {
            prompt[i++] = *ps1;
            ps1++;
        }
    }
    puts(prompt);
    free(prompt);
    return 0;
}

int set_envvar(char *arg, int set)  {
    if(set) {
        char *val = strchr(arg, '=');
        val++;
        *(val - 1) = '\0';
        if(setenv(arg, val, 1) < 0) {
            perror("setenv failed\n");
            return -1;
        }
    }
    else    {
        if(unsetenv(arg) < 0) {
            perror("unsetenv failed\n");
            return -1;
        }
    }
    return 1;
}

size_t exec_sleep_pipe(node_t *node, size_t i)   {
    int max_sec = atoi(node->pipe.parts[i]->command.argv[1]);
    while(++i < node->pipe.n_parts)    {
        if(is_sleep_cmd(node->pipe.parts[i]))  {
            max_sec = max_sec > atoi(node->pipe.parts[i]->command.argv[1]) ?
                      max_sec : atoi(node->pipe.parts[i]->command.argv[1]);
        }
        else break;
    }
    sprintf(node->pipe.parts[i-1]->command.argv[1], "%d", max_sec);
    run_command(node->pipe.parts[i-1]);

    /* one command left, no need to pipe */
    if(i ==  node->pipe.n_parts-1)   {
        run_command(node->pipe.parts[i]);
        return 0;
    }
        /* no commands left */
    else if(i == node->pipe.n_parts) {
        return 0;
    }
    return i;
}

int exec_pipes(node_t *node) {
    int status;
    pid_t ppid;
    size_t num_cmds = node->pipe.n_parts;
    int num_pipes = (num_cmds - 1) * 2;
    int pipes[num_pipes];

    if(create_pipes(pipes, num_pipes) < 0)    {
        return -1;
    }
    for(size_t i = 0; i < num_cmds; i++)  {
        if(is_sleep_cmd(node->pipe.parts[i]))  {
            i = exec_sleep_pipe(node, i);
            if(i == 0)  {
                return 0;
            }
        }
        pid_t pid = fork();
        if(pid == 0) {
            if(i != 0)  {
                if(dup2(pipes[(i-1) * 2], 0) < 0)   {
                    return -1;
                }
            }
            if(i != num_cmds - 1)   {
                if(dup2(pipes[i*2 + 1], 1) < 0) {
                    return -1;
                }
            }
            if(close_pipes(pipes, num_pipes) < 0) {
                return -1;
            }
            if(node->pipe.parts[i]->type == NODE_SEQUENCE)  {
                run_command(node->pipe.parts[i]);
                while((ppid = wait(&status)) > 0);
            }
            else {
                run_command(node->pipe.parts[i]);
            }
        }
        curr_job = pid;
    }
    return close_pipes(pipes, num_pipes);
}

int exec_redirect(node_t *node, int fd, int mode) {
    if(fd < 0)  {
        perror("open failed\n");
        return -1;
    }
    pid_t pid = fork();
    if(pid < 0) {
        perror("fork failed\n");
        exit(1);
    }
    else if(pid == 0)   {
        if(dup2(fd, mode) < 0)  {
            perror("dup2 failed\n");
            return -1;
        }
        if(node->redirect.child->type != NODE_REDIRECT)   {
            execvp(node->redirect.child->command.argv[0], node->redirect.child->command.argv);
            exit(1);
        }
        else    {
            handle_redirect(node->redirect.child);
            exit(1);
        }
    }
    else    {
        curr_job = pid;
        int status;
        wait(&status);
    }
    return 0;
}

int handle_redirect(node_t *node) {
    int fd;
    switch (node->redirect.mode) {
        case REDIRECT_OUTPUT: {
            fd = open(node->redirect.target, O_WRONLY | O_TRUNC | O_CREAT, 0777);
            return exec_redirect(node, fd, STDOUT_FILENO);
        }
        case REDIRECT_INPUT: {
            fd = open(node->redirect.target, O_RDONLY | O_CREAT, 0777);
            return exec_redirect(node, fd, STDIN_FILENO);
        }
        case REDIRECT_APPEND: {
            fd = open(node->redirect.target, O_WRONLY | O_APPEND | O_CREAT, 0777);
            return exec_redirect(node, fd, STDOUT_FILENO);
        }
        case REDIRECT_DUP: {
            return exec_redirect(node, node->redirect.fd2, node->redirect.fd);
        }
    }
    return 0;
}

void sigint_handler()  {
    exit(0);
}

// todo: merge with fork_exec
int exec_subshell(node_t *node)   {
    int status;
    pid_t pid = fork();
    if (pid < 0)    {
        perror("Fork failed\n");
        exit(1);
    }
    else if (pid == 0)  {
        run_command(node->subshell.child);
        exit(1);
    }
    else    {
        curr_job = pid;
        wait(&status);
    }
    return 0;
}

void sigcont()  {
    kill(curr_job, SIGCONT);
}

void init_process() {
    signal(SIGINT, sigint_handler);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGCONT, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
}

int is_builtin_cmd(char **argv)    {
    if(strcmp(argv[0], "cd") == 0)  {
        chdir(argv[1]);
        return 1;
    }
    else if(strcmp(argv[0], "exit") == 0)    {
        exit(atoi(argv[1]));
    }
    else if(strcmp(argv[0], "pwd") == 0) {
        chdir(argv[1]);
    }
    else if(strcmp(argv[0], "set") == 0) {
        return set_envvar(argv[1], 1);
    }
    else if(strcmp(argv[0], "unset") == 0) {
        return set_envvar(argv[1], 0);
    }
    else if(strcmp(argv[0], "bg") == 0)  {
        pid_t pgid = getpgid(curr_job);
        if(pgid < 0)    {
            printf("job terminated\n");
            return 1;
        }
        if(killpg(pgid, SIGCONT) < 0)    {
            perror("kill failed\n");
        }
        return 1;
    }
    else if(strcmp(argv[0], "fg") == 0)  {
        int status;
        pid_t pgid = getpgid(curr_job);
        if(pgid < 0)    {
            printf("job terminated\n");
            return 1;
        }
        tcsetpgrp(STDIN_FILENO, pgid);
        if(killpg(pgid, SIGCONT) < 0)    {
            perror("kill failed\n");
        }
        waitpid(curr_job, &status, WUNTRACED);
        pid_t ppid = getpid();
        pid_t ppgid = getpgid(ppid);
        tcsetpgrp(STDIN_FILENO, ppgid);
        return 1;
    }
    return 0;
}

void launch_process(int fg)   {
    pid_t pid = getpid();
    if(fg)  {
        tcsetpgrp(STDIN_FILENO, pid);
        setpgid(pid, pid);
    }
    signal(SIGINT, sigint_handler);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGCONT, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
}

void fork_exec(char **argv)   {
    if(is_builtin_cmd(argv))    {
        return;
    }
    int status;
    pid_t pid = fork();
    if(pid < 0) {
        perror("fork failed\n");
        exit(1);
    }
    else if(pid == 0)   {
        pid_t ppid = getppid();
        pid_t ppgid = getpgid(ppid);
        launch_process(0);
        setpgid(pid, ppgid);
        execvp(argv[0], argv);
        perror("execvp failed\n");
        exit(1);
    }
    else    {
        pid_t temp = curr_job;
        curr_job = pid;
        wait(&status);
        if (WIFEXITED(status))  {
            curr_job = temp;
        }
    }
}

int exec_seq2(node_t *parent)  {
    if(parent->type != NODE_SEQUENCE)    {
        parse_cmd(parent);
        return 0;
    }
    exec_seq2(parent->sequence.first);
    exec_seq2(parent->sequence.second);
    return 0;
}

void parse_cmd(node_t *node)    {
    switch(node->type)  {
        case NODE_COMMAND:
            fork_exec(node->command.argv);
            break;
        case NODE_SEQUENCE:
            exec_seq2(node);
            break;
        case NODE_PIPE:
            exec_pipes(node);
            break;
        case NODE_REDIRECT:
            handle_redirect(node);
            break;
        case NODE_DETACH:
            fork_exec(node->detach.child->command.argv);
            break;
        case NODE_SUBSHELL:
            exec_subshell(node);
            break;
    }
}

int exec_detached(node_t *node)   {
    pid_t pid = fork();
    if(pid == 0)    {
        setpgid(pid, pid);
        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGCONT, sigcont);
        parse_cmd(node->detach.child);
        kill(getpid(), SIGKILL);
    }
    else    {
        curr_job = pid;
    }
    return 0;
}

int exec_seq(node_t *parent)  {
    if(parent->type != NODE_SEQUENCE)    {
        if(parent->type == NODE_DETACH) {
            exec_detached(parent);
        }
        else    {
            run_command(parent);
        }
        return 0;
    }
    exec_seq(parent->sequence.first);
    exec_seq(parent->sequence.second);
    return 0;
}

void initialize(void)   {
    /* This code will be called once at startup */
    init_process();
    char *ps1 = getenv("PS1");
    if(ps1) {
        gen_prompt(ps1);
    }
    else if (prompt)    {
        prompt = "vush$ ";
    }
}

void exec_cmd(char **argv)   {
    if(is_builtin_cmd(argv))    {
        return;
    }
    int status;
    pid_t pid = fork();
    if(pid < 0) {
        perror("fork failed\n");
        exit(1);
    }
    else if(pid == 0)   {
        launch_process(1);
        execvp(argv[0], argv);
        perror("execvp failed\n");
        exit(1);
    }
    else    {
        pid_t temp = curr_job;
        curr_job = pid;
        waitpid(pid, &status, WUNTRACED);
        if (WIFEXITED(status)){
            curr_job = temp;
        }
        pid_t ppid = getpid();
        pid_t ppgid = getpgid(ppid);
        tcsetpgrp(STDIN_FILENO, ppgid);
    }
}

void run_command(node_t *node)  {
    //print_tree(node);
    char *ps1 = getenv("PS1");
    if(ps1) {
        gen_prompt(ps1);
    }
    else if (prompt)    {
        prompt = "vush$ ";
    }
    switch(node->type)  {
        case NODE_COMMAND:
            exec_cmd(node->command.argv);
            break;
        case NODE_SEQUENCE:
            exec_seq(node);
            break;
        case NODE_PIPE:
            exec_pipes(node);
            break;
        case NODE_REDIRECT:
            handle_redirect(node);
            break;
        case NODE_DETACH:
            exec_detached(node);
            break;
        case NODE_SUBSHELL:
            exec_subshell(node);
            break;
    }
}