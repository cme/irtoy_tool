/* ------------------------------------------------------------
 *  Keyword decoding
 */

#include "keywords.h"
#include "dict.h"

Keyword
decode_keyword(const char *str)
{
  static DictDecode decode_keywords[] = {
#define KEYWORD(n) { #n, k_##n },
#include "keywords.inc"
#undef KEYWORDS
    {NULL, 0}
  };
  static Dict *keyword_dict = NULL;
  return dict_decode (&keyword_dict, decode_keywords, str);
}

