/* IRToy controller
 * Connections:
 *  - IR port (receive/transmit)
 *  - MythRemote port
 *  - Command port
 */
#if __linux__
#define USE_UINPUT
#endif
/*
 * TODO: More efficient matching?
 * TODO: sort packets to find the best packet to transmit?
 * TODO: Move gap/jitter to IRState.
 * TODO: Don't reconstruct the fd_set every select().
 * TODO: better recording of samples? Because as it stands... this is gross.
 *
 * Oct 2016 to-do
 * - command to set 'UNKNOWN' key name in output
 * - refactor analysis to take one file, and clean it out.
 * - globs in device names
 * - decode packets to binary?
 * - use osascript in interactive mode with a pipe to cut out start overheads.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdbool.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>

#ifdef USE_UINPUT
#include <linux/uinput.h>
#endif

#include "dict.h"
#include "irtoy.h"
#include "error.h"
#include "keywords.h"
#include "mac_actions.h"
#include "server.h"

int ir_packet_timeout = 100000;
int ir_debounce_time = 250000;  /* initial repeat delay */
int ir_repeat_delay = 0;        /* current repeat delay */

/* ------------------------------------------------------------
 * Testing stuff
 */

typedef struct PacketFile PacketFile;
struct PacketFile
{
  char *fname;
  int n_packets;
  IRPacket **packets;
};

PacketFile *
read_packets (FILE * in)
{
  PacketFile *pf;
  int n_packets_allocated = 1;

  pf = malloc (sizeof *pf);
  pf->packets = malloc (sizeof *pf->packets);
  pf->n_packets = 0;

  while (!feof (in))
    {
      IRPacket *k;
      k = irpacket_scanf (in);
      if (!k)
        break;
      if (pf->n_packets + 1 >= n_packets_allocated)
        {
          n_packets_allocated *= 2;
          pf->packets = realloc (pf->packets,
                                 n_packets_allocated * sizeof *pf->packets);
        }
      pf->packets[pf->n_packets++] = k;
    }
  pf->packets[pf->n_packets] = NULL;
  return pf;
}

void
analyse_packet_files (int n, char **name)
{
  int i, x, y, j;
  const int jitter_max = 100;
  int max_useful_jitter = 0;
  FILE *in;
  PacketFile **files = malloc (n * sizeof *files);
  int n_packets = 0;
  IRPacket **packets;
  const char **names;
  int *m;
  int best_jitter;
  int best_jitter_score;
  bool *keep;
  bool changed;
  int *scores;
  int *kept_packets;

  for (i = 0; i < n; i++)
    {
      char *c;
      in = fopen (name[i], "r");
      if (!in)
        fatal (0, "Can't open input file '%s'", name[i]);
      files[i] = read_packets (in);
      files[i]->fname = strdup(name[i]);
      for (c = files[i]->fname; *c; c++)
        if (*c == '/')
          *c = '_';
      printf ("Read %d packets from '%s'\n", files[i]->n_packets,
              files[i]->fname);
      n_packets += files[i]->n_packets;
    }

  /* Build the big list */
  packets = calloc (n_packets + 1, sizeof *packets);
  names = calloc (n_packets + 1, sizeof *names);
  n_packets = 0;
  for (i = 0; i < n; i++)
    {
      int j;
      for (j = 0; j < files[i]->n_packets; j++)
        {
          packets[n_packets] = files[i]->packets[j];
          names[n_packets] = files[i]->fname;
          n_packets++;
        }
    }

  printf ("All packets\n");
  for (i = 0; i < n_packets; i++)
    {
      printf ("%s ", names[i]);
      irpacket_render (stdout, packets[i]);
      printf ("\n");
    }

  printf ("%d packets\n", n_packets);
  m = calloc (n_packets * n_packets, sizeof *m);

  printf ("Pulse width distribution\n");
  {
    int max_pulse_width = 0;
    int max_count = 0;
    int *counts;
    for (i = 0; i < n_packets; i++)
      for (j = 0; j < packets[i]->n_pulses; j++)
        if (max_pulse_width < packets[i]->pulses[j].width)
          max_pulse_width = packets[i]->pulses[j].width;
    printf ("Max pulse width is %d\n", max_pulse_width);
    counts = calloc (max_pulse_width + 1, sizeof *counts);
    for (i = 0; i < n_packets; i++)
      for (j = 0; j < packets[i]->n_pulses; j++)
        {
          int c = ++counts[packets[i]->pulses[j].width];
          if (c > max_count)
            max_count = c;
        }
    for (i = 0; i < max_pulse_width; i++)
      {
        printf ("%5d %-6d", i, counts[i]);
        for (j = 0; j < (79 - 12) * counts[i] / max_count; j++)
          printf ("*");
        printf ("\n");
      }
    free (counts);
  }

  /* Jitter Matrix.
     This holds, for each combination of X and Y, minimum jitter
     necessary to allow them to match, so is a measure of the
     proximity of the packets in the structural space.
  */
  printf ("Computing jitter matrix\n");
  for (y = 0; y < n_packets; y++)
    for (x = y; x < n_packets; x++)
      {
        int i;
        for (i = 0; i < jitter_max; i++)
          {
            if (irpacket_match (packets[x], packets[y], i))
              break;
          }
        m[x + y * n_packets] = i;
        m[y + x * n_packets] = i;
        if (i > max_useful_jitter && i != jitter_max)
          max_useful_jitter = i;
      }

  printf ("Jitter matrix:\n");
  for (y = 0; y < n_packets; y++)
    {
      for (x = 0; x < n_packets; x++)
        printf ("%4d", m[x + y * n_packets]);
      printf ("\n");
    }

  best_jitter = jitter_max;
  for (j = 0; j <= max_useful_jitter; j++)
    {
      int mismatches = 0,       /* false mismatches */
        ambiguous = 0;          /* false matches */
      for (y = 0; y < n_packets; y++)
        for (x = y; x < n_packets; x++)
          {
            bool match = m[x + y * n_packets] <= j;
            bool same = (names[x] == names[y]);
            if (match && !same)
              {
                if (j == 0)
                  {
                    printf ("Warning: entirely ambiguous data:\n");
                    printf ("%s ", names[x]);
                    irpacket_printf (stdout, packets[x]);
                    printf (" matches\n%s ", names[y]);
                    irpacket_printf (stdout, packets[x]);
                    printf ("\n");
                  }
                ambiguous++;
              }
            else if (!match && same)
              mismatches++;
          }
      printf ("At jitter=%d, false mismatches=%d, ambiguous=%d\n",
              j, mismatches, ambiguous);
      if (j == 0 || ambiguous + mismatches < best_jitter_score)
        {
          best_jitter_score = ambiguous + mismatches;
          best_jitter = j;
        }
      if (mismatches == 0)
        {
          printf ("No mismatches, abandoning search\n");
          break;
        }
    }

  printf ("Best jitter is %d with a badness of %d\n",
          best_jitter, best_jitter_score);

  printf ("Optimising key codes\n");
  keep = calloc (n_packets, sizeof *keep);
  for (x = 0; x < n_packets; x++)
    keep[x] = true;
  scores = calloc (n_packets, sizeof *scores);
  kept_packets = calloc (n_packets, sizeof *kept_packets);
  do
    {
      int best_packet;
      int best_packet_score;
      int n_kept_packets;
      changed = false;

      for (i = 0; i < n; i++)
        {
          best_packet_score = -1;
          for (x = 0; x < n_packets; x++)
            {
              int x_score = 0;
              if (!keep[x])
                continue;
              if (names[x] != files[i]->fname)
                continue;
              for (y = 0; y < n_packets; y++)
                {
                  bool match, same;
                  if (x == y)
                    continue;
                  match = m[x + y * n_packets] <= best_jitter;
                  same = names[x] == names[y];
                  if (match && same && keep[y])
                    /* In this round, only score it if it helps us
                       narrow the field */
                    x_score++;
                  else if (match && !same)
                    x_score -= 1;       /* penalise false + */
                  else if (!match && same)
                    ;           /* Probably two distinct patterns
                                   from one key. Don't penalise. */
                  else if (!match && !same)
                    x_score++;
                }
              printf ("%s ", names[x]);
              irpacket_printf (stdout, packets[x]);
              printf (" score=%d\n", x_score);
              scores[x] = x_score;
              if (x_score > best_packet_score || best_packet_score < 0)
                {
                  best_packet_score = x_score;
                  best_packet = x;
                }
            }

          printf ("Best packet is: p%d %s ", best_packet, names[best_packet]);
          irpacket_printf (stdout, packets[best_packet]);
          printf (" with score %d\n", best_packet_score);

          n_kept_packets = 1;
          kept_packets[0] = best_packet;

          /* Trim redundant packets */
          for (x = 0; x < n_packets; x++)
            {
              int k;
              if (!keep[x] || x == best_packet
                  || names[x] != names[best_packet])
                continue;
              
              /* Can we find a kept packet that makes this packet X
                 unnecessary? */
              printf ("Do we need packet %d?\n", x);
              for (k = 0; k < n_kept_packets; k++)
                if (m[kept_packets[k] + x * n_packets] <= best_jitter)
                  {
                    keep[x] = false;
                    changed = true;
                    printf ("Don't need to keep p%d because of packet p%d", x, kept_packets[k]);
                    irpacket_printf (stdout, packets[x]);
                    printf ("\n");
                    break;
                  }
              if (keep[x])
                {
                  kept_packets[n_kept_packets++] = x;
                }
            }
          printf ("Kept %d packets for '%s'\n", n_kept_packets, names[i]);
        }
    }
  while (changed);

  /* Don't keep duplicates. Only do this at this stage since in the
     above code, the duplicates serve to weight the scores. */
  for (x = 0; x < n_packets; x++)
    if (keep[x] && scores[x] > 0)
      for (y = x + 1; y < n_packets; y++)
        if (irpacket_match (packets[x], packets[y], 0))
          keep[y] = false;

  /* Dump out 'canonical' packets */
  {
    int kept = 0;
    printf ("# Good packet list:\n");
    for (x = 0; x < n_packets; x++)
      {
        if (keep[x] && scores[x] > 0)
          {
            kept++;
            printf ("button %s ", names[x]);
            irpacket_printf (stdout, packets[x]);
            printf (" # p%d score %d\n", x, scores[x]);
          }
      }
    printf ("# kept %d of %d packets\n", kept, n_packets);
  }

}


