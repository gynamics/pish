/**
 * pish.c -- A pure and intersting shell
 *
 * PISH is not a csh-compatible shell, but provides a few
 * useful features to be used as an interactive unix shell.
 * It aims to provide a practical example about chaining pipes
 * and string processing to *nix newbies.
 *
 * All operations that require random access to a string
 * are implemented by folding it into a vector and then unfolding.
 * In this way, the code can be reused for processing arguments.
 * It is not a good idea to rely on it so let us just stop here.
 *
 * CopyRevolted by gynamics <dybfysiat@163.com>
 */

#define _GNU_SOURCE
#include <argp.h>
#include <ctype.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <wait.h>

/** we may use some features from GNU readline */
#ifdef WITH_GNU_READLINE
/* readline must be put after stdio.h */
#include <readline/history.h>
#include <readline/readline.h>
#else /* WITH_GNU_READLINE */
#define rl_bind_key(...)
#define add_history(...)

char *readline(const char *prompt) {
  char *buf = NULL;
  size_t bufsz;
  printf("%s", prompt);

  if (getline(&buf, &bufsz, stdin) < 0) {
    if (buf)
      free(buf);

    buf = NULL;
  } else { // strip input line
    while (strchr (" \t\v\n", *buf))
      buf++;

    char *end = buf + strlen (buf);

    while (strchr (" \t\v\n", *end))
      end--;

    end[1] = '\0';
  }

  return buf; // no need to clean up
}
#endif /* WITH_GNU_READLINE */

int pish_chdir(char **argv, int fds[2]);
int pish_eval(char **argv, int fds[2]);
int pish_exit(char **argv, int fds[2]);
int pish_help(char **argv, int fds[2]);
int pish_set(char **argv, int fds[2]);
int pish_unset(char **argv, int fds[2]);
int pish_source(char **argv, int fds[2]);
char *pish_fifo(const char *cmdline, const char *input);

