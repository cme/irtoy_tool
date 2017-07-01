/* ------------------------------------------------------------
 * Mac OS system events
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>

#include "mac_actions.h"
#include "error.h"
#include "dict.h"

#define MAC_KEYS                                \
  KEY(0, 0x00, ANSI_A)                          \
  KEY(1, 0x01, ANSI_S)                          \
  KEY(2, 0x02, ANSI_D)                          \
  KEY(3, 0x03, ANSI_F)                          \
  KEY(4, 0x04, ANSI_H)                          \
  KEY(5, 0x05, ANSI_G)                          \
  KEY(6, 0x06, ANSI_Z)                          \
  KEY(7, 0x07, ANSI_X)                          \
  KEY(8, 0x08, ANSI_C)                          \
  KEY(9, 0x09, ANSI_V)                          \
  KEY(10, 0x0A, ISO_Section)                    \
  KEY(11, 0x0B, ANSI_B)                         \
  KEY(12, 0x0C, ANSI_Q)                         \
  KEY(13, 0x0D, ANSI_W)                         \
  KEY(14, 0x0E, ANSI_E)                         \
  KEY(15, 0x0F, ANSI_R)                         \
  KEY(16, 0x10, ANSI_Y)                         \
  KEY(17, 0x11, ANSI_T)                         \
  KEY(18, 0x12, ANSI_1)                         \
  KEY(19, 0x13, ANSI_2)                         \
  KEY(20, 0x14, ANSI_3)                         \
  KEY(21, 0x15, ANSI_4)                         \
  KEY(22, 0x16, ANSI_6)                         \
  KEY(23, 0x17, ANSI_5)                         \
  KEY(24, 0x18, ANSI_Equal)                     \
  KEY(25, 0x19, ANSI_9)                         \
  KEY(26, 0x1A, ANSI_7)                         \
  KEY(27, 0x1B, ANSI_Minus)                     \
  KEY(28, 0x1C, ANSI_8)                         \
  KEY(29, 0x1D, ANSI_0)                         \
  KEY(30, 0x1E, ANSI_RightBracket)              \
  KEY(31, 0x1F, ANSI_O)                         \
  KEY(32, 0x20, ANSI_U)                         \
  KEY(33, 0x21, ANSI_LeftBracket)               \
  KEY(34, 0x22, ANSI_I)                         \
  KEY(35, 0x23, ANSI_P)                         \
  KEY(36, 0x24, Return)                         \
  KEY(37, 0x25, ANSI_L)                         \
  KEY(38, 0x26, ANSI_J)                         \
  KEY(39, 0x27, ANSI_Quote)                     \
  KEY(40, 0x28, ANSI_K)                         \
  KEY(41, 0x29, ANSI_Semicolon)                 \
  KEY(42, 0x2A, ANSI_Backslash)                 \
  KEY(43, 0x2B, ANSI_Comma)                     \
  KEY(44, 0x2C, ANSI_Slash)                     \
  KEY(45, 0x2D, ANSI_N)                         \
  KEY(46, 0x2E, ANSI_M)                         \
  KEY(47, 0x2F, ANSI_Period)                    \
  KEY(48, 0x30, Tab)                            \
  KEY(49, 0x31, Space)                          \
  KEY(50, 0x32, ANSI_Grave)                     \
  KEY(51, 0x33, Delete)                         \
  KEY(53, 0x35, Escape)                         \
  KEY(55, 0x37, Command)                        \
  KEY(56, 0x38, Shift)                          \
  KEY(57, 0x39, CapsLock)                       \
  KEY(58, 0x3A, Option)                         \
  KEY(59, 0x3B, Control)                        \
  KEY(60, 0x3C, RightShift)                     \
  KEY(61, 0x3D, RightOption)                    \
  KEY(62, 0x3E, RightControl)                   \
  KEY(63, 0x3F, Function)                       \
  KEY(64, 0x40, F17)                            \
  KEY(65, 0x41, ANSI_KeypadDecimal)             \
  KEY(67, 0x43, ANSI_KeypadMultiply)            \
  KEY(69, 0x45, ANSI_KeypadPlus)                \
  KEY(71, 0x47, ANSI_KeypadClear)               \
  KEY(72, 0x48, VolumeUp)                       \
  KEY(73, 0x49, VolumeDown)                     \
  KEY(74, 0x4A, Mute)                           \
  KEY(75, 0x4B, ANSI_KeypadDivide)              \
  KEY(76, 0x4C, ANSI_KeypadEnter)               \
  KEY(78, 0x4E, ANSI_KeypadMinus)               \
  KEY(79, 0x4F, F18)                            \
  KEY(80, 0x50, F19)                            \
  KEY(81, 0x51, ANSI_KeypadEquals)              \
  KEY(82, 0x52, ANSI_Keypad0)                   \
  KEY(83, 0x53, ANSI_Keypad1)                   \
  KEY(84, 0x54, ANSI_Keypad2)                   \
  KEY(85, 0x55, ANSI_Keypad3)                   \
  KEY(86, 0x56, ANSI_Keypad4)                   \
  KEY(87, 0x57, ANSI_Keypad5)                   \
  KEY(88, 0x58, ANSI_Keypad6)                   \
  KEY(89, 0x59, ANSI_Keypad7)                   \
  KEY(90, 0x5A, F20)                            \
  KEY(91, 0x5B, ANSI_Keypad8)                   \
  KEY(92, 0x5C, ANSI_Keypad9)                   \
  KEY(93, 0x5D, JIS_Yen)                        \
  KEY(94, 0x5E, JIS_Underscore)                 \
  KEY(95, 0x5F, JIS_KeypadComma)                \
  KEY(96, 0x60, F5)                             \
  KEY(97, 0x61, F6)                             \
  KEY(98, 0x62, F7)                             \
  KEY(99, 0x63, F3)                             \
  KEY(100, 0x64, F8)                            \
  KEY(101, 0x65, F9)                            \
  KEY(102, 0x66, JIS_Eisu)                      \
  KEY(103, 0x67, F11)                           \
  KEY(104, 0x68, JIS_Kana)                      \
  KEY(105, 0x69, F13)                           \
  KEY(106, 0x6A, F16)                           \
  KEY(107, 0x6B, F14)                           \
  KEY(109, 0x6D, F10)                           \
  KEY(111, 0x6F, F12)                           \
  KEY(113, 0x71, F15)                           \
  KEY(114, 0x72, Help)                          \
  KEY(115, 0x73, Home)                          \
  KEY(116, 0x74, PageUp)                        \
  KEY(117, 0x75, ForwardDelete)                 \
  KEY(118, 0x76, F4)                            \
  KEY(119, 0x77, End)                           \
  KEY(120, 0x78, F2)                            \
  KEY(121, 0x79, PageDown)                      \
  KEY(122, 0x7A, F1)                            \
  KEY(123, 0x7B, LeftArrow)                     \
  KEY(124, 0x7C, RightArrow)                    \
  KEY(125, 0x7D, DownArrow)                     \
  KEY(126, 0x7E, UpArrow)
  
static int decode_key_name (const char *name)
{
  static DictDecode decode_keys[] = {
#define KEY(n, h, s) { #s, n },
    MAC_KEYS
#undef KEY
    { NULL, 0 }
  };
  static Dict *d = NULL;
  return dict_decode (&d, decode_keys, name);
}

void osascript(const char *cmd)
{
  int cpid = fork();
  if (cpid == -1) {
    fatal(0, "Couldn't fork");
  } else if (cpid != 0) {
    int status;
    /* Wait for child, ignore return code */
    waitpid(cpid, &status, 0);
  } else {
    fprintf (stdout, "osascript: '%s'\n", cmd);
    execlp("osascript", "osascript", "-e", cmd, NULL);
    fprintf(stderr, "Couldn't exec osascript\n");
    exit(2);
  }
}