unsigned char test_chars[] = {
  0, 5,                         /* 1 */
  0, 5,                         /* 0 */
  0, 10,                        /* 1 */
  0, 100,                       /* 0 */

  0, 1,
  0, 2,
  0, 3,
  0, 40,

  255, 255,
  255, 255,
  255, 255,

  0, 1,
  0, 2,
  0, 3,
  0, 40
};

void
log_packet (IRPacket * k)
{
  static int counter = 1;
  static IRDict *d;
  const char *name;
  if (!d)
    d = new_irdict ();
  name = irdict_lookup_packet (d, k);
  if (name)
    fprintf (stdout, "(Duplicate packet '%s')\n", name);
  else
    {
      char buffer[BUFSIZ];
      sprintf (buffer, "packet_%d", counter++);
      fprintf (stdout, "(New unique packet '%s')\n", buffer);
      irdict_insert (d, strdup (buffer), k);
    }
}

int
test_main (int argc, char *argv[])
{
  IRState *ir = new_irstate ();
  IRPacket *k;
  IRPacket *out_packets[1];
  int n, i;

  n = irstate_rxbytes (ir, sizeof (test_chars), test_chars, out_packets);
  fprintf (stdout, "Test sequence has %d packets:\n", n);
  for (i = 0; i < n; i++)
    {
      irpacket_printf (stdout, out_packets[i]);
      fprintf (stdout, "\n");
      irpacket_render (stdout, out_packets[i]);
      fprintf (stdout, "\n");
      log_packet (out_packets[i]);
    }

  while (!feof (stdin))
    {
      fprintf (stdout, "Gimme a packet: ");
      k = irpacket_scanf (stdin);
      if (k)
        {
          fprintf (stdout, "Got packet:\n");
          irpacket_printf (stdout, k);
          fprintf (stdout, "\n");
          log_packet (k);
        }
      else
        {
          fprintf (stdout, "Couldn't get packet\n");
        }
    }

  return 0;
}


/* ------------------------------------------------------------
 * Server/Connection management
 * XXX TODO: handle timeouts better. Set time?
 */

typedef struct IRConnectionInfo IRConnectionInfo;
typedef struct IRServerInfo IRServerInfo;

struct IRConnectionInfo {
  IRServerInfo *si;
  char *buffer;
  char *end;
};

struct IRServerInfo {
  Server *server;
  Dict *keymaps;                /* name -> Keymap* */
  Keymap *current_keymap;

  IRDict *buttondict;
  IRState *ir;
  Connection *mythremote;
  Connection *irdev;
  Connection *vlc;
  Connection *uinput;
  FILE *out_file;

  /* Key debouncing */
  const char *last_button;
  struct timeval last_button_time;
  struct timeval next_repeat_time; /* earliest time of next repeat button */

  bool verbose;

  char *unknown_key;
};

bool handle_button (IRServerInfo *si, const char *button);
IRPacket *transmit_button (IRServerInfo *si, const char *button);

bool mythremote_command (IRServerInfo *si, const char *command);
bool vlc_command (IRServerInfo *si, const char *command);

bool send_myth_command (IRServerInfo *si, const char *command);
bool send_keypress (IRServerInfo *si, int key);
bool send_key (IRServerInfo *si, int key, int value);


IRServerInfo *
new_irserverinfo (void)
{
  IRServerInfo *si = malloc (sizeof *si);
  /* IR specific stuff */
  si->buttondict = NULL;
  si->keymaps = dict_new (NULL);
  si->current_keymap = NULL;
  si->ir = NULL;
  si->mythremote = NULL;
  si->out_file = NULL;
  si->last_button = NULL;
  si->uinput = NULL;
  si->verbose = false;
  si->unknown_key = NULL;
  return si;
}

IRConnectionInfo *new_irconnectioninfo (IRServerInfo *si) {
  IRConnectionInfo *ci = malloc (sizeof *ci);
  ci->si = si;
  ci->buffer = malloc (sizeof (char) * BUFSIZ);
  ci->end = ci->buffer;
  return ci;
}