#define __unused __attribute__((unused))
#define ARRAY_SIZE(a) sizeof(a) / sizeof((a)[0])
#define STRV(s, ...)                                                           \
  (char *[]) { s, ##__VA_ARGS__, 0 }

struct pish_cmd_desc {
  char *cmdstr;
  int (*exec)(char **argv, int fds[2]);
  char **helpstr;
};

static struct pish_cmd_desc pish_builtin_cmd[] = {
    {
        "cd",
        pish_chdir,
        STRV("change directory."),
    },
    {
        "eval",
        pish_eval,
        STRV("evaluate expression."),
    },
    {
        "exit",
        pish_exit,
        STRV("exit pish."),
    },
    {
        "help",
        pish_help,
        STRV("show help about builtin commands."),
    },
    {
        "set",
        pish_set,
        STRV("manipulating environment variables.",
             "/set/ displays all keys and values in environ.",
             "/set A/ sets the value of A to \"\".",
             "/set A B/ sets the value of A to B."),
    },
    {
        "unset",
        pish_unset,
        STRV("unset an environment variable", "/unset A/ unsets variable A."),
    },
    {
        "source",
        pish_source,
        STRV("read & execute contents of a file, line by line."),
    },
};

static int pish_argc;
static char **pish_argv;
static char pish_status[6] = {'0', '\0'};

/** substring */
char *strsub(const char *s, int len) {
  if (!s || len < 0)
    return NULL;

  char *ns = malloc((1 + len) * sizeof(char));

  strncpy(ns, s, len);
  ns[len] = '\0';
  return ns;
}

/** string clone */
static inline char *strclo(const char *s) {
  if (!s)
    return NULL;

  return strsub(s, strlen(s));
}

/** count occurrence in @s of given character @ch */
int strcoc(const char *s, int ch) {
  int cnt = 0;

  if (!s)
    return 0;

  while (*s != '\0')
    if (*s++ == ch)
      cnt++;

  return cnt;
}

/**
 * test if @lch and @rch are balanced in an expression
 * return 0 for balanced,
 * return a positive integer for number of unmatched @lch,
 * return a negative integer for number of unmatched @rch.
 */
static inline int strchp(const char *s, int lch, int rch) {
  return strcoc(s, lch) - strcoc(s, rch);
}

/** get length of a string vector, it must be ended with NULL */
int sv_len(char **sv) {
  if (!sv)
    return -1;

  int n = 0;

  while (sv[n] != NULL)
    n++;

  return n;
}

/** free contents of a string vector, it must be ended with NULL */
void sv_free(char **sv) {
  if (sv) {
    char **p = sv;

    while (*p != NULL)
      free(*p++);

    free(sv);
  }
}

/** print a string vector, for debugging usage */
void sv_pr(char **sv) {
  if (sv) {
    while (*sv != NULL)
      puts(*sv++);
  }
}

/**
 * break string @s with @delimitors into a string vector,
 * consecutive delimitors will be stripped out.
 * remember to free that vector with sv_free(),
 */
char **sv_fold(const char *s, const char *delimitors) {
  if (!s)
    return NULL;

  int argmax = 2;
  char **argv = malloc(argmax * sizeof(char *));
  char *buf = strclo(s);
  char *tok = strtok(buf, delimitors);
  int i = 0;

  /* tokenize */
  while (tok != NULL) {
    argv[i++] = strclo(tok);
    tok = strtok(NULL, delimitors);

    if (i + 1 == argmax) /* extend vector */
    {
      argmax *= 2;
      argv = realloc(argv, sizeof(char *) * argmax);
    }
  }

  argv[i] = NULL;
  free(buf);
  return argv;
}

/**
 * inverse operation of sv_fold(),
 * flatten a string vector into one string,
 * append @head before it and @tail after it,
 * using @sep as separator if not NULL.
 */
char *sv_unfold(char **sv, const char *sep, const char *head,
                const char *tail) {
  if (!sv || !sv[0])
    return NULL;

  if (!sep)
    sep = "";

  int seplen = strlen(sep);
  int len = 1 + strlen(sv[0]);

  if (head)
    len += strlen(head);

  if (tail)
    len += strlen(tail);

  for (int i = 1; sv[i] != NULL; i++)
    len += (seplen + strlen(sv[i]));

  char *s = malloc(len * sizeof(char));

  if (head)
    strcpy(s, head);
  else
    s[0] = '\0';

  strcat(s, sv[0]);

  for (int i = 1; sv[i] != NULL; i++) {
    strcat(s, sep);
    strcat(s, sv[i]);
  }

  if (tail)
    strcat(s, tail);

  return s;
}

/** test if an character represents an oct digit */
static inline int isodigit(int __c) { return ('0' <= __c) && (__c <= '7'); }

/** character to oct (dec as well) number */
static inline int c2oct(int ch) { return (ch - '0'); }

/** character to hex number */
static inline int c2hex(int ch) {
  if (ch <= '9')
    return (ch - '0');
  else if (ch <= 'F')
    return 0xa + (ch - 'A');
  else
    return 0xa + (ch - 'a');
}

/**
 * convert escaped sequence to character
 *
 * @pb points to output buffer pointer
 * @pp points to input buffer pointer
 * @end is the end of input buffer
 * if @quote is false, it parses input without converting
 *
 * return NULL if parse failed.
 */
const char *eseqtoch(char **pb, const char **pp, const char *end, bool quote) {
  const char *p = *pp;

  switch (*p) {
  case '\\':
  case '\'':
  case '\"':
  case '\?':
    *(*pb)++ = *p++;
    break;
  case 'a':
    *(*pb)++ = quote ? *p++ : '\a';
    break;
  case 'b':
    *(*pb)++ = quote ? *p++ : '\b';
    break;
  case 'e':
    *(*pb)++ = quote ? *p++ : '\033';
    break;
  case 'f':
    *(*pb)++ = quote ? *p++ : '\f';
    break;
  case 'n':
    *(*pb)++ = quote ? *p++ : '\n';
    break;
  case 'r':
    *(*pb)++ = quote ? *p++ : '\r';
    break;
  case 't':
    *(*pb)++ = quote ? *p++ : '\t';
    break;
  case 'v':
    *(*pb)++ = quote ? *p++ : '\v';
    break;
  case 'z':
    *(*pb)++ = quote ? *p++ : EOF;
    break;
  case 'x':
    if (p + 2 < end) {
      if (quote) {
        *(*pb)++ = *p++;
        *(*pb)++ = *p++;
        *(*pb)++ = *p++;
      } else {
        if (isxdigit(p[1]) && isxdigit(p[2])) {
          *(*pb)++ = c2hex(p[1]) * 0x10 + c2hex(p[2]);
          p += 3;
        } else
          return NULL;
      }
    } else {
      *pp = end;
      return NULL;
    }
    break;
  case '0' ... '7':
    if (p + 1 < end && p[0] == '0' && p[1] == '\'') {
      if (quote) {
        *(*pb)++ = *p++;
        *(*pb)++ = *p++;
      } else {
        *(*pb)++ = '\0'; // special case
        p++;
      }
    } else if (p + 2 < end && isodigit(p[1]) && isodigit(p[2])) {
      if (quote) {
        *(*pb)++ = *p++;
        *(*pb)++ = *p++;
        *(*pb)++ = *p++;
      } else {
        *(*pb)++ = c2oct(p[0]) * 0100 + c2oct(p[1]) * 010 + c2oct(p[2]);
        p += 3;
      }
    } else {
      *pp = end;
      return NULL;
    }
    break;
  default:
    fprintf(stderr, "%s: unknown escape sequence \\%c\n", __func__, *p);
    return NULL;
  }

  return (*pp = p);
}

/**
 * peek a character from input
 *
 * @pb points to output buffer pointer
 * @pp points to input buffer pointer
 * @end is the end of input buffer
 * if @quote is false, it parses input without converting
 *
 * return NULL if parse failed.
 */
const char *peek_char(char **pb, const char **pp, const char *end, bool quote) {
  const char *p = *pp;

  switch (*p) {
  case '\0':
    p = end;
    break;
  case '\\':
    if (quote)
      *(*pb)++ = *p;

    p++;

    if (!eseqtoch(pb, &p, end, quote))
      return NULL;

    break;
  default: // raw unicode can bypass here!
    *(*pb)++ = *p++;
    break;
  }

  return (*pp = p);
}

/**
 * peek a string literal from input
 *
 * @buf is the output buffer
 * @pp points to input buffer pointer
 * @end is the end of input buffer
 * if @quote is false, it parses input without converting
 *
 * return NULL if parse failed, otherwise,
 * return position of end of peeked string
 */
const char *peek_str(char *buf, char const **pp, const char *end, bool quote) {
  const char *p = *pp;
  char *b = buf;

  while (p < end) {
    switch (*p) {
    case '\"':
      *pp = p;
      return b;
    default:
      // a tricky operation to reuse peek_char interface
      if (!peek_char(&b, &p, end, quote)) {
        fprintf(stderr, "failed to parse string literal %s.\n", buf);
        return NULL;
      }
      break;
    }
  }
  return NULL;
}

/**
 * break string @s into a string vector,
 * but keep all string literals between '"' as it is.
 * if @quote is true, keep quotes, otherwise remove them.
 * remember to free that vector with sv_free(),
 */
char **pish_fold(const char *s, const char *delimitors, bool quote) {
  if (!s)
    return NULL;

  int i = 0;
  int j = 0;
  int argmax = 2;
  char **argv = malloc(argmax * sizeof(char *));
  char *buf = malloc((1 + strlen(s)) * sizeof(char));
  const char *ptr = s;
  const char *end = s + 1 + strlen(s);

  /* tokenize */
  while (*ptr != '\0') {
    if (strchr(delimitors, *ptr)) {
      if (j == 0) {
        ptr++;
        continue;
      }
    } else if (*ptr == '"') {
      if (quote)
        buf[j++] = '"';

      ptr++; // skip the first '"'

      const char *q = peek_str(&buf[j], &ptr, end, quote);

      if (!q)
        goto out;

      j += (q - &buf[j]);

      if (quote)
        buf[j++] = '"';

      ptr++; // skip the second '"'
      continue;
    } else {
      buf[j++] = *ptr++;
      continue;
    };

    argv[i++] = strsub(buf, j);
    j = 0;

    if (i + 1 == argmax) /* extend vector */
    {
      argmax *= 2;
      argv = realloc(argv, sizeof(char *) * argmax);
    }
  }

  if (j > 0) {
    argv[i++] = strsub(buf, j);

    if (i + 1 == argmax) /* extend vector */
      argv = realloc(argv, sizeof(char *) * (1 + argmax));
  }

out:
  argv[i] = NULL;
  free(buf);
  return argv;
}

int pish_chdir(char **argv, int fds[2]) {
  close(fds[0]);

  if (argv[1])
    return chdir(argv[1]);
  else
    return -1;
}

int pish_help(__unused char **argv, int fds[2]) {
  close(fds[0]);

  for (size_t i = 0; i < ARRAY_SIZE(pish_builtin_cmd); ++i) {
    dprintf(fds[1], "%s:\n", pish_builtin_cmd[i].cmdstr);

    char **s = pish_builtin_cmd[i].helpstr;

    while (*s)
      dprintf(fds[1], "\t%s\n", *s++);
  }

  return 0;
}

int pish_exit(char **argv, __unused int fds[2]) {
  if (argv[1])
    exit(strtol(argv[1], NULL, 10)); // exit with given value
  else
    exit(0);
}

/**
 * expand all substrings started with '$' in string @s.
 * supports recursive $(...) subshell and nonrecursive ${...} subkey.
 * return a new string, do not forget to free it!
 */
char *pish_expand(const char *s) {
  char **v = sv_fold(s, "$");
  int n = sv_len(v);
  int k = (s[0] == '$') ? 0 : 1; /* expand the first token? */

  for (int i = k; i < n; ++i) {
    char *end = NULL;
    char *key = NULL;
    char *val = NULL;

    if (v[i][0] == '(') {
      if (strchp(v[i], '(', ')') == 0) /* balanced ? */
      {
        end = strrchr(v[i], ')');
        char *cmd = strsub(&v[i][1], end - &v[i][1]);
        val = pish_fifo(cmd, NULL);

        free(cmd);
      } else if (i + 1 < n) /* unbalanced, defer it to subshell */
      {
        /*
         * transform:      fold           unfold
         *  $ ( S $ ( S ) ) => ( S | ( S ) ) => | ( S $ ( S ) )
         */
        val = sv_unfold((char *[]){v[i], v[i + 1], NULL},
                        (v[i + 1][0] == '(' ? "$" : NULL), "", "");
        free(v[i]);
        v[i] = strclo("");
        free(v[i + 1]); // just replace
        v[i + 1] = val;
        continue;
      } else
        goto out;
    } else {
      if (v[i][0] == '{' && (end = strchr(v[i], '}')))
        key = strsub(&v[i][1], end - &v[i][1]);
      else
        key = strclo(v[i]);

      if (*key == '?')
        val = strclo(pish_status);
      else if (isdigit(*key)) {
        int m = c2oct(*key);
        if (m < pish_argc)
          val = strclo(pish_argv[m]);
      } else
        val = strclo(getenv(key) ?: "");

      free(key);
    }

    if (!val)
      val = strclo("");

    /* concat ending part */
    char *es;

    if (end)
      es = sv_unfold((char *[]){val, end + 1, NULL}, NULL, NULL, NULL);
    else
      es = strclo(val);

    free(val);
    /* replace string */
    free(v[i]);
    v[i] = es;
  }

  char *js;
out:
  js = sv_unfold(v, NULL, NULL, NULL); /* join substrings */
  sv_free(v);
  return js;
}

extern char **environ;

int pish_set(char **argv, int fds[2]) {
  close(fds[0]);

  if (argv[1] != NULL) {
    if (argv[2] != NULL)
      setenv(argv[1], argv[2], 1);
    else
      setenv(argv[1], "", 1);
  } else { // print environ
    char **p = environ;

    while (*p != NULL)
      dprintf(fds[1], "%s\n", *p++);
  }

  return 0;
}

int pish_unset(char **argv, int fds[2]) {
  close(fds[0]);

  if (argv[1] != NULL)
    unsetenv(argv[1]); // replace

  return 0;
}

/**
 * fork a child process to execute @argv,
 * redirect its stdin to @fds[0] and stdout to @fds[1]
 */
int pish_fork(char **argv, int fds[2]) {
  int pid = vfork();

  if (pid == 0) {
    dup2(fds[0], fileno(stdin));
    dup2(fds[1], fileno(stdout));

    int status = execvp(argv[0], argv);

    fprintf(stderr, "failed to execute %s, ret = %d\n", argv[0], status);
    exit(status);
  }

  return pid;
}

/**
 * execute @cmd, if it is started with a builtin cmd,
 * run it directly, otherwise execute it with pish_fork()
 */
int pish_exec(const char *cmd, int fds[2]) {
  size_t j;
  int status = 0;
  char **argv = pish_fold(cmd, " \t\v\n;", false);

  if (!argv || !argv[0])
    return 0;

  for (j = 0; j < ARRAY_SIZE(pish_builtin_cmd); j++) {
    if (strcmp(argv[0], pish_builtin_cmd[j].cmdstr) == 0)
      break;
  }

  if (j < ARRAY_SIZE(pish_builtin_cmd))
    status = pish_builtin_cmd[j].exec(argv, fds);
  else
    status = pish_fork(argv, fds);

  sv_free(argv);
  return status;
}

/** expand strings in @argv once more */
int pish_eval(char **argv, int fds[2]) {
  if (!argv[0]) {
    close(fds[0]);
    return -1;
  }

  char *cmd = sv_unfold(&argv[1], "\" \"", "\"", "\"");
  char *ecmd = pish_expand(cmd);

  free(cmd);

  int status = pish_exec(ecmd, fds);

  free(ecmd);
  return status;
}

/** send @signum to all child processes */
void pish_sweep(int signum) {
  pid_t mypid = getpid();
  pid_t pid;

  while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
    if (pid != mypid) {
      kill(pid, signum);
    }
  }
}

