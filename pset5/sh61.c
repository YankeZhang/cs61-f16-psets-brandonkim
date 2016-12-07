#include "sh61.h"
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/param.h>

// struct command
//    Data structure describing a command. Add your own stuff.

typedef struct command command;
struct command {
    int argc;      // number of arguments
    char** argv;   // arguments, terminated by NULL
    pid_t pid;     // process ID running this command, -1 if none
    int background;
    command* next;    // Next command in list
};

// Current working directory global
char cwd[MAXPATHLEN];

// Built-in commands
char BUILTIN_CD[] = "cd";
char BUILTIN_EXIT[] = "exit";
// command_alloc()
//    Allocate and return a new command structure.

static command* command_alloc(void) {
    command* c = (command*) malloc(sizeof(command));
    c->argc = 0;
    c->argv = NULL;
    c->pid = -1;
    c->background = 0;
    c->next = NULL;
    return c;
}


// command_free(c)
//    Free command structure `c`, including all its words.

static void command_free(command* c) {
    for (int i = 0; i != c->argc; ++i)
        free(c->argv[i]);
    free(c->argv);
    free(c);
}


// command_append_arg(c, word)
//    Add `word` as an argument to command `c`. This increments `c->argc`
//    and augments `c->argv`.

static void command_append_arg(command* c, char* word) {
    c->argv = (char**) realloc(c->argv, sizeof(char*) * (c->argc + 2));
    c->argv[c->argc] = word;
    c->argv[c->argc + 1] = NULL;
    ++c->argc;
}


// COMMAND EVALUATION

// start_command(c, pgid)
//    Start the single command indicated by `c`. Sets `c->pid` to the child
//    process running the command, and returns `c->pid`.
//
//    PART 1: Fork a child process and run the command using `execvp`.
//    PART 5: Set up a pipeline if appropriate. This may require creating a
//       new pipe (`pipe` system call), and/or replacing the child process's
//       standard input/output with parts of the pipe (`dup2` and `close`).
//       Draw pictures!
//    PART 7: Handle redirections.
//    PART 8: The child process should be in the process group `pgid`, or
//       its own process group (if `pgid == 0`). To avoid race conditions,
//       this will require TWO calls to `setpgid`.

pid_t start_command(command* c, pid_t pgid) {
    (void) pgid;
    pid_t child_pid;
    // Handle built-ins before possibly forking
    if (strcmp(c->argv[0], BUILTIN_CD) == 0) {
        if (chdir(c->argv[1]) == -1) {
            perror("failed to change cwd: ");
            return -1;
        } else return 0;
    }
     if (strcmp(c->argv[0], BUILTIN_EXIT) == 0)
       _exit(EXIT_SUCCESS); 
    child_pid = fork();

    if (child_pid == 0) {
        // In child
        // printf("Filius sum.\n");
        c->pid = child_pid;
        execvp(c->argv[0], c->argv);
        // If this executes, something's gone wrong
        perror("Execvp failed: ");
        _exit(EXIT_FAILURE);
        return -1;
    } else if (child_pid > 0) {
	// In parent (shell)
        return c->pid;
    } else {
        fprintf(stderr, "Failed to fork, oh dear.\n");
        return -1;
    }
}


// run_list(c)
//    Run the command list starting at `c`.
//
//    PART 1: Start the single command `c` with `start_command`,
//        and wait for it to finish using `waitpid`.
//    The remaining parts may require that you change `struct command`
//    (e.g., to track whether a command is in the background)
//    and write code in run_list (or in helper functions!).
//    PART 2: Treat background commands differently.
//    PART 3: Introduce a loop to run all commands in the list.
//    PART 4: Change the loop to handle conditionals.
//    PART 5: Change the loop to handle pipelines. Start all processes in
//       the pipeline in parallel. The status of a pipeline is the status of
//       its LAST command.
//    PART 8: - Choose a process group for each pipeline.
//       - Call `set_foreground(pgid)` before waiting for the pipeline.
//       - Call `set_foreground(0)` once the pipeline is complete.
//       - Cancel the list when you detect interruption.

void run_list(command* c) {
    command* next;
    int status; // For waitpid()
    pid_t child;
    next = c;
    while (next->argc != 0) {
        child = start_command(next, 42);
        if (next->background == 0)
	    waitpid(child, &status, 0);
        if (next->next == NULL) // This was the last command in the list
	    break;
        //Otherwise we still have more to go
        next = next->next;
    }
}


// eval_line(c)
//    Parse the command list in `s` and run it via `run_list`.

void eval_line(const char* s) {
    int type;
    char* token;
    command* next_command;
    // Your code here!

    // build the command
    command* c1 = command_alloc();
    command* current = c1;
    while ((s = parse_shell_token(s, &type, &token)) != NULL) {
        if (type == TOKEN_NORMAL)
            command_append_arg(current, token);
        if (type == TOKEN_BACKGROUND)
            current->background = 1;
        if (type == TOKEN_SEQUENCE || type == TOKEN_BACKGROUND) {
           next_command = command_alloc(); 
           current->next = next_command;
           current = next_command;
        }
        waitpid(-1,0,WNOHANG);
    }
    // execute it
    if (c1->argc)
        run_list(c1);
    command_free(c1);
}


int main(int argc, char* argv[]) {
    FILE* command_file = stdin;
    int quiet = 0;

    // Check for '-q' option: be quiet (print no prompts)
    if (argc > 1 && strcmp(argv[1], "-q") == 0) {
        quiet = 1;
        --argc, ++argv;
    }

    // Check for filename option: read commands from file
    if (argc > 1) {
        command_file = fopen(argv[1], "rb");
        if (!command_file) {
            perror(argv[1]);
            exit(1);
        }
    }

    // - Put the shell into the foreground
    // - Ignore the SIGTTOU signal, which is sent when the shell is put back
    //   into the foreground
    set_foreground(0);
    handle_signal(SIGTTOU, SIG_IGN);

    char buf[BUFSIZ];
    int bufpos = 0;
    int needprompt = 1;

    while (!feof(command_file)) {
        // Update cwd
        getcwd(cwd, MAXPATHLEN);

        // Print the prompt at the beginning of the line
        if (needprompt && !quiet) {
            printf("sh61[%d]:%s:$ ", getpid(), cwd);
            fflush(stdout);
            needprompt = 0;
        }

        // Read a string, checking for error or EOF
        if (fgets(&buf[bufpos], BUFSIZ - bufpos, command_file) == NULL) {
            if (ferror(command_file) && errno == EINTR) {
                // ignore EINTR errors
                clearerr(command_file);
                buf[bufpos] = 0;
            } else {
                if (ferror(command_file))
                    perror("sh61");
                break;
            }
        }

        // If a complete command line has been provided, run it
        bufpos = strlen(buf);
        if (bufpos == BUFSIZ - 1 || (bufpos > 0 && buf[bufpos - 1] == '\n')) {
            eval_line(buf);
            bufpos = 0;
            needprompt = 1;
        }

        // Handle zombie processes and/or interrupt requests
        // Your code here!
        waitpid(-1,0,WNOHANG);
    }

    return 0;
}