Connection *
new_cmdconnection (IRServerInfo *si, int fd, IRConnectionInfo *ci)
{
  Connection *n;
  char buffer[BUFSIZ];
  sprintf (buffer, "cmd connection %d", fd);
  n = new_connection (si->server, fd, buffer, ci);
  return n;
}


/* ------------------------------------------------------------
 * Command server
 */

void
can_read_command (Connection * n, void *h)
{
  /* Dynamic buffer for this transfer */
  char buffer[BUFSIZ];
  int fd = connection_fd (n);
  int count = read (fd, buffer, BUFSIZ);
  int i;
  IRConnectionInfo *ci = (IRConnectionInfo *)h;

  for (i = 0; i < count; i++)
    {
      if (buffer[i] != '\n' && buffer[i] != '\r')
        {
          if (ci->end - ci->buffer < BUFSIZ - 1)
            {
              *ci->end++ = buffer[i];
            }
          else
            {
              char str[] = "Error: command buffer overrun\n\n";
              write (fd, str, sizeof (str) - 1);
              count = 0;        /* => close connection */
              break;
            }
        }
      else if (ci->buffer != ci->end)
        {
          IRPacket *k;
          *ci->end++ = '\0';
	  /* ">symname" to transmit 'symname' */
          if (ci->buffer[0] == '>')
            {
              k = transmit_button (ci->si, ci->buffer+1);
              if (k)
                {
                  write (fd, "ok\n", 3);
                  if (ci->si->verbose)
                    {
                      fprintf (stdout, "Command '%s' gets packet: ", ci->buffer);
                      irpacket_printf (stdout, k);
                      fprintf (stdout, "\n");
                      irpacket_render (stdout, k);
                      fprintf (stdout, "\n");
                    }
                }
              else
                {
                  char b2[BUFSIZ];
                  sprintf (b2, "Unknown button '%s'\n", ci->buffer);
                  write (fd, b2, strlen (b2));
                }
            }
	  /* "=symname" to set the symbol for unknown packets to 'symname' */
          else if (ci->buffer[0] == '=')
            {
              if (ci->si->verbose)
                fprintf (stdout, "Setting UNKWOWN key to '%s'\n", &ci->buffer[1]);
              if (ci->si->unknown_key)
                free (ci->si->unknown_key);
              ci->si->unknown_key = strdup (&ci->buffer[1]);
            }
          else
            {
              if (ci->si->verbose)
                fprintf (stdout, "Command port gets '%s'\n", ci->buffer);
              if (handle_button(ci->si, ci->buffer))
                write (fd, "ok\n", 3);
            }

          ci->end = ci->buffer;
        }
    }
  if (count == 0)
    {
      free (ci->buffer);
      close (fd);
      connection_remove (n);
    }
}

void
can_read_cmdport (Connection * n, void *h)
{
  int nfd = accept (connection_fd (n), NULL, NULL);
  IRServerInfo *si = (IRServerInfo *)h;
  Connection *n2;
  /* Set non-blocking */
  fcntl (nfd, F_SETFL, fcntl (nfd, F_GETFL) | O_NONBLOCK);
  n2 = new_cmdconnection (si, nfd,
			  new_irconnectioninfo (si));
  connection_set_can_read(n2, can_read_command);
}


/* ------------------------------------------------------------
 * Keymaps and Actions
 */

typedef enum ActionID {
  action_keypress, action_multitap, action_mythtv, action_transmit,
  action_set_keymap, action_vlc, action_applescript
} ActionID;

struct Action
{
  ActionID id;
  const char *operand;
  Action *next;
};

Action *
new_action (ActionID id, const char *operand) {
  Action *a = malloc (sizeof *a);
  a->id = id;
  a->operand = operand;
  a->next = NULL;
  return a;
}

struct Keymap {
  const char *name;
  const char *inherit_name;
  Keymap *inherit;
  Dict *mapping;                /* char* -> Action* */
};

Keymap *
new_keymap (const char *name) {
  Keymap *km = malloc (sizeof *km);
  km->name = strdup (name);
  km->mapping = dict_new (NULL);
  km->inherit_name = NULL;
  km->inherit = NULL;
  return km;
}

void
keymap_add_action (Keymap *m, const char *k, Action *a) {
  dict_set (m->mapping, k, a);
}


/* ------------------------------------------------------------
 * Options and Config File
 */

typedef struct ServerOpts ServerOpts;
struct ServerOpts
{
  char *config_file;
  char *irdev;
  int cmdport;
  char *frontend_host;
  int frontend_port;
  char *vlc_host;
  int vlc_port;
  char *out_file;
  bool verbose;
  char *uinput_dev;
  char *buttondict_fname;  
  bool daemon;
};

/* Config file format is:
 * file ::= entry*
 * entry ::= '#' comment
 *         | "irdev" string
 *         | "frontend" string
 *         | "frontend_port" integer
 *         | "button" string packet
 *         | "cmdport" integer
 *         | "include" string
 *         | "out_file" string
 * XXX out of date....
 * packet ::= " { " integer* " } "
 */

char *
read_string (FILE * in)
{
  char buffer[BUFSIZ], *b = buffer;
  char c;
  char end = ' ';
  if (feof (in))
    return NULL;
  c = fgetc (in);
  while (isspace (c))
    c = fgetc (in);
  if (c == '#')
    {
      /* Comment until end of line. */
      for (;;)
        {
          c = fgetc (in);
          if (c == '\n' || feof (in))
            /* Now read the next string. */
            return read_string (in);
        }
    }
  if (c == '\"' || c == '\'')
    end = c;
  else
    {
      /* c is first char of string */
      end = ' ';
      *b++ = c;
    }
  while (!feof (in))
    {
      c = fgetc (in);
      if (c == end || (end == ' ' && isspace (c)))
        break;
      *b++ = c;
    }
  *b++ = '\0';
  return strdup (buffer);
}

int
read_integer (FILE * in)
{
  char *str = read_string (in);
  int rv;
  if (!str)
    return 0;
  rv = atoi (str);
  free (str);
  return rv;
}

/* Button dictionary IO */
void
read_buttondict (ServerOpts *opts, IRServerInfo *si, const char *file)
{
  FILE *in;
  if (opts->verbose)
    fprintf (stdout, "Reading button dictionary file '%s'\n", file);
  in = fopen(file, "r");
  if (!in)
    fatal(0, "Can't read button dictionary file '%s'\n", file);
  while (!feof (in))
    {
      char buffer[BUFSIZ];
      if (!fscanf (in, "%s", buffer) || feof (in))
        break;
      if (!strcmp (buffer, "button")) {
        IRPacket *k;
        char *id;
        id = read_string (in);
        k = irpacket_scanf (in);
        irdict_insert (si->buttondict, id, k);
      } else if (buffer[0] == '#') {
        char c;
        for (;;)
          {
            c = fgetc (in);
            if (c == '\n' || feof (in))
              break;
          }
      } else {
        fatal(0, "Unknown entry '%s' in button dictionary '%s'\n",
              buffer, file);
      }
    }
  fclose (in);
}

