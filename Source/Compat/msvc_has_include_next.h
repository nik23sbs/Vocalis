// Solo compilatore Microsoft: gli header di compatibilita' di eSpeak-NG usano __has_include_next (estensione GCC/Clang).
// Su Windows gli header di sistema che cercano (unistd.h, endian.h, getopt.h) non esistono: la risposta e' sempre "no".
#pragma once
#if defined(_MSC_VER) && ! defined(__clang__) && ! defined(__has_include_next)
 #define __has_include_next(x) 0
#endif