bool mac_key(const char *name)
{
  bool shift = false, control = false, command = false;
  char str_shift[] = "shift+",
    str_control[] = "control+",
    str_command[] = "command+";
  char *buffer;
  int code;
  char verb_buffer[BUFSIZ];

  /* Modifiers */
  while (*name)
    {
      if (!strncasecmp(name, str_shift, sizeof(str_shift)-1))
        {
          shift = true;
          name += sizeof (str_shift) -1;
        }
      else if (!strncmp(name, str_control, sizeof(str_control)-1))
        {
          control = true;
          name += sizeof (str_control) -1;
        }
      else if (!strncmp(name, str_command, sizeof(str_command)-1))
        {
          command = true;
          name += sizeof (str_command) -1;
        }
      else
        break;
    }

  code = decode_key_name (name);
  if (code == -1)
    /* Use keystroke string. */
    sprintf (verb_buffer, "keystroke \"%s\"", name);
  else
    sprintf (verb_buffer, "key code \"%d\"", code);

  if (shift || control || command)
    {
      /* Use the modifier format */
      const char fmt[] = "tell application \"System Events\" to"
        " %s using { %s%s%s%s%s }";
      buffer = malloc (sizeof (fmt) + 60);
      sprintf (buffer, fmt, verb_buffer,
               shift?"shift down":"",
               (shift && (control||command))?", ":"",
               control?"control down":"",
               (control && command)?", ":"",
               command?"command down":"");
      osascript (buffer);
      free (buffer);
    }
  else
    {
      const char fmt[] = "tell application \"System Events\" to %s";
      buffer = malloc(11 + sizeof fmt);
      sprintf(buffer, fmt, verb_buffer);
      osascript(buffer);
      free(buffer);
    }
  return true;
}