/**
 * execute command strings in @argv as subprocesses
 * and piping their I/O to the next one by one.
 * use stdin as input and print result to stdout.
 *
 * READ END fds[0] -+   pipev[0] --+    X
 *                  |      ||      |
 *                 cmd0    ||     cmd1
 *                  |      ||      |
 * WRITE END   X    +-> pipev[1]   +-> fds[1]
 */
int pish_pipe(char **argv, int fds[2]) {
  int status = 0;
  int n = sv_len(argv);
  int(*pipev)[2] = malloc((1 + n) * sizeof(int[2]));

  /* build pipes */
  pipev[0][0] = dup(fds[0]);

  for (int i = 1; i < n; i++)
    pipe(pipev[i]);

  pipev[n][1] = dup(fds[1]);

  for (int i = 0; i < n; ++i) {
    status = pish_exec(argv[i], (int[2]){pipev[i][0], pipev[i + 1][1]});

    if (status < 0) /* fork failure */
      goto out;

    /* close the write end here so that the next child won't get blocked. */
    close(pipev[i + 1][1]);
  }

  pid_t mypid = getpid();
  pid_t pid;

  status = 0;
  /* wait for children */
  while ((pid = waitpid(-1, &status, WUNTRACED)) >= 0) {
    if (pid != mypid && WIFEXITED(status)) {
      if ((status = WEXITSTATUS(status)) < 0)
        goto out; /* an error is caught */
    }
  }

out:
  pish_sweep(SIGKILL); /* kill all remaining children */
  close(pipev[0][0]);

  for (int i = 1; i < n; ++i) {
    close(pipev[i][0]);
    close(pipev[i][1]);
  }

  close(pipev[n][1]);
  fflush(stdout);
  free(pipev);
  return status;
}

