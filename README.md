# PISH

Pure and Interesting SHell.

## About

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

Currently there are still some quirks about `eval`, but I won't care.

## Abuse

This project reminds me that bash is such an exclusive shell. Even
with such a stupid shell with a lot of quirks, I found it is still
possible to achieve what we do in bash.

### closure
You can make a closure with `set` command.`set` doesn't evaluate
variable expansions in its second argument. (however, subshells and
pipes in subshells are evaluated as early as possible, so you need to
get some extra escapes for it, with string literal and dollar escape,
note that `\044` is translated to `$` only after evaluation of a
string literal)

``` shell
    set printpath "echo ${PATH}"

    eval $printpath

    set printdir "echo \"\044(ls | xargs)\""

    eval $printdir
```

Pish also supports scripting, so you can also write some scripts and
then source them, or run them in subshells. Each script is equal to a
function, at most 10 arguments are allowed for each scripts.

It is also possible to create a recursive closure.

``` shell
    set Y "\044{Y}\044{Y}"

    eval echo ${Y}
    # ${Y}${Y}
    eval eval echo ${Y}
    # ${Y}${Y}${Y}${Y}
    eval eval eval echo ${Y}
    # ${Y}${Y}${Y}${Y}${Y}${Y}${Y}${Y}
```

### conditionals
It is possible to implement `if` with only coreutils.

``` shell
    # if
    test <condition>
    # then
    seq $? 0 | xargs -I{} <expression-0>
    # else
    seq 1 $? | xargs -I{} <expression-1>
```

### loops
It is also possible to implement `for` loop with only coreutils.

``` shell
    # for i in $(seq 0 10); do <expression>; done
    seq 0 10 | xargs -I{} <expression>
```

It is also possible to get an infinite loop, with `yes`

``` shell
    # while true; do <expression>; done
    yes | xargs -I{} <expression>
```

### redirection
When we want to write something to a file, `tee` is here for use.

When we want to `cat` the output of subshells, actually, we can use
`mkfifo` for temporary pipes, and then cat on these new fifos.
