/* jconfigint.h for Diffractor's libjpeg-turbo build.
 *
 * Diffractor keeps all libjpeg-turbo configuration in jconfig.h (which every
 * internal source includes via jpeglib.h / jinclude.h), so this internal
 * config header intentionally only forwards to it to satisfy the sources that
 * include "jconfigint.h" directly. */
#include "jconfig.h"