void write_buttondict (ServerOpts *opts, IRServerInfo *si, const char *file)
{
  FILE *out;
  IRSymbol *s;

  if (opts->verbose)
    fprintf (stdout, "Writting button dictionary to '%s'\n", file);
  out = fopen (file, "w");
  if (!out)
    fatal(0, "Can't write button dictionary file '%s'\n", file);
  for (s = si->buttondict->first; s; s = s->next)
  {
    fprintf (out, "button %s ", s->name);
    irpacket_printf (out, s->packet);
    fprintf (out, "\n");
  }
  fclose (out);
}

/* Action IO */
Action *
read_action (FILE *in)
{
  char *id = read_string (in);
  switch (decode_keyword(id)) {
  case k_keypress:
    return new_action(action_keypress, read_string (in));
  case k_multitap:
    return new_action(action_multitap, read_string (in));
  case k_transmit:
    return new_action(action_transmit, read_string (in));
  case k_set_keymap:
    return new_action(action_set_keymap, read_string (in));
  case k_vlc:
    return new_action(action_vlc, read_string (in));
  case k_begin: {
    /* Read an action sequence */
    Action *a, *last_a = NULL, *first_a = NULL;
    while (!feof (in)) {
      a = read_action (in);
      if (!first_a)
        first_a = a;
      if (last_a)
        last_a->next = a;
      if (!a)
        break;
      last_a = a;
    }
    return first_a;
  }
  case k_end:
    return NULL;
  case k_applescript:
    return new_action (action_applescript, read_string (in));
  default:
    fatal (0, "Unknown action '%s'\n", id);
  }
  return NULL;
}

/*
  keymap "vlc"
    key "up" keypress up
    key "down" keypress down
  end
 */
Keymap *
read_keymap (FILE *in)
{
  char *id = read_string (in);
  Keymap *km = new_keymap (id);
  char *key_id;
  Action *action;
  free (id);
  while (!feof (in))
    {
      id = read_string (in);
      switch (decode_keyword (id))
        {
        case k_key:
          free (id);
          key_id = read_string (in);
          action = read_action (in);
          keymap_add_action (km, key_id, action);
          free (key_id);
          break;
        case k_end:
          /* Finish */
          free (id);
          return km;
          break;
        case k_inherit:
          free (id);
          if (km->inherit_name)
            fatal (1, "Duplicate inherit statement in keymap '%s'\n",
                   km->name);
          km->inherit_name = read_string (in);
          break;
        default:
          fatal (1, "Unknown keyword '%s' in keymap", id);
          return NULL;
        }
    }
  return NULL;
}

/* Config file IO */
void
read_config (ServerOpts *opts, IRServerInfo *si, const char *file)
{
  FILE *in;

  if (opts->verbose)
    fprintf (stdout, "Reading config file '%s'\n", file);

  in = fopen (file, "r");
  if (!in)
    fatal (0, "Can't read config file '%s'", file);
  while (!feof (in))
    {
      char buffer[BUFSIZ];
      if (!fscanf (in, "%s", buffer) || feof (in))
        break;

      switch (decode_keyword(buffer))
        {
        case k_button:
          {
            IRPacket *k;
            char *id;
            id = read_string (in);
            k = irpacket_scanf (in);
            irdict_insert (si->buttondict, id, k);
            break;
          }
        case k_irdev:
          opts->irdev = read_string (in);
          break;
        case k_frontend:
          opts->frontend_host = read_string (in);
          break;
        case k_frontend_port:
          opts->frontend_port = read_integer (in);
          break;
        case k_cmdport:
          opts->cmdport = read_integer (in);
          break;
        case k_include:
          {
            char *f = read_string (in);
            read_config (opts, si, f);
            free (f);
            break;
          }
        case k_jitter:
          irtoy_jitter = read_integer (in);
          break;
        case k_gap:
          irtoy_gap = read_integer (in);
          break;
        case k_packet_timeout:
          ir_packet_timeout = read_integer (in);
          break;
        case k_debounce_time:
          ir_debounce_time = read_integer (in);
          break;
        case k_out_file:
          opts->out_file = read_string (in);
          break;
        case k_vlc_host:
          opts->vlc_host = read_string (in);
          break;
        case k_vlc_port:
          opts->vlc_port = read_integer (in);
          break;
        case k_uinput_dev:
          opts->uinput_dev = read_string (in);
          break;
        case k_buttondict:
          opts->buttondict_fname = read_string (in);
          break;

        case k_keymap: {
          Keymap *km = read_keymap (in);
          dict_set(si->keymaps, km->name, km);
          /* Set current keymap to most recently defined keymap */
          si->current_keymap = km;
          break;
        }

        default:
          if (buffer[0] == '#')
            {
              /* Comment */
              char c;
              for (;;)
                {
                  c = fgetc (in);
                  if (c == '\n' || feof (in))
                    break;
                }
            }
          else
            fatal (0, "Unknown config file entry '%s'\n", buffer);
        }
    }
  fclose (in);
}


/* ------------------------------------------------------------
 * VLC remote connection
 */
bool
vlc_command (IRServerInfo *si, const char *command)
{
  Connection *n = si->vlc;
  if (n)
    {
      connection_write (n, command, strlen (command));
      connection_write (n, "\n", 1);
      if (si->verbose)
        fprintf (stdout, "Sent command '%s' to VLC\n", command);
      return true;
    }
  else
    {
      return false;
    }
}

void
can_read_vlc (Connection *n, void *h)
{
  char buffer[BUFSIZ];
  int fd = connection_fd (n);
  int count = read (fd, buffer, BUFSIZ);
  IRServerInfo *si = (IRServerInfo *)h;

  if (count == 0)
    {
      if (si->vlc != n)
        fatal (0, "can_read_vlc(n): n != n->server->vlc");
      si->vlc = NULL;
      close (fd);
      connection_remove (n);
    }
  fprintf (stdout, "VLC response:\n");
  fflush (stdout);
  write (fileno (stdout), buffer, count);
  fprintf (stdout, "\n");
}

Connection *
open_vlc (IRServerInfo *si, char *host, int port)
{
  Connection *vlc;
  int fd;
  if (si->verbose)
    fprintf (stdout, "Opening VLC port %s:%d\n", host, port);
  vlc = connection_port (si->server, host, port, si);
  if (!vlc)
    return NULL;
  fd = connection_fd (vlc);
  if (fd == -1)
    return NULL;

  /* Set it non-blocking */
  fcntl (fd, F_SETFL, fcntl (fd, F_GETFL) | O_NONBLOCK);
  connection_set_can_read(vlc, can_read_vlc);
  si->vlc = vlc;
  return vlc;
}


/* ------------------------------------------------------------
 * Myth remote connection
 */

bool
mythremote_command (IRServerInfo *si, const char *command)
{
  Connection *n = si->mythremote;
  if (n)
    {
      connection_write (n, command, strlen (command));
      connection_write (n, "\n", 1);
      if (si->verbose)
        fprintf (stdout, "Sent command '%s' to MythTV\n", command);
      return true;
    }
  else
    {
      return false;
    }
}

void
can_read_mythremote (Connection * n, void *h)
{
  char buffer[BUFSIZ];
  int fd = connection_fd (n);
  int count = read (fd, buffer, BUFSIZ);
  IRServerInfo *si = (IRServerInfo *)h;

  if (count == 0)
    {
      if (si->mythremote != n)
        fatal (0, "can_read_mythremote(n): n != n->server->mythremote");
      si->mythremote = NULL;
      close (fd);
      connection_remove (n);
    }
  fprintf (stdout, "mythtv response:\n");
  fflush (stdout);
  write (fileno (stdout), buffer, count);
  fprintf (stdout, "\n");
}

