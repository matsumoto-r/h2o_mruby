#ifndef H20_MRUBY_H                                                              
#define H20_MRUBY_H                                                              

#include "h2o.h"
#include <mruby.h>

#define MODULE_NAME "h2o_mruby"
#define MODULE_VERSION "0.0.1"
#define MODULE_DESCRIPTION MODULE_NAME "/" MODULE_VERSION

struct st_h2o_mruby_config_vars_t {
    h2o_iovec_t mruby_handler_path;
};

typedef struct st_h2o_mruby_config_vars_t h2o_mruby_config_vars_t;

struct st_h2o_mruby_handler_t {
    h2o_handler_t super;
    h2o_mruby_config_vars_t config;
};

typedef struct st_h2o_mruby_handler_t h2o_mruby_handler_t;

/* handler/mruby.c */
h2o_mruby_handler_t *h2o_mruby_register(h2o_pathconf_t *pathconf, h2o_mruby_config_vars_t *vars);



#endif
