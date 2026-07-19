#include <gem.h>
#include "dialog.h"
#include "config.h"

static DriveConfig cfg;

int main(void)
{
    appl_init();
    graf_mouse(ARROW, (MFORM *)0);

    config_load(&cfg, CFG_FILENAME);
    dialog_run(&cfg);

    appl_exit();
    return 0;
}
