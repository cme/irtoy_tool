/* ------------------------------------------------------------
 *  Keyword decoding
 */

#ifndef __keywords_h
#define __keywords_h

/* Define an enum type with each keyword with 'k_' prefixed */
typedef enum
{
#define KEYWORD(n) k_##n,
#include "keywords.inc"
#undef KEYWORD
  n_strings
} Keyword;

extern Keyword
decode_keyword(const char *str);


#endif /* __keywords_h */
