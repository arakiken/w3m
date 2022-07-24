#ifndef JS_HTML_H
#define JS_HTML_H

#include <quickjs/quickjs.h>

extern JSClassID WindowClassID;
extern JSClassID LocationClassID;
extern JSClassID DocumentClassID;
extern JSClassID NavigatorClassID;
extern JSClassID HistoryClassID;
extern JSClassID HTMLFormElementClassID;
extern JSClassID HTMLElementClassID;

extern char *alert_msg;

extern int term_ppc;
extern int term_ppl;

typedef struct _OpenWindow {
    char *url;
    char *name;
} OpenWindow;

typedef struct _WindowState {
    GeneralList *win; /* OpenWindow */
    int close;
} WindowState;

typedef struct _DocumentState {
    Str write;
    Str cookie;
    int cookie_changed;
    int open;
} DocumentState;

typedef struct _HistoryState {
    int pos;
} HistoryState;

typedef struct _LocationState {
    Str url;
    ParsedURL pu;
    int refresh;
#define JS_LOC_REFRESH	1
#define JS_LOC_HASH	2
} LocationState;

typedef struct _HTMLFormElementState {
    int submit;
    int reset;
} HTMLFormElementState;

typedef struct _XMLHttpRequestState {
    int method;
    char *request;
    TextList *extra_headers;
    int override_content_type;
    int override_user_agent;
    TextList *response_headers;
} XMLHttpRequestState;

#define i2us(s) wc_Str_conv(s, InnerCharset, WC_CES_UTF_8)
#define i2uc(s) wc_Str_conv(Strnew_charp(s), InnerCharset, WC_CES_UTF_8)->ptr
#define u2is(s) wc_Str_conv(s, WC_CES_UTF_8, InnerCharset)
#define u2ic(s) wc_Str_conv(Strnew_charp(s), WC_CES_UTF_8, InnerCharset)->ptr

#define js_is_undefined(val) JS_IsUndefined(val)
#define js_is_exception(val) JS_IsException(val)
#define js_is_object(val) JS_IsObject(val)
#define js_set_state(obj, op) JS_SetOpaque(obj, op)
#define js_get_state(obj, id) JS_GetOpaque(obj, id)
#define js_free(ctx, val) JS_FreeValue(ctx, val)

extern JSContext *js_html_init(Buffer *buf);
extern void js_html_final(JSContext *ctx);

extern void js_eval(JSContext *ctx, char *script);
extern JSValue js_eval2(JSContext *ctx, char *script);
extern JSValue js_eval2_this(JSContext *ctx, int formidx, char *script);

extern char *js_get_cstr(JSContext *ctx, JSValue value);
extern Str js_get_str(JSContext *ctx, JSValue value);
extern void js_reset_functions(JSContext *ctx);
extern Str js_get_function(JSContext *ctx, char *script);
extern int js_get_int(JSContext *ctx, int *i, JSValue value);
extern int js_is_true(JSContext *ctx, JSValue value);
extern void js_add_event_listener(JSContext *ctx, JSValue jsThis, char *type, char *func);
#ifdef USE_LIBXML2
int js_create_dom_tree(JSContext *ctx, char *filename, const char *charset);
#else
#define js_create_dom_tree(a, b, c) (0)
#endif

#endif
