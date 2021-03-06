/*
 * Copyright (c) 2014,2015 DeNA Co., Ltd., Kazuho Oku, Daisuke Maki
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include "h2o.h"
#include "h2o/mruby.h"
#include <mruby.h>
#include <mruby/proc.h>
#include <mruby/compile.h>
#include <mruby/string.h>

#include <errno.h>

#define MODULE_NAME "h2o_mruby"
#define MODULE_VERSION "0.0.1"
#define MODULE_DESCRIPTION MODULE_NAME "/" MODULE_VERSION

enum code_type {
    H2O_MRUBY_STRING,
    H2O_MRUBY_FILE
};

struct st_h2o_mruby_code_t {
    h2o_iovec_t *path;
    struct RProc *proc;
    mrbc_context *ctx;
    enum code_type type;
    unsigned int cache;
};

typedef struct st_h2o_mruby_code_t h2o_mruby_code_t;

struct st_h2o_mruby_context_t {
    h2o_mruby_handler_t *handler;
    mrb_state *mrb;

    /* TODO: add other hook code */
    h2o_mruby_code_t *h2o_mruby_handler_code;
};

typedef struct st_h2o_mruby_context_t h2o_mruby_context_t;

static void h2o_mruby_compile_code(mrb_state *mrb, h2o_iovec_t *path,
    h2o_mruby_code_t *code)
{
    struct mrb_parser_state* p;
    FILE *fp;

    char *rb_path = alloca(path->len + 1);
    memcpy(rb_path, path->base, path->len);
    rb_path[path->len] = '\0';

    if ((fp = fopen(rb_path, "r")) == NULL) {
        code->proc = NULL;
        fprintf(stderr, "%s: failed to open mruby script: %s(%s)\n",
            MODULE_NAME, path->base, strerror(errno));
        return;
    }

    code->path = path;
    code->ctx = mrbc_context_new(mrb);
    mrbc_filename(mrb, code->ctx, code->path->base);
    p = mrb_parse_file(mrb, fp, code->ctx);
    code->proc = mrb_generate_code(mrb, p);

    fclose(fp);
    mrb_pool_close(p->pool);
}

static void on_context_init(h2o_handler_t *_handler, h2o_context_t *ctx)
{
    h2o_mruby_handler_t *handler = (void *)_handler;
    h2o_mruby_context_t *handler_ctx = h2o_mem_alloc(sizeof(*handler_ctx));

    handler_ctx->handler = handler;

    /* ctx has a mrb_state per thread */
    handler_ctx->mrb = mrb_open();
    handler_ctx->h2o_mruby_handler_code = h2o_mem_alloc(sizeof(
          *handler_ctx->h2o_mruby_handler_code));

    h2o_mruby_compile_code(handler_ctx->mrb, &handler->config.mruby_handler_path,
        handler_ctx->h2o_mruby_handler_code);
    h2o_context_set_handler_context(ctx, &handler->super, handler_ctx);
}

static void on_context_dispose(h2o_handler_t *_handler, h2o_context_t *ctx)
{
    h2o_mruby_handler_t *handler = (void *)_handler;
    h2o_mruby_context_t *handler_ctx = h2o_context_get_handler_context(ctx,
        &handler->super);

    if (handler_ctx == NULL)
        return;

    mrb_close(handler_ctx->mrb);
    free(handler_ctx);
}

static void on_handler_dispose(h2o_handler_t *_handler)
{
    h2o_mruby_handler_t *handler = (void *)_handler;

    free(handler->config.mruby_handler_path.base);
    free(handler);
}

static int on_req(h2o_handler_t *_handler, h2o_req_t *req)
{
    h2o_mruby_handler_t *handler = (void *)_handler;
    h2o_mruby_context_t *handler_ctx = h2o_context_get_handler_context(
        req->conn->ctx, &handler->super);
    mrb_state *mrb = handler_ctx->mrb;
    mrb_value result;

    result = mrb_run(mrb, handler_ctx->h2o_mruby_handler_code->proc,
        mrb_top_self(mrb));

    if (mrb->exc) {
        struct RString *error = mrb_str_ptr(mrb_obj_value(mrb->exc));
        fprintf(stderr, "mruby raised: %s", error->as.heap.ptr);
        mrb->exc = 0;
        h2o_send_error(req, 500, "Internal Server Error",
            "Internal Server Error", 0);
    } else {
        h2o_send_error(req, 200, "h2o_mruby dayo", mrb_str_to_cstr(mrb, result),
            0);
    }

    return 0;
}

h2o_mruby_handler_t *h2o_mruby_register(h2o_pathconf_t *pathconf,
    h2o_mruby_config_vars_t *vars)
{
    h2o_mruby_handler_t *handler = (void *)h2o_create_handler(pathconf,
        sizeof(*handler));

    handler->super.on_context_init = on_context_init;
    handler->super.on_context_dispose = on_context_dispose;
    handler->super.dispose = on_handler_dispose;
    handler->super.on_req = on_req;
    handler->config = *vars;

    return handler;
}
