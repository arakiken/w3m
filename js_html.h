#ifndef JS_HTML_H
#define JS_HTML_H

#include <quickjs/quickjs.h>

extern JSClassID WindowClassID;
extern JSClassID LocationClassID;
extern JSClassID DocumentClassID;
extern JSClassID NavigatorClassID;
extern JSClassID HistoryClassID;
extern JSClassID AnchorClassID;
extern JSClassID FormItemClassID;
extern JSClassID FormClassID;
extern JSClassID ImageClassID;
extern JSClassID CookieClassID;

typedef struct jse_windowopen {
    char *url;
    char *name;
} jse_windowopen_t;

typedef struct _WindowState {
    GeneralList *win;
    int close;
} WindowState;

typedef struct _DocumentState {
    Str write;
} DocumentState;

typedef struct _NavigatorState {
    Str appcodename;
    Str appname;
    Str appversion;
    Str useragent;
} NavigatorState;

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

typedef struct _FormState {
    int submit;
    int reset;
} FormState;

#define js_is_object(val) JS_IsObject(val)
#define js_get_state(obj, id) JS_GetOpaque(obj, id)
#define js_html_final(ctx) JS_FreeContext(ctx)

extern JSContext *js_html_init(void);

extern JSValue js_eval(JSContext *interp, char *str);

extern char *js_get_cstr(JSContext *ctx, JSValue value);
extern Str js_get_str(JSContext *ctx, JSValue value);

#endif
