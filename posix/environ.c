/* This file just defines the `__environ' variable (and alias `environ').  */

#include <unistd.h>
#include <stddef.h>
#include <stdlib/setenv.h>

/* This must be initialized; we cannot have a weak alias into bss.  */
char **__environ = NULL;
weak_alias (__environ, environ)

/* The SVR4 ABI says `_environ' will be the name to use
   in case the user overrides the weak alias `environ'.  */
weak_alias (__environ, _environ)

struct environ_array *__environ_array_list;
environ_counter __environ_counter;
