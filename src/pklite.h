#ifndef PKLITE_H
#define PKLITE_H
#include <stddef.h>
/* Returns malloc'd decompressed load module (without relocations), or NULL. */
unsigned char *pklite_unpack(const unsigned char *exe, size_t exelen, size_t *outlen);
#endif
