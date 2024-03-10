# PISH

Pure and Interesting SHell.

Compilation: `gcc -o pish pish.c`

Usage: see `./pish -h`

It provides these bash-like features:

- run commands with arguments.
- `"..."` for string literal, support escape sequences.
- `${...}` for env expansion.
- `$?` for return status of last command.
- `$0 ... $9` for referencing command line arguments.
- `$(...)` for subshell, allow recursive subshells.
- `... | ...` for piping, allow cascading pipes.
- a little set of builtin commands, including:
  - `cd` for change directory
  - `set` and `unset` for env management
  - `eval` for extra evaluation
  - `source` for read commands from a file
  - `exit` for exit program
- prompt styling
- (optional) GNU readline shell, compile it with option
  `-DWITH_GNU_READLINE -lreadline`

I wrote this project for practicing linux userspace API.
