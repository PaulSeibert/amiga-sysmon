#ifndef UI_H
#define UI_H
#include <exec/types.h>

BOOL  ui_open(void);
void  ui_close(void);
ULONG ui_sigmask(void);
BOOL  ui_handle(void);       /* drain the window port; FALSE = quit */
void  ui_tick(void);         /* timer: re-sample and redraw */
ULONG ui_refresh_secs(void);

#endif
