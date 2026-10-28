#ifndef POCKET_LEVEL_LOADING_H
#define POCKET_LEVEL_LOADING_H

#include <stddef.h>

/* Present real discovery progress before/between blocking metadata reads. */
void POCKET_ShowLevelLoading(size_t found);

#endif
