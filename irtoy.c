#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>
#include "irtoy.h"
#include "error.h"
#include "dict.h"

int irtoy_gap = 8;              /* min gap between packets */
int irtoy_jitter = 3;           /* acceptable jitter */

IRPacket *
new_irpacket (void)
{
  IRPacket *k;
  k = malloc (sizeof *k);
  k->n_pulses_allocated = 8;
  k->n_pulses = 0;
  k->pulses = calloc (k->n_pulses_allocated, sizeof *k->pulses);
  return k;
}

void
free_irpacket (IRPacket * k)
{
  if (k->pulses)
    free (k->pulses);
  free (k);
}

bool
irpacket_complete (IRPacket * k)
{
  /* XXX Complete the packet */
  return false;
}

void
irpacket_pulse (IRPacket * k, IRPulse pulse)
{
  /* Add a pulse to a packet */
  if (k->n_pulses_allocated == k->n_pulses)
    {
      k->n_pulses_allocated *= 2;
      k->pulses =
        realloc (k->pulses, k->n_pulses_allocated * sizeof *k->pulses);
    }
  k->pulses[k->n_pulses++] = pulse;
}

void
irpacket_printf (FILE * out, IRPacket * k)
{
  /* Print it out */
  int i;
  bool expected_value = true;
  if (!k)
    {
      fprintf (out, "NULL");
      return;
    }
  fprintf (out, " { ");
  for (i = 0; i < k->n_pulses; i++)
    {
#if 0
      if (k->pulses[i].value != expected_value)
        fatal (0, "Unexpected pulse value: pulse %d (of %d) has value %d",
               i, k->n_pulses, k->pulses[i].value);
#endif
      fprintf (out, "%d ", k->pulses[i].width);
      expected_value = !expected_value;
    }
  fprintf (out, "} ");
}

void
irpacket_decode (IRPacket * k)
{
#define IMUL(x, f) (((int)(512 * (f)) * x ) / 512)
  int i;
  int packet_len = 0;
  int min_width = 0;
  int max_bits = 0;
  int min_bits = 0;
  int min_res = 2;              /* minimum resolution of +/- 2 */
  const double proportional_error = 1.1;
  
  for (i = 0; i < k->n_pulses; i++)
    {
      packet_len += k->pulses[i].width;
      if (k->pulses[i].width < min_width || min_width == 0)
        min_width = k->pulses[i].width;
    }

  printf("Decode packet: "); irpacket_printf(stdout, k);
  printf("\n"); irpacket_render(stdout, k);
  printf("\nMinimum pulse width = %d, total packet time = %d\n",
         min_width, packet_len);

  /* Minimum number of bits: there *must* be at least as many bits as
     there were pulses. Also, the minimum size of a pulse might set a
     lower bound: a pulse can be at most one bit wide, give or take. */
  if (IMUL(min_width, 0.9) > 0)
    min_bits = packet_len / (IMUL(min_width, 0.9));
  else
    min_bits = packet_len;
  
  printf("First guess at min bits: %d (by min pulse width)\n", min_bits);
  if (min_bits < k->n_pulses)
    {
      printf("Number of bits must be at least number of pulses, so %d\n",
             k->n_pulses);
      min_bits = k->n_pulses;
    }
  /* Maximum bits: our minimum resolution is +/- 3. If bits are any
     smaller than that, we stand no chance of recognising them. */
  max_bits = packet_len / (2 * min_res);
  printf("Can only fit %d bits (of min size %d) in %d time.\n",
         max_bits, 2*min_res, packet_len);

  printf("Packet has somewhere between %d and %d bits.\n", min_bits, max_bits);

#undef IMUL
}

void
irpacket_render (FILE * out, IRPacket * k)
{
  int term_width = 78;
  int total_width;
  int i, c, x;
  char *cols;
  if ((cols = getenv("COLUMNS")))
    {
      term_width = atoi(cols);
    }
  total_width = 0;
  for (i = 0; i < k->n_pulses; i++)
    total_width += k->pulses[i].width;
  c = 0;
  x = 0;
  for (i = 0; i < k->n_pulses; i++)
    {
      x += k->pulses[i].width;
      while (c < 1.0 * x * term_width / total_width)
        {
          c++;
          if (k->pulses[i].value)
            fputc ('|', out);
          else
            fputc ('_', out);
        }
    }
}