/** entry */
int pish(const char *cmdline, int fds[2]) {
  char *cmd;
  const char *end = strchr(cmdline, '#'); // comments

  if (end)
    cmd = strsub(cmdline, end - cmdline);
  else
    cmd = strclo(cmdline);

  char *ecmd = pish_expand(cmd);

  free(cmd);

  if (!ecmd)
    return 0;

  /*
   * without a one-pass parser, we suffer and suck here
   * however, the parse itself takes at least 500 lines.
   */
  char **cmdv = pish_fold(ecmd, "|", true);

  free(ecmd);

  int status = pish_pipe(cmdv, fds);
  sprintf(pish_status, "%5d", status);

  sv_free(cmdv);
  return status;
}

/** update the environment variables for repl */
void pish_update_env(void) {
  char *dirname = get_current_dir_name();

  setenv("PWD", dirname, 1);

  struct passwd *pw = getpwuid(getuid());
  if (pw)
    setenv("USER", pw->pw_name, 1);
  else
    setenv("USER", "", 1);

  free(dirname);
}

/**
 * read, evaluate, print line by line, @f is the input stream.
 * we use a FILE * pointer for bufferred IO.
 */
int pish_repl(FILE *f, int fds[2]) {
  int status = 0;
  size_t bufsz = 0;
  char *buf = NULL;

  while (!feof(f)) {
    pish_update_env();

    if (getline(&buf, &bufsz, f) < 0)
      break;

    if (bufsz > 0) {
      status = pish(buf, fds);

      if (status)
        break;
    }
  }

  free(buf);
  return status;
}

