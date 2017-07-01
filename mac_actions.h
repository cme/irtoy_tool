/* Mac OS (AppleScript) actions */
#ifndef __mac_actions_h
#define __mac_actions_h

#include <stdio.h>
#include <stdbool.h>

extern void osascript(const char *cmd);

/* Mac key.
 * Parameter is a string of the form:
 * <modifiers><name>
 * <modifiers> ::= ( "shift+" | "command+" | "control+" ) *
 * name ::= key name | string
 */
extern bool mac_key(const char *name);

#endif  /* __mac_actions_h */
