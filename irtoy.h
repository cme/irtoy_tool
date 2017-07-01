/* IRToy and infrared definitions */
#ifndef __irtoy_h
#define __irtoy_h

#include <stdio.h>
#include <stdbool.h>

#include "dict.h"

typedef struct IRState IRState;
typedef struct IRPulse IRPulse;
typedef struct IRPacket IRPacket;
typedef struct IRSymbol IRSymbol;
typedef struct IRDict IRDict;

/* ------------------------------------------------------------
 * IR Pulses and Packets
 */

struct IRPacket
{
  IRPulse *pulses;
  int n_pulses;
  int n_pulses_allocated;
};

struct IRPulse
{
  bool value;
  unsigned short width;
};

extern IRPacket *new_irpacket (void);
extern void free_irpacket (IRPacket * k);
extern bool irpacket_complete (IRPacket * k);
extern void irpacket_pulse (IRPacket * k, IRPulse pulse);
extern void irpacket_printf (FILE * out, IRPacket * k);
extern void irpacket_render (FILE * out, IRPacket * k);
extern IRPacket *irpacket_scanf (FILE * in);
extern bool irpacket_match (IRPacket * a, IRPacket * b, int jitter);

extern IRDict *new_irdict (void);
extern IRPacket *irdict_lookup_name (IRDict * d, const char *name);
extern const char *irdict_lookup_packet (IRDict * d, IRPacket * k);
extern void irdict_insert (IRDict * d, const char *name, IRPacket * k);
extern IRState *new_irstate (void);
extern IRPacket *irstate_pulse (IRState * ir, unsigned short width);
extern IRPacket *irstate_timeout (IRState * ir);
extern int irstate_rxbytes (IRState * ir, int n_bytes, unsigned char *bytes,
                            IRPacket ** out_packets);
extern void irstate_open (IRState * ir, const char *dev);


extern int irtoy_gap;           /* min gap between packets */
extern int irtoy_jitter;        /* acceptable jitter */

struct IRState
{
  bool value;                   /* current 0/1 state */
  IRPacket *packet;             /* packet under construction */
  int last_width;
  int fd;
  unsigned char buf;
  bool buf_valid;
  bool timed_out;
};

struct IRSymbol
{
  const char *name;
  IRPacket *packet;
  IRSymbol *next;
};

struct IRDict
{
  IRSymbol *first;
  Dict *by_name;
};


#endif  /* __irtoy_h */