Connection *
open_mythremote (IRServerInfo *si, char *host, int port)
{
  Connection *mythremote;
  int fd;
  if (si->verbose)
    fprintf (stdout, "Opening MythRemote port %s:%d\n", host, port);
  mythremote = connection_port (si->server, host, port, si);
  if (!mythremote)
    return NULL;
  fd = connection_fd (mythremote);
  if (fd == -1)
    return NULL;

  /* Set it non-blocking */
  fcntl (fd, F_SETFL,
         fcntl (fd, F_GETFL) | O_NONBLOCK);
  connection_set_can_read (mythremote, can_read_mythremote);
  si->mythremote = mythremote;
  return mythremote;
}


/* ------------------------------------------------------------
 * IR connection
 */

IRPacket *
transmit_button (IRServerInfo *si, const char *button)
{
  IRPacket *k;
  k = irdict_lookup_name (si->buttondict, button);
  if (k)
    {
      int i;
      char *buffer, *b;
      int len = 3 + 2 * k->n_pulses;
      buffer = malloc (len);
      b = buffer;

      *b++ = 3;                 /* start transmission */
      for (i = 0; i < k->n_pulses; i++)
        {
          *b++ = (k->pulses[i].width) >> 8;     /* high byte */
          *b++ = (k->pulses[i].width) & 0xff;   /* low byte */
        }
      *b++ = 0xff;
      *b++ = 0xff;

      if (si->verbose)
        {
          fprintf (stdout, "Transmitting: ");
          for (i = 0; i < len; i++)
            fprintf (stdout, "%d ", buffer[i]);
          fprintf (stdout, "\n");
        }

      if (si->irdev)
        {
          char response[3];
	  int fd = connection_fd (si->irdev);
          connection_write (si->irdev, buffer, len);

          /* Temporarily set the connection blocking. Blugh. */
          fcntl (fd, F_SETFL,
                 fcntl (fd, F_GETFL) & ~O_NONBLOCK);

          /* Read the return message */
          read (fd, response, 3);
          fprintf (stdout, "Returned %d (%c) %d %d\n",
                   response[0], response[0], response[1], response[2]);

          /* Set blocking again */
          fcntl (fd, F_SETFL,
                 fcntl (fd, F_GETFL) | O_NONBLOCK);
        }
      else if (si->verbose)
        {
          fprintf (stdout, "(No IR connection to transmit on)\n");
        }
      free (buffer);
      return k;
    }
  return NULL;
}

/* Receive a button-press packet */
void
receive_button (IRServerInfo *si, Connection * n, const char *name)
{
  bool repeated = false;

  /* Some remotes use a 'repeat last keypress' symbol. Detect this and
   * repeat the last keypress we emitted. 
   */
  if (!strcmp(name, "REPEAT"))
    {
      if (si->verbose)
        fprintf (stdout, "REPEAT symbol from remote -> '%s'\n",
                 si->last_button);
      name = si->last_button;
      if (!name)
        /* No previous keypress. Weird, but possible if packet was
         *  lost. Ignore.
         */
        return;
    }
  if (   si->last_button != NULL
      && (   name == si->last_button
          || !strcmp(name, si->last_button)))
    {
      /* Possibly repeated button press */
      struct timeval tv;
      gettimeofday (&tv, NULL);
      if (tv.tv_sec > si->next_repeat_time.tv_sec
          || (tv.tv_sec == si->next_repeat_time.tv_sec
              && tv.tv_usec >= si->next_repeat_time.tv_usec))
        {
          /* Repeat keypress */
          if (si->verbose)
            fprintf (stdout, "Repeated keypress ok\n");
            repeated = true;
        }
      else
        {
          /* Repeated keypress, but within repeat 
             timeout. Don't process button, and don't reset repeat
             time. */
          if (si->verbose)
            fprintf (stdout, "Dropping too-soon keypress\n");
          return;
        }
    }
  
  /* First press of a new/different button, or after repeat time
     has elapsed.  */
  handle_button (si, name);
  if (repeated)
    {
      /* Accelerate repeat time */
      ir_repeat_delay -= (ir_debounce_time / 16);
      if (ir_repeat_delay < 0)
        ir_repeat_delay = 0;
      if (si->verbose)
        fprintf (stdout, "repeated button, ir_repeat_delay=%d\n",
                 ir_repeat_delay);
    }
  else
    {
      /* Set initial repeat time */
      ir_repeat_delay = ir_debounce_time;
      if (si->verbose)
        fprintf (stdout, "pressed button, ir_repeat_delay=%d\n",
                 ir_repeat_delay);
  }

  /* Set earliest repeat time to ir_repeat_delay usecs in the
     future. */
  gettimeofday (&si->last_button_time, NULL);
  si->next_repeat_time = si->last_button_time;
  si->next_repeat_time.tv_usec += ir_repeat_delay;
  if (si->next_repeat_time.tv_usec >= 1000000)
    {
      si->next_repeat_time.tv_sec +=
        si->next_repeat_time.tv_usec / 1000000;
      si->next_repeat_time.tv_usec =
        si->next_repeat_time.tv_usec % 1000000;
    }
  si->last_button = name;
}

void
can_read_ir (Connection * n, void *h)
{
  IRPacket *k;
  int count;
  unsigned char c;
  IRServerInfo *si = (IRServerInfo *)h;
  int fd = connection_fd (n);
  count = read (fd, &c, 1);
  if (count != 0)
    {
      count = irstate_rxbytes (si->ir, 1, &c, &k);
      if (count)
        {
          /* Received a complete packet from the IR interface */
          const char *name;
          if (si->verbose)
            {
              fprintf (stdout, "Received IR packet: ");
              irpacket_printf (stdout, k);
              fprintf (stdout, "\n");
              irpacket_render (stdout, k);
              fprintf (stdout, "\n");
            }
          name = irdict_lookup_packet (si->buttondict, k);
          if (si->out_file)
            {
              if (name)
                fprintf (si->out_file, "key \"%s\" ", name);
              else
                fprintf (si->out_file, "key %s ",
                         si->unknown_key? si->unknown_key : "UNKNOWN");
              irpacket_printf (si->out_file, k);
              fprintf (si->out_file, "\n");
              fflush (si->out_file);
            }
          if (name)
            receive_button (si, n, name);
          else if (si->verbose)
            fprintf (stdout, "Unknown packet\n");
        }
    }
  else
    {
      if (si->verbose)
        fprintf (stdout, "Closed IR connection\n");
      close (fd);
      if (si->irdev != n)
        fatal (0, "can_read_ir(n): n != si->irdev");
      si->irdev = NULL;
      connection_remove (n);
    }
}

