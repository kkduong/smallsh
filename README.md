## smallsh

`smallsh` is a minimal Unix-like shell implemented in C as part of **CS 344 – Operating Systems I** at Oregon State University.  
It supports command execution, I/O redirection, background/foreground processes, and signal handling using POSIX APIs.

---

### Features

- **Built-in commands**
  - `exit` – terminates the shell and all child processes
  - `cd` – changes the working directory (defaults to `$HOME`)
  - `status` – prints the exit status or terminating signal of the last foreground process

- **Command execution**
  - Runs other GNU/Linux commands via `fork()`, `execvp()`, and `waitpid()`
  - Supports both **foreground (`process`)** and **background (`process &`)** execution
  - Prevents zombie/orphan processes by reaping background children

- **I/O redirection**
  - `< file` for stdin redirection  
  - `> file` for stdout redirection  
  - Implemented using `dup2()` and POSIX file operations

- **Signal handling**
  - `SIGINT` (Ctrl-C):  
    - Ignored by the shell itself  
    - Forwarded to foreground processes
  - `SIGTSTP` (Ctrl-Z):  
    - Toggles "foreground-only" mode for the shell  
    - Disables background execution when active

- **Command prompt**
  - A simple `:` prompt is displayed for each user command

---

### Requirements

- GCC or Clang
- Linux environment (POSIX-compliant)
- Standard C libraries

---

### Compilation

A helper shell script is provided:

```bash
./compileall.sh

