#ifndef CS16_BROWSER_NATIVE_H
#define CS16_BROWSER_NATIVE_H
#include <stdint.h>
void *cs16_browser_native_create(void);
void cs16_browser_native_destroy(void *);
int cs16_browser_native_resize(void *,int,int);
int cs16_browser_native_navigate(void *,const char *);
int cs16_browser_native_action(void *,int); /* 0 stop, 1 reload, 2 back, 3 forward */
int cs16_browser_native_busy(void *);
unsigned cs16_browser_native_events(void *); /* 1 finish, 2 error */
int cs16_browser_native_snapshot(void *,uint8_t *,int,int);
int cs16_browser_native_script(void *,const char *,char *,unsigned);
void cs16_browser_native_pump(void);
#endif
