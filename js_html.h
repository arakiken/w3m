/* -*- tab-width:8; c-basic-offset:4 -*- */
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

typedef struct window_ctx_st {
  GeneralList *win;
  int close;
} WindowCtx;

typedef struct document_ctx_st {
  Str write;
} DocumentCtx;

typedef struct navigator_ctx_st {
  Str appcodename;
  Str appname;
  Str appversion;
  Str useragent;
} NavigatorCtx;

typedef struct history_ctx_st {
  int pos;
} HistoryCtx;

typedef struct location_ctx_st {
  Str url;
  ParsedURL pu;
  int refresh;
#define JS_LOC_REFRESH	1
#define JS_LOC_HASH	2
} LocationCtx;

extern JSContext *js_html_init(void);

#endif
