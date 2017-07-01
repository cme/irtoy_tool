/* Error handling */
#ifndef __error_h
#define __error_h
extern char fatal_buffer[BUFSIZ];
extern void fatal_die (int n, const char *str);
#define fatal(n, s...)                          \
do                                              \
  {                                             \
    sprintf (fatal_buffer, s);                  \
    fatal_die (n, fatal_buffer);                \
  } while (0)
#endif  /* __error_h */

#define warning(s...)                           \
do                                              \
  {                                             \
    fprintf (stderr, "Warning: ");              \
    fprintf (stderr, s);                        \
  } while (0)