void
timeout_ir (Connection * n, void *h)
{
  IRServerInfo *si = (IRServerInfo *)h;
  IRPacket *k = irstate_timeout (si->ir);

  if (k)
    {
      const char *name;
      fprintf (stdout, "Received IR packet on timeout: ");
      irpacket_printf (stdout, k);
      fprintf (stdout, "\n");
      irpacket_render (stdout, k);
      fprintf (stdout, "\n");
      name = irdict_lookup_packet (si->buttondict, k);
      if (si->out_file)
        {
          if (name)
            fprintf (si->out_file, "key \"%s\" ", name);
          else
            fprintf (si->out_file, "key %s ",
                     si->unknown_key? si->unknown_key : "UNKNOWN");

          irpacket_printf (si->out_file, k);
          fprintf (si->out_file, " # on timeout\n");
          fflush (si->out_file);
        }
      if (name)
        {
          fprintf (stdout, "Button name '%s'\n", name);
          receive_button (si, n, name);
        }
      else
        {
          fprintf (stdout, "Unknown packet\n");
        }
    }
    /* Reset last button and repeat timer */
    si->last_button = NULL;
    ir_repeat_delay = 0;

}

/* ------------------------------------------------------------
 * uinput connection
 */

Connection *
open_uinput (IRServerInfo *si, const char *dev)
{
#ifdef USE_UINPUT
  int fd;
  Connection *n;
  struct uinput_user_dev uinp;
  int i;

  if (si->verbose)
    fprintf (stdout, "Opening uinput device '%s'\n", dev);
  fd = open (dev, O_WRONLY);
  if (fd < 0)
    fatal (0, "Cannot open uinput device '%s'", dev);

  /* For now, just key events. */
  ioctl (fd, UI_SET_EVBIT, EV_KEY);
  ioctl (fd, UI_SET_EVBIT, EV_REP);
  for (i = 0; i < 256; i++)
    {
      ioctl (fd, UI_SET_KEYBIT, i);
    }

  memset (&uinp, '\0', sizeof uinp);
  strncpy (uinp.name, "IR Controller", UINPUT_MAX_NAME_SIZE);
  uinp.id.version = 4;          /* ? */
  uinp.id.bustype = BUS_USB;

  write (fd, &uinp, sizeof uinp);

  if (ioctl (fd, UI_DEV_CREATE))
    {
      fatal (0, "Cannot create uinput device");
    }

  n = new_connection (si->server, fd, "uinput", si);

  return n;
#else
  return NULL;
#endif
}

void
uinput_key (Connection * n, int key, int value)
{
#ifdef USE_UINPUT
  struct input_event ev;
  int fd = connection_fd (n);
  memset (&ev, '\0', sizeof ev);
  ev.type = EV_KEY;
  ev.code = key;
  ev.value = value;
  write (fd, &ev, sizeof ev);
  ev.type = EV_SYN;
  ev.code = 0;
  ev.value = 0;
  write (fd, &ev, sizeof ev);
#endif
}

void
uinput_key_press (Connection * n, int key)
{
  uinput_key (n, key, 1);
  uinput_key (n, key, 0);
}

#ifdef USE_UINPUT
bool
send_keypress (IRServerInfo *si, int key)
{
  if (si->uinput)
    {
      uinput_key_press (si->uinput, key);
      return true;
    }
  else
    return false;
}

bool
send_key (IRServerInfo *si, int key, int value)
{
  if (si->uinput)
    {
      uinput_key (si->uinput, key, value);
      return true;
    }
  else
    return false;
}
#else
#define send_key(si,k,v1) (0)
#define send_keypress(si,k) (0)
#endif


/* ------------------------------------------------------------
 * Multi-tap input
 */

#ifdef USE_UINPUT
struct timeval multitap_last_time;

int multitap_last_key;

typedef struct MultiTapState MultiTapState;

struct MultiTapState
{
  unsigned key;
  MultiTapState *loop;
};

MultiTapState mts_2[] = {
  {KEY_A},
  {KEY_B},
  {KEY_C, mts_2}
};

MultiTapState mts_3[] = {
  {KEY_D},
  {KEY_E},
  {KEY_F, mts_3}
};

MultiTapState mts_4[] = {
  {KEY_G},
  {KEY_H},
  {KEY_I, mts_4}
};

MultiTapState mts_5[] = {
  {KEY_J},
  {KEY_K},
  {KEY_L, mts_5}
};

MultiTapState mts_6[] = {
  {KEY_M},
  {KEY_N},
  {KEY_O, mts_6}
};

MultiTapState mts_7[] = {
  {KEY_P},
  {KEY_Q},
  {KEY_R},
  {KEY_S, mts_7}
};

MultiTapState mts_8[] = {
  {KEY_T},
  {KEY_U},
  {KEY_V, mts_8}
};

MultiTapState mts_9[] = {
  {KEY_W},
  {KEY_X},
  {KEY_Y},
  {KEY_Z, mts_9}
};

MultiTapState mts_0[] = {
  {KEY_SPACE, mts_0}
};

MultiTapState *mt_key_states[] = {
  ['2'] = mts_2,
  ['3'] = mts_3,
  ['4'] = mts_4,
  ['5'] = mts_5,
  ['6'] = mts_6,
  ['7'] = mts_7,
  ['8'] = mts_8,
  ['9'] = mts_9,
};

MultiTapState *multitap_current_state;

#endif /* USE_UINPUT */

bool
multitap_tap (IRServerInfo *si, int key)
{
#ifdef USE_UINPUT
  struct timeval now;
  struct timeval diff;
  if (gettimeofday (&now, NULL))
    fatal (0, "Can't get time");
  diff.tv_sec = now.tv_sec - multitap_last_time.tv_sec;
  diff.tv_usec = now.tv_usec - multitap_last_time.tv_usec;
  if (diff.tv_usec < 0 || diff.tv_usec >= 1000000)
    {
      /* borrow */
      diff.tv_usec += 1000000;
      diff.tv_sec -= 1;
    }
  multitap_last_time = now;

  /* Handle end/timeout: go to idle state. */
  if (diff.tv_sec >= 1 || key != multitap_last_key)
    {
      multitap_last_key = key;
      multitap_current_state = NULL;
    }

  if (multitap_current_state)
    {
      /* delete last inserted char */
      send_keypress (si, KEY_BACKSPACE);
      /* advance to next state state */
      if (multitap_current_state->loop)
        multitap_current_state = multitap_current_state->loop;
      else
        multitap_current_state++;
    }
  else
    {
      /* initialise state from mt_key_states */
      multitap_current_state = mt_key_states[key & 0xff];
    }

  /* insert char for new state */
  if (multitap_current_state)
    {
      send_keypress (si, multitap_current_state->key);
      return true;
    }
#endif /* USE_UINPUT */
  return false;
}


/* ------------------------------------------------------------
 * Server
 */

#define TRACE(x...) do { if (si->verbose) fprintf (stdout, x); } while (0)

Action *find_action_for_button (IRServerInfo *si, const char *button)
{
  Keymap *km = si->current_keymap;
  Action *a = NULL;
  /* Search through keymaps until we find an action. */
  while (km && !a)
    {
      Keymap *last_km = km;
      TRACE("Decode button '%s' with keymap '%s'\n", button, km->name);
      a = dict_get(km->mapping, button);
      if (!a)
        {
          if (km->inherit)
            km = km->inherit;
          else if (km->inherit_name)
            {
              km->inherit = dict_get (si->keymaps, km->inherit_name);
              if (!km->inherit)
                warning ("Can't find keymap '%s' (from keymap '%s')\n",
                         km->inherit_name, km->name);
              km = km->inherit;
            }
          else
            /* No more keymaps to search. */
            km = NULL;
          if (km) TRACE ("Decode with inherited keymap '%s'\n",
                         km->name);
          if (km == si->current_keymap)
            fatal (1, "Loop in keymap inheritance, '%s' includes '%s'\n",
                   last_km->name, km->name);
        }
    }
  return a;
}

