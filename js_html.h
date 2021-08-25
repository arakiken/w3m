/* -*- tab-width:8; c-basic-offset:4 -*- */
#ifndef JS_HTML_H
#define JS_HTML_H

#ifdef HAVE_NJS

/* for NJS 0.3.0 or later. */
#include <njs/njs.h>

#define JSType			NJSValue
#define JSInterpPtr		NJSInterpPtr
#define JSClassPtr		NJSClassPtr
#define JSFreeProc		NJSFreeProc
#define JSMethodResult		NJSMethodResult
#define	JSInterpOptions		NJSInterpOptions

#define JS_ERROR		NJS_ERROR
#define JS_OK			NJS_OK

#define JS_TYPE_STRING		NJS_VALUE_STRING
#define JS_TYPE_BOOLEAN		NJS_VALUE_BOOLEAN
#define JS_TYPE_DOUBLE		NJS_VALUE_DOUBLE
#define JS_TYPE_INTEGER		NJS_VALUE_INTEGER

#define JS_CF_STATIC		NJS_CF_STATIC
#define JS_CF_IMMUTABLE		NJS_CF_IMMUTABLE

#define js_eval			njs_eval
#define js_result		njs_result
#define js_create_interp	njs_create_interp
#define js_destroy_interp	njs_destroy_interp
#define js_class_context	njs_class_context
#define js_class_create		njs_class_create
#define js_define_class		njs_define_class
#define js_lookup_class		njs_lookup_class
#define js_class_define_method	njs_class_define_method
#define js_class_define_property njs_class_define_property
#define js_type_make_string	njs_type_make_string
#define js_type_make_array	njs_type_make_array
#define js_isa			njs_isa
#define js_init_default_options	njs_init_default_options

#else /* HAVE_NJS */

/* for NJS 0.2.5. */
#include <js.h>

#endif

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

extern JSInterpPtr js_html_init(void);

#endif
