/* Settings mock: emulate durable NVS record across RAM-state resets. */
#pragma once
#include <sys/types.h>
typedef ssize_t (*settings_read_cb)(void *, void *, size_t);
#define SETTINGS_STATIC_HANDLER_DEFINE(...)
static unsigned fake_flash_writes;
static int fake_flash_error;
static bool fake_flash_has_value;
static uint8_t fake_flash_value;
static int settings_name_steq(const char *name,const char *key,const char **next) {
    if(strcmp(name,key)!=0)return 0;
    *next=NULL;return 1;
}
static int settings_save_one(const char *name,const void *data,size_t len) {
    assert(strcmp(name,"bbp9981/kb/brightness")==0&&len==1U);
    fake_flash_writes++;
    if(fake_flash_error)return fake_flash_error;
    fake_flash_value=*(const uint8_t *)data;fake_flash_has_value=true;return 0;
}
