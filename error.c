/* Error handling */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "error.h"

char fatal_buffer[BUFSIZ];
void
fatal_die (int n, const char *str)
{
  fprintf (stderr, "Fatal error: %s\n", str);
  if (errno)
    perror (NULL);
  exit (n);
}
