#ifndef VTT_STORE_H
#define VTT_STORE_H

/* The folders of saved things under the data directory -- maps, stamps,
 * characters, handouts -- and the one rule for what a saved thing may be
 * called. Each is a flat folder of NAME.ext files. */

#include <stddef.h>
#include "map.h"

/* $XDG_DATA_HOME/vtt/SUB, else ~/.local/share/vtt/SUB. */
void store_dir(const char *sub, char *buf, size_t sz);

/* Letters, digits, - and _, under MAP_NAME_MAX: what can be a file name
 * anywhere, and never a path. */
int  store_name_ok(const char *name);

/* SUB's folder/NAME.EXT; 0 when it does not fit. The name is not checked. */
int  store_path(const char *sub, const char *name, const char *ext, char *buf, size_t sz);

/* The NAMEs in dir with the extension ext (".vtt"), sorted, names that
 * fail store_name_ok left out; the first max of them into names, and the
 * count of all. names may be NULL with max 0, to count. */
int  store_list(const char *dir, const char *ext, char (*names)[MAP_NAME_MAX], int max);

/* The same, all of them, in an array the caller frees; NULL and *n 0 when
 * there are none. */
char (*store_list_all(const char *dir, const char *ext, int *n))[MAP_NAME_MAX];

#endif /* VTT_STORE_H */
