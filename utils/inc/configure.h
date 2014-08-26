#ifndef __CONFIGURE_H__
#define __CONFIGURE_H__

/* utils::Configure* */
typedef void configure_t;

configure_t *utils_new_configure();

void utils_delete_configure(configure_t *c);

int uitls_configure_load(const char *file);

int utils_configure_get_single_str(configure_t *c, const char *section, const char *key, char *val, size_t len);

int utils_configure_get_single_long(configure_t *c, const char *section, const char *key, long *val);

#endif