IRPacket *
irpacket_scanf (FILE * in)
{
  /* Read it from IN */
  IRPacket *k = new_irpacket ();
  char buffer[BUFSIZ];
  IRPulse pulse;
  pulse.value = 0;

  if (!fscanf (in, "%s", buffer) || feof (in))
    return NULL;
  if (strcmp (buffer, "{"))
    fatal (0, "Malformed packet: expected '{', got '%s'", buffer);
  while (!feof (in))
    {
      fscanf (in, "%s", buffer);
      if (strcmp (buffer, "}"))
        {
          int width = atoi (buffer);
          if (width < 0 || width > 0xffff)
            fatal (0, "Malformed packet: expected short int or '}', got '%s'",
                   buffer);
          pulse.width = width;
          pulse.value = !pulse.value;
          irpacket_pulse (k, pulse);
        }
      else
        {
          irpacket_complete (k);
          break;
        }
    }
  return k;
}

/* Do two packets match? */
bool
irpacket_match (IRPacket * a, IRPacket * b, int jitter)
{
  int i;
  int a_total = 0, b_total = 0;
  if (a->n_pulses != b->n_pulses)
    return false;
  for (i = 0; i < a->n_pulses; i++)
    {
      a_total += a->pulses[i].width;
      b_total += b->pulses[i].width;
      if (abs (a->pulses[i].width - b->pulses[i].width) <= jitter)
        continue;
      else
        return false;
    }
  /* The total packet time should also fit into the allowed jitter:
   * it's not cumulative.
   */
  if (abs (a_total - b_total) <= jitter)
    return true;
  else
    return false;
}

/* ------------------------------------------------------------
 * Dict/Symbol handling
 * XXX note: just about everything here is O(n). That shouldn't matter
 * though. 
 */

IRDict *
new_irdict (void)
{
  IRDict *d = malloc (sizeof *d);
  d->first = NULL;
  d->by_name = dict_new (NULL);
  return d;
}

/* Find a symbol (any symbol) for this name.
 */
IRPacket *
irdict_lookup_name (IRDict * d, const char *name)
{
  IRSymbol *s;
  s = dict_get (d->by_name, name);
  if (s)
    return s->packet;
  else
    return NULL;
}

/* Find a symbol name for this packet */
const char *
irdict_lookup_packet (IRDict * d, IRPacket * k)
{
  IRSymbol *s;
  for (s = d->first; s; s = s->next)
    if (irpacket_match (s->packet, k, irtoy_jitter))
      return s->name;
  return NULL;
}

/* Insert a symbol in the dictionary */
void
irdict_insert (IRDict * d, const char *name, IRPacket * k)
{
  IRSymbol *s = malloc (sizeof *s);
  s->next = d->first;
  s->name = name;
  s->packet = k;
  d->first = s;
  if (!dict_has_key (d->by_name, name))
    dict_insert (d->by_name, name, s);
  //XXX
  irpacket_decode(k);
}

/* ------------------------------------------------------------
 * IR State handling
 */

IRState *
new_irstate (void)
{
  IRState *ir;
  ir = malloc (sizeof *ir);
  ir->value = false;
  ir->packet = NULL;
  ir->last_width = -1;
  ir->fd = -1;
  ir->buf_valid = false;
  ir->timed_out = false;
  return ir;
}

IRPacket *
irstate_pulse (IRState * ir, unsigned short width)
{
  IRPacket *k;
  IRPulse p;
  bool timed_out = ir->timed_out;
  ir->timed_out = false;
  /* Previous signal value was IR->VALUE. This pulse marks the end of
     that phase with a pulse of width WIDTH. */
  if (width == 0xffff)
    {
      /* Dummy end pulse */
      if (ir->packet)
        irpacket_complete (ir->packet);
      k = ir->packet;
      ir->packet = NULL;
      ir->value = false;        /* idle */
      return k;
    }
  /* Real pulse, flip current state */
  ir->value = !ir->value;
  if (ir->value == false && width > irtoy_gap * ir->last_width)
    {
      /* Gap between packets */

      /* Previous packet was ended on a timeout. This is the gap
         between the previous packet and a new packet. Drop it on the
         floor. */
      if (timed_out)
        return NULL;
      if (!ir->packet)
        fatal (0, "Got gap without a current packet");
      irpacket_complete (ir->packet);
      k = ir->packet;
      ir->packet = NULL;
      ir->value = false;
      return k;
    }

  /* Got an actual non-terminal pulse */
  if (!ir->packet)
    ir->packet = new_irpacket ();
  p.value = ir->value;
  p.width = width;
  irpacket_pulse (ir->packet, p);
  ir->last_width = width;
  return NULL;
}