/**
 * run pish() with bufferred input and output
 * do not forget to free the output buffer.
 */
char *pish_fifo(const char *cmdline, const char *input) {
  int fds[2][2];

  pipe(fds[0]);
  pipe(fds[1]);

  if (input) /* write input into write end */
    write(fds[0][1], input, strlen(input));

  close(fds[0][1]);
  int status = pish(cmdline, (int[2]){fds[0][0], fds[1][1]});
  close(fds[0][0]);
  close(fds[1][1]);

  if (status)
    return NULL;

  char *buf = NULL;
  int size;

  ioctl(fds[1][0], FIONREAD, &size); /* get size to read */

  if (size > 0) {
    buf = malloc((size + 1) * sizeof(char));
    buf[size] = '\0'; /* append terminal */

    if ((size = read(fds[1][0], buf, size)) < 0) {
      fprintf(stderr, "pipe read error, status = %d.\n", size);
      free(buf);
      buf = NULL;
    }
  }

  close(fds[1][0]);
  return buf;
}

int pish_source(char **argv, int fds[2]) {
  int i = 1;
  int status = 0;

  while (argv[i]) {
    FILE *f = fopen(argv[i], "r");

    if (f) {
      status = pish_repl(f, fds);

      fclose(f);
      if (status < 0)
        break;
    } else {
      fprintf(stderr, "failed to open file %s, errno = %d.\n", argv[i], errno);
      return errno;
    }
  }

  return status;
}

