#ifndef DIALOG_H
#define DIALOG_H

#include "drive.h"

/* Shown in the main window title so a SETTINGS-disk PRG can be told
 * apart from an older UF2. Not the Pico RELEASE_VERSION. */
#define SIDETNFS_PRG_VERSION "f37-a1"

void dialog_run(DriveConfig *cfg);

#endif