void server_action (IRServerInfo *si, Action *a)
{
  while (a) {
    switch (a->id)
      {
      case action_keypress:
        // XXX must decode to a number from string. Urk.
        // send_keypress (v, XXX);
        mac_key (a->operand);
        break;
      case action_multitap:
        multitap_tap (si, a->operand[0]);
        break;
      case action_mythtv:
        mythremote_command (si, a->operand);
        break;
      case action_transmit:
        transmit_button (si, a->operand);
        break;
      case action_set_keymap:
        si->current_keymap = dict_get (si->keymaps, a->operand);
        if (si->verbose)
          fprintf (stdout, "Setting keymap to '%s'\n", a->operand);
        if (!si->current_keymap)
          fatal (0, "Cannot find keymap '%s'\n", a->operand);
        break;
      case action_vlc:
        vlc_command(si, a->operand);
        break;
      case action_applescript:
        osascript (a->operand);
        break;
      }
    /* Next action in sequence */
    a = a->next;
  }
}


/* This is the biggie. Decode commands and map them to actions.
 */
bool
handle_button (IRServerInfo *si, const char *button)
{
  Action *a;
  if (si->verbose)
    fprintf (stdout, "Got button press '%s'\n", button);
  a = find_action_for_button (si, button);
  if (a)
    {
      server_action (si, a);
      return true;
    }
  else
    {
      TRACE ("Cannot find button '%s' via keymap, reverting to old routine\n",
             button);
    }
  switch (decode_keyword(button))
    {
      /* Numbers: same everywhere. */
    case k_dvd_0: case k_dvdrw_0:
      return mythremote_command (si, "key 0") || multitap_tap (si, '0')
        || send_keypress (si, KEY_0) || mac_key("0");
    case k_dvd_1: case k_dvdrw_1:
      return mythremote_command (si, "key 1") || multitap_tap (si, '1')
        || send_keypress (si, KEY_1) || mac_key("1");
    case k_dvd_2: case k_dvdrw_2:
      return mythremote_command (si, "key 2") || multitap_tap (si, '2')
        || send_keypress (si, KEY_2) || mac_key("2");
    case k_dvd_3: case k_dvdrw_3:
      return mythremote_command (si, "key 3") || multitap_tap (si, '3')
        || send_keypress (si, KEY_3) || mac_key("3");
    case k_dvd_4:
    case k_dvdrw_4:
      return mythremote_command (si, "key 4") || multitap_tap (si, '4')
        || send_keypress (si, KEY_4) || mac_key("4");
    case k_dvd_5:
    case k_dvdrw_5:
      return mythremote_command (si, "key 5") || multitap_tap (si, '5')
        || send_keypress (si, KEY_5) || mac_key("5");
    case k_dvd_6:
    case k_dvdrw_6:
      return mythremote_command (si, "key 6") || multitap_tap (si, '6')
        || send_keypress (si, KEY_6) || mac_key("6");
    case k_dvd_7:
    case k_dvdrw_7:
      return mythremote_command (si, "key 7") || multitap_tap (si, '7')
        || send_keypress (si, KEY_7) || mac_key("7");
    case k_dvd_8:
    case k_dvdrw_8:
      return mythremote_command (si, "key 8") || multitap_tap (si, '8')
        || send_keypress (si, KEY_8) || mac_key("8");
    case k_dvd_9:
    case k_dvdrw_9:
      return mythremote_command (si, "key 9") || multitap_tap (si, '9')
        || send_keypress (si, KEY_9) || mac_key("9");

      /* Navigation is more or less universal. */
    case k_dvd_left:
    case k_dvdrw_left:
      return mythremote_command (si, "key left")
        || vlc_command (si, "rewind") || send_keypress (si, KEY_LEFT)
        || mac_key ("LeftArrow");
    case k_dvd_right:
    case k_dvdrw_right:
      return mythremote_command (si, "key right")
        || vlc_command (si, "fastforward")
        || send_keypress (si, KEY_RIGHT)
        || mac_key ("RightArrow");
    case k_dvd_up:
    case k_dvdrw_up:
      return mythremote_command (si, "key up") || send_keypress (si, KEY_UP)
        || mac_key ("UpArrow");
    case k_dvd_down:
    case k_dvdrw_down:
      return mythremote_command (si, "key down") || send_keypress (si, KEY_DOWN)
        || mac_key ("DownArrow");
    case k_dvd_ok:
    case k_dvdrw_ok:
      return mythremote_command (si, "key enter")
        || send_keypress (si, KEY_ENTER)
        || mac_key ("Return");

      /* Old DVD remote for MythTV etc. */
    case k_dvd_next:
      return mythremote_command (si, "key end") || send_keypress (si, KEY_COMMA);
    case k_dvd_prev:
      return mythremote_command (si, "key home") || send_keypress (si, KEY_DOT);
    case k_dvd_stop:
      return send_keypress (si, KEY_X);
    case k_dvd_power:
      return send_keypress (si, KEY_S);
    case k_dvd_title:
      return
        mythremote_command (si, "key escape") || send_keypress (si, KEY_ESC);
    case k_dvd_pause:
      return mythremote_command (si, "key p") || vlc_command (si, "play")
        || send_keypress (si, KEY_SPACE);
      return send_keypress (si, KEY_P);
    case k_dvd_menu:
      return mythremote_command (si, "key m") || send_keypress (si, KEY_TAB);
      return send_keypress (si, KEY_C);
    case k_dvd_display:
      return mythremote_command (si, "key i") || send_keypress (si, KEY_M);
    case k_dvd_subtitle:
      return send_keypress (si, KEY_T);  /* or T? */


      /* DVDRW remote to control Kodi
       * Generally most useful non-direct control keys are:
       * 'm' -- on-screen controls
       * 'tab' -- toggle between menus and playback
       * 'backspace' -- back: probably what 'esc' should have been.
       * 'esc' -- back up menus. Not needed really.
       */
    case k_dvdrw_last:
      return send_keypress (si, KEY_COMMA);
    case k_dvdrw_first:
      return send_keypress (si, KEY_DOT);

    case k_dvdrw_disc:
      return send_keypress (si, KEY_BACKSPACE)
        || mac_key("Escape");
    case k_dvdrw_system:
      return send_keypress (si, KEY_TAB);

      /* Mash random buttons: on-screen controls */
    case k_dvdrw_top_menu:
    case k_dvdrw_edit:
    case k_dvdrw_select:
      return send_keypress (si, KEY_M) || mac_key("M");

    case k_dvdrw_stop:
      return send_keypress (si, KEY_X);
    case k_dvdrw_pause:
      return send_keypress (si, KEY_SPACE) || mac_key(" ");
    case k_dvdrw_play:
      return send_keypress (si, KEY_P) || mac_key("P");
    case k_dvdrw_subtitle:
      return send_keypress (si, KEY_T) || mac_key("S");
    case k_dvdrw_audio:
      return mac_key("A");

    default:
      return false;
    }
}