/** An interactive shell with prompt */
int pish_ishell(void) {
  char *prompt = NULL;

  setenv("PROMPT", "\e[0m[\e[33m${PWD}\e[0m]\e[31m,`'\e[0m ", 0);
  rl_bind_key('\t', rl_complete);

  while (true) {
    /* update env */
    pish_update_env();
    /* update prompt */
    const char *ps = getenv("PROMPT") ?: "($PROMPT Unavailable)> ";

    if (prompt)
      free(prompt);

    prompt = pish_expand(ps);

    char *line = readline(prompt);

    if (line) {
      add_history(line);

      int status = pish(line, (int[2]){fileno(stdin), fileno(stdout)});
      free(line);

      if (status < 0)
        fprintf(stderr, "task exited abnormally, status = %d\n", status);
    } else
      return 0;
  }
}

/** handler for SIGINT */
void sigint_handler(__unused int signum) { pish_sweep(SIGKILL); }

static char **cmd_help =
    STRV("Usage: pish [OPTION] [ARGS]", "",
         "Options:", "  -c [STRING]\tsource given STRING .",
         "  -h\t\tdisplay this help information.",
         "  -i\t\trun an interactive shell (using GNU readline).",
         "    \t\tpress Ctrl+C to interrupt current command.",
         "    \t\tpress Ctrl+D to send an EOF to exit shell", "",
         "run \"help\" in shell to get a list of builtin commands", "");

int main(int argc, char *argv[]) {
  pish_argc = argc;
  pish_argv = argv;

  if (argc > 1 && argv[1][0] == '-') {
    switch (argv[1][1]) {
    case 'c':
      if (argc > 2)
        return pish(argv[2], (int[2]){fileno(stdin), fileno(stdout)});
      break;
    case 'h':
      sv_pr(cmd_help);
      break;
    case 'i':
      signal(SIGINT, sigint_handler);
      return pish_ishell();
    default:
      fprintf(stderr, "Unknown option %s\n", argv[1]);
      sv_pr(cmd_help);
      return -1;
    };
  } else
    return pish_repl(stdin, (int[2]){-1, fileno(stdout)});

  return 0;
}
