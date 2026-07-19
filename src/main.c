#include <gem.h>
#include "dialog.h"
#include "drive.h"

static DriveConfig cfg;

int main(void)
{
    appl_init();
    graf_mouse(ARROW, (MFORM *)0);

    /* In-memory starting point only (no file I/O) -- dialog_run()
     * overrides this from the cartridge firmware when present. */
    drive_config_init_defaults(&cfg);
    dialog_run(&cfg);

    appl_exit();
    return 0;
}