IRPacket *
irstate_timeout (IRState * ir)
{
  IRPacket *k = NULL;
  if (ir->packet)
    irpacket_complete (ir->packet);
  k = ir->packet;
  ir->packet = NULL;
  ir->timed_out = true;
  if (ir->buf_valid && ir->buf == 0xff)
    {
      /* Timed out with a single 0xff in the byte buffer. Suspicious.
       * This probably means we're out of sync and have actually
       * received a 0xff 0xff pair, but mistakenly interpreted the
       * second 0xff as the first byte of a word beginning with 0xff..
       * So let's drop that on the floor.
       */
      ir->buf_valid = false;
    }
  return k;
}

int
irstate_rxbytes (IRState * ir, int n_bytes, unsigned char *bytes,
                 IRPacket ** out_packets)
{
  int i;
  int n_out_packets = 0;
  for (i = 0; i < n_bytes; i++)
    {
      if (ir->buf_valid)
        {
          unsigned short width;
          IRPacket *k;
          width = ir->buf << 8; /* prior byte was high byte */
          width |= bytes[i];
          k = irstate_pulse (ir, width);
          ir->buf_valid = false;
          if (k)
            out_packets[n_out_packets++] = k;
        }
      else
        {
          ir->buf_valid = true;
          ir->buf = bytes[i];
        }
    }
  return n_out_packets;
}

void
irstate_open (IRState * ir, const char *dev)
{
  struct termios t_opt;
  struct stat st;
  int i;
  if (strchr (dev, '*'))
    {
      /* Wildcard in device name; try 0-9 in place of the '*'. */
      int i = 0;
      char buffer[BUFSIZ];
      char *cp;
      strcpy (buffer, dev);
      cp = strchr (buffer, '*');
      for (i = 0; i <= 9; i++)
        {
          *cp = '0' + i;
          /* Open TTY in blocking mode. We'll set it non-blocking later. */
          ir->fd = open (buffer, O_RDWR | O_NOCTTY);
          if (ir->fd != -1)
            break;
        }
    }
  else
    {
      /* Open TTY in blocking mode. We'll set it non-blocking later. */
      ir->fd = open (dev, O_RDWR | O_NOCTTY);
    }
  if (ir->fd == -1)
    fatal (0, "Cannot open device '%s'", dev);

  fstat (ir->fd, &st);
  if (S_ISCHR (st.st_mode))
    {
      char c, c2, c3;
      int open_tries = 5;
      /* Largely lifted from IRToyRecPlay.
       * IRToyRecPlay also sets the terminal speed, but I don't think
       * we should have to worry about that.
       */
      tcgetattr (ir->fd, &t_opt);
      cfsetispeed (&t_opt, 921600);
      cfsetospeed (&t_opt, 921600);
      t_opt.c_cflag |= (CLOCAL | CREAD);
      t_opt.c_cflag &= ~PARENB;
      t_opt.c_cflag &= ~CSTOPB;
      t_opt.c_cflag &= ~CSIZE;
      t_opt.c_cflag |= CS8;
      t_opt.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
      t_opt.c_iflag &= ~(IXON | IXOFF | IXANY);
      t_opt.c_iflag &= ~(ICRNL | INLCR);
      t_opt.c_oflag &= ~(OCRNL | ONLCR);
      t_opt.c_oflag &= ~OPOST;
      t_opt.c_cc[VMIN] = 0;
      t_opt.c_cc[VTIME] = 10;
      tcflush (ir->fd, TCIOFLUSH);
      tcsetattr (ir->fd, TCSANOW, &t_opt);

      fprintf (stdout, "Initialising IRToy");
      do
        {
          for (i = 0; i < 5; i++)
            {
              c = '\0';
              fprintf (stdout, ".");
              fflush (stdout);
              write (ir->fd, &c, 1);
            }
          sleep (1 + 5 - open_tries);
          fprintf (stdout, "\nSetting sampling mode\n");
          c = 'S';
          write (ir->fd, &c, 1);
          /* Read protocol version */
          if (read (ir->fd, &c, 1)
              && read (ir->fd, &c2, 1) && read (ir->fd, &c3, 1))
            {
              fprintf (stdout, "Protocol version '%c%c%c'\n", c, c2, c3);
              if (c == 'S' && isdigit (c2) && isdigit (c3))
                break;
              else
                {
                  if (--open_tries == 0)
                    fatal (0, "Couldn't set sampling mode");
                  fprintf (stdout, "Read bogus response, retrying...\n");
                }
            }
          else
            fatal (0, "Couldn't read protocol version\n");
        }
      while (open_tries);

    }
  fcntl (ir->fd, F_SETFL, fcntl (ir->fd, F_GETFL) | O_NONBLOCK);
}
