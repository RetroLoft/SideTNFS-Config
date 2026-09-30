#ifndef DIALOG_H
#define DIALOG_H

#include "drive.h"

/* Device name in user-visible texts: the ACSI build (make acsi) talks to
 * an ACSI2TNFS adapter instead of the SideTNFS cartridge. */
#ifdef TRANSPORT_ACSI
#define DEVICE_NAME    "ACSI2TNFS"
#define FIRMWARE_URL   "https://github.com/RetroLoft/ACSI2TNFS/"
#else
#define DEVICE_NAME    "SideTNFS"
#define FIRMWARE_URL   "https://github.com/RetroLoft/SideTNFS-Firmware/"
#endif

void dialog_run(DriveConfig *cfg);

#endif