void
idle (Connection * n, void *h)
{
  static int i;
  const char cs[] = "/-\\|";
  fprintf (stdout, "\r%c\r", cs[i++]);
  fflush (stdout);
  if (!cs[i])
    i = 0;
}

int
main_server (ServerOpts * opts)
{
  Connection *cmdsock = NULL;
  Connection *irdev = NULL;
  struct timeval last_check;
  IRServerInfo *si;

  gettimeofday (&last_check, NULL);

  si = new_irserverinfo ();
  si->server = new_server (si);
  si->buttondict = new_irdict ();
  si->ir = new_irstate ();

  if (opts->config_file)
    read_config (opts, si, opts->config_file);

  si->verbose = opts->verbose;
  server_set_timeout (si->server, ir_packet_timeout);

  /* Read in a button dictionary if provided */
  if (opts->buttondict_fname)
    {
      read_buttondict (opts, si, opts->buttondict_fname);
    }
  if (si->verbose)
    {
      Connection *idler;
      IRSymbol *m;
      if (!opts->daemon && isatty (fileno (stdout)))
        {
          idler = new_connection (si->server, 0, "idler", si);
	  connection_set_timeout(idler, idle);
        }

      fprintf (stdout, "cmdport: %d\n", opts->cmdport);
      fprintf (stdout, "irdev: %s\n", opts->irdev);
      fprintf (stdout, "frontend_host: %s\n", opts->frontend_host);
      fprintf (stdout, "frontend_port: %d\n", opts->frontend_port);

      fprintf (stdout, "IR symbol dictionary\n");
      for (m = si->buttondict->first; m; m = m->next)
        {
          fprintf (stdout, "button %s ", m->name);
          irpacket_printf (stdout, m->packet);
          fprintf (stdout, "\n");
        }
    }

  /* Open command server port */
  if (opts->cmdport)
    {
      cmdsock = server_listenport (si->server, opts->cmdport, 10, si);
      connection_set_can_read (cmdsock, can_read_cmdport);
    }

  /* Open infrared device */
  if (opts->irdev)
    {
      int fd;
      irstate_open (si->ir, opts->irdev);
      fd = si->ir->fd;
      if (fd == -1)
        fatal (0, "Can't open device '%s'\n", opts->irdev);
      irdev = new_connection (si->server, fd, "irdev", si);
      connection_set_can_read (irdev, can_read_ir);
      connection_set_timeout (irdev, timeout_ir);
      si->irdev = irdev;
    }

  /* Open myth remote port */
  if (opts->frontend_host)
    open_mythremote (si, opts->frontend_host, opts->frontend_port);

  /* Open VLC remote */
  if (opts->vlc_host)
    open_vlc (si, opts->vlc_host, opts->vlc_port);

  /* Open uinput device */
  if (opts->uinput_dev)
    si->uinput = open_uinput (si, opts->uinput_dev);

  /* Open dump file if any */
  if (opts->out_file)
    {
      si->out_file = fopen (opts->out_file, "w");
      if (!si->out_file)
        fatal (0, "Couldn't open output file '%s'\n", opts->out_file);
    }

  /* Kick off daemon if needed */
  if (opts->daemon) {
    pid_t pid;
    fprintf(stderr, "irtoy daemon mode...\n");
    pid = fork();
    if (pid < 0)
      exit(EXIT_FAILURE);
    if (pid > 0)
      exit(EXIT_SUCCESS);

    /* New session, please */
    if (setsid() < 0)
      exit(EXIT_FAILURE);
    
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    pid = fork();
    if (pid < 0)
      exit(EXIT_FAILURE);
    if (pid > 0)
      exit(EXIT_SUCCESS);
    umask(0);
    chdir("/");

    /* Close stdin/stdout etc. */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    open("/dev/null", O_RDONLY); // stdin
    open("/dev/null", O_WRONLY|O_CREAT);
    open("/dev/null", O_WRONLY|O_CREAT);

    opts->verbose = false;
  }

  /* Main loop */
  for (;;)
    {
      struct timeval now;
      gettimeofday (&now, NULL);
      if (now.tv_sec != last_check.tv_sec)
        {
          last_check = now;
          if (opts->frontend_host && !si->mythremote)
            {
              if (si->verbose)
                fprintf (stdout, "Trying to connect to myth...\n");
              open_mythremote (si, opts->frontend_host, opts->frontend_port);
            }
          if (opts->vlc_host && !si->vlc)
            {
              if (si->verbose)
                fprintf (stdout, "Trying to connect to VLC...\n");
              open_vlc (si, opts->vlc_host, opts->vlc_port);
            }
        }
      server_select (si->server);
    }

  return 0;
}

/* ------------------------------------------------------------
 */

void
help (const char *argv0)
{
  fprintf (stdout, ("Syntax: %s [-t] [-f config_file] [-i device]"
                    " [-p cmdport] [-h frontend] [-d]\n"), argv0);
  exit (0);
}

int
main (int argc, char *argv[])
{
  int i;
  char *c;
  ServerOpts opts;
  opts.config_file = NULL;
  opts.irdev = NULL;
  opts.cmdport = 0;
  opts.frontend_host = NULL;
  opts.frontend_port = 6546;
  opts.verbose = false;
  opts.out_file = NULL;
  opts.vlc_host = NULL;
  opts.vlc_port = 0;
  opts.uinput_dev = NULL;
  opts.buttondict_fname = NULL;
  opts.daemon = false;

  for (i = 1; i < argc; i++)
    {
      if (argv[i][0] == '-')
        {
          switch (argv[i][1])
            {
            case 'a':
              /* Analyse packet files */
              analyse_packet_files (argc - (i + 1), &argv[i + i]);
              return 0;
            case 't':
              /* Test mode */
              return test_main (argc - (i + 1), &argv[i + 1]);
            case 'd':
              opts.daemon = true;
              break;
            default:
              help (argv[0]);
            case 'f':          /* -f <config file> */
              if (argv[i][2])
                opts.config_file = &argv[i][2];
              else if (++i < argc)
                opts.config_file = argv[i];
              else
                help (argv[0]);
              break;
            case 'i':          /* -i <ir device> */
              if (argv[i][2])
                opts.irdev = &argv[i][2];
              else if (++i < argc)
                opts.irdev = argv[i];
              else
                help (argv[0]);
              break;
            case 'p':          /* -p <cmdport> */
              if (argv[i][2])
                opts.cmdport = atoi (&argv[i][2]);
              else if (++i < argc)
                opts.cmdport = atoi (argv[i]);
              else
                help (argv[0]);
              break;
            case 'v':
              opts.verbose = true;
              break;
            case 'h':          /* -h <frontend host> */
              if (argv[i][2])
                opts.frontend_host = &argv[i][2];
              else if (++i < argc)
                opts.frontend_host = argv[i];
              else
                help (argv[0]);
              c = strchr (opts.frontend_host, ':');
              if (c)
                {
                  *c = '\0';
                  opts.frontend_port = atoi (c + 1);
                }
              break;
            case 'o':
              if (argv[i][2])
                opts.out_file = &argv[i][2];
              else if (++i < argc)
                opts.out_file = argv[i];
              else
                help (argv[0]);
              break;
            }
        }
      else
        {
          help (argv[0]);
        }
    }
  return main_server (&opts);
}
