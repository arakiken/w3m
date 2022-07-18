#include "fm.h"
#ifdef USE_JAVASCRIPT
#include "js_html.h"
#include <sys/utsname.h>

#define NUM_TAGNAMES 5
#define TN_FORM 0
#define TN_IMG 1
#define TN_SCRIPT 2
#define TN_SELECT 3
#define TN_CANVAS 4

typedef struct _CtxState {
    JSValue tagnames[NUM_TAGNAMES];
    JSValue *funcs;
    int nfuncs;
    Buffer *buf;
} CtxState;

#if 1
#define EVAL_FLAG 0
#else
#define EVAL_FLAG JS_EVAL_FLAG_STRICT
#endif

JSClassID WindowClassID = 1; /* JS_CLASS_OBJECT */
JSClassID LocationClassID;
JSClassID DocumentClassID;
JSClassID NavigatorClassID;
JSClassID HistoryClassID;
JSClassID HTMLFormElementClassID;
JSClassID HTMLElementClassID;
static JSClassID HTMLImageElementClassID;
static JSClassID HTMLSelectElementClassID;
static JSClassID HTMLScriptElementClassID;
static JSClassID HTMLCanvasElementClassID;
static JSClassID ElementClassID;
static JSClassID SVGElementClassID;
static JSClassID XMLHttpRequestClassID;
static JSClassID CanvasRenderingContext2DClassID;

char *alert_msg;

static JSRuntime *rt;

int term_ppc;
int term_ppl;

#ifdef USE_LIBXML2
#if 0
#define DOM_DEBUG
#endif
#include <libxml/HTMLparser.h>
#include <libxml/HTMLtree.h>
static void create_tree(JSContext *ctx, xmlNode *node, JSValue jsparent, int innerhtml);
#endif

static void log_to_file(JSContext *ctx, const char *head, int argc, JSValueConst *argv);

static void
log_msg(const char *msg)
{
    FILE *fp;

    if ((fp = fopen("scriptlog.txt", "a")) != NULL) {
	fwrite(msg, strlen(msg), 1, fp);
	fwrite("\n", 1, 1, fp);
	fclose(fp);
    }
}

#ifdef SCRIPT_DEBUG
static JSValue
backtrace(JSContext *ctx, const char *script, JSValue eval_ret)
{
#if 0
    FILE *fp = fopen(Sprintf("w3mlog%p.txt", ctx)->ptr, "a");
    fprintf(fp, "%s\n", script);
    fclose(fp);
#endif

    if (JS_IsException(eval_ret) &&
	/* see script_buf2js() in script.c */
	strcmp(script, "document;") != 0) {
	JSValue err = JS_GetException(ctx);
	const char *err_str = JS_ToCString(ctx, err);

	JSValue stack = JS_GetPropertyStr(ctx, err, "stack");
	const char *stack_str = JS_ToCString(ctx, stack);

	FILE *fp = fopen("scriptlog.txt", "a");
	fprintf(fp, "<%s>\n%s\n%s\n", err_str, stack_str, script);
	fclose(fp);

	JS_FreeCString(ctx, err_str);
	JS_FreeCString(ctx, stack_str);
	JS_FreeValue(ctx, err);
	JS_FreeValue(ctx, stack);
    }

    return eval_ret;
}
#else
#define backtrace(ctx, script, eval_ret) (eval_ret)
#endif

static char *
get_cstr(JSContext *ctx, JSValue value)
{
    const char *str;
    size_t len;
    char *new_str;

    if (!JS_IsString(value) || (str = JS_ToCStringLen(ctx, &len, value)) == NULL) {
	return NULL;
    }

    new_str = allocStr(str, len);
    JS_FreeCString(ctx, str);

    return new_str;
}

static Str
get_str(JSContext *ctx, JSValue value)
{
    const char *str;
    size_t len;
    Str new_str;

    if (!JS_IsString(value) || (str = JS_ToCStringLen(ctx, &len, value)) == NULL) {
	return NULL;
    }

    new_str = Strnew_charp_n(str, len);
    JS_FreeCString(ctx, str);

    return new_str;
}

static JSValue
add_event_listener(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
#ifdef SCRIPT_DEBUG
    const char *str;
#endif
    JSValue events;
    JSValue prop;
    JSValue event;

    if (argc < 2) {
	return JS_EXCEPTION;
    }

#if 0
    FILE *fp = fopen("w3mlog.txt", "a");
    fprintf(fp, "%s\n", JS_ToCString(ctx, argv[1]));
    fclose(fp);
#endif

    events = JS_GetPropertyStr(ctx, jsThis, "w3m_events");
    if (!JS_IsArray(ctx, events)) {
	JS_FreeValue(ctx, events);
	events = JS_NewArray(ctx);
	JS_SetPropertyStr(ctx, jsThis, "w3m_events", JS_DupValue(ctx, events));
    }
    prop = JS_GetPropertyStr(ctx, events, "push");

    event = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, event, "type", JS_DupValue(ctx, argv[0]));
    JS_SetPropertyStr(ctx, event, "currentTarget", JS_DupValue(ctx, jsThis));
    JS_SetPropertyStr(ctx, event, "target", JS_DupValue(ctx, jsThis));
    JS_SetPropertyStr(ctx, event, "listener", JS_DupValue(ctx, argv[1]));

    JS_FreeValue(ctx, JS_Call(ctx, prop, events, 1, &event));

    JS_FreeValue(ctx, event);
    JS_FreeValue(ctx, prop);
    JS_FreeValue(ctx, events);

#ifdef SCRIPT_DEBUG
    str = JS_ToCString(ctx, argv[0]);
#if 1
    if (strcmp(str, "loadstart") != 0 && strcmp(str, "load") != 0 && strcmp(str, "loadend") != 0 &&
	strcmp(str, "DOMContentLoaded") != 0 &&
	strcmp(str, "visibilitychange") != 0 && strcmp(str, "submit") != 0 &&
	strcmp(str, "click") != 0 && strcmp(str, "keypress") != 0 &&
	strcmp(str, "keydown") != 0 && strcmp(str, "keyup") != 0 &&
	strcmp(str, "input") != 0 && strcmp(str, "focus") != 0 &&
	strcmp(str, "message") != 0)
#endif
    {
	FILE *fp = fopen("scriptlog.txt", "a");
	JSValue tag = JS_GetPropertyStr(ctx, jsThis, "nodeName");
	const char *str2 = JS_ToCString(ctx, tag);
	fprintf(fp, "<Unknown event (addEventListener)> %s (tag %s) \n", str, str2);
	JS_FreeCString(ctx, str2);
	JS_FreeValue(ctx, tag);
	fclose(fp);
    }
    JS_FreeCString(ctx, str);
#endif

    return JS_UNDEFINED;
}

static JSValue
remove_event_listener(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    const char *arg_type;
    JSValue arg_handle_event;
    JSValue events;
    int i;

    if (argc < 2) {
	return JS_EXCEPTION;
    }

    events = JS_GetPropertyStr(ctx, jsThis, "w3m_events");
    if (!JS_IsArray(ctx, events)) {
	JS_FreeValue(ctx, events);
	return JS_UNDEFINED;
    }

    arg_type = JS_ToCString(ctx, argv[0]);
    arg_handle_event = JS_GetPropertyStr(ctx, argv[1], "handleEvent");
    for (i = 0; ; i++) {
	JSValue event = JS_GetPropertyUint32(ctx, events, i);
	JSValue listener;
	JSValue type;
	const char *str;

	if (!JS_IsObject(event)) {
	    JS_FreeValue(ctx, event);
	    break;
	}

	listener = JS_GetPropertyStr(ctx, event, "listener");
	type = JS_GetPropertyStr(ctx, event, "type");
	JS_FreeValue(ctx, event);
	str = JS_ToCString(ctx, type);
	if ((JS_VALUE_GET_PTR(listener) == JS_VALUE_GET_PTR(argv[1]) ||
	     JS_VALUE_GET_PTR(listener) == JS_VALUE_GET_PTR(arg_handle_event)) &&
	    strcmp(str, arg_type) == 0) {
	    JSValue argv2[2];
	    JSValue prop = JS_GetPropertyStr(ctx, events, "splice");
	    argv2[0] = JS_NewInt32(ctx, i);
	    argv2[1] = JS_NewInt32(ctx, 1);
	    JS_FreeValue(ctx, JS_Call(ctx, prop, events, 2, argv2));
	    JS_FreeValue(ctx, argv2[0]);
	    JS_FreeValue(ctx, argv2[1]);
	    JS_FreeValue(ctx, prop);

	    JS_FreeValue(ctx, listener);
	    JS_FreeCString(ctx, str);
	    JS_FreeValue(ctx, type);

	    break;
	}
	JS_FreeValue(ctx, listener);
	JS_FreeCString(ctx, str);
	JS_FreeValue(ctx, type);
    }

    JS_FreeValue(ctx, arg_handle_event);
    JS_FreeCString(ctx, arg_type);
    JS_FreeValue(ctx, events);

    return JS_UNDEFINED;
}

static JSValue
dispatch_event(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue type;
    const char *str;

    if (argc < 1) {
	return JS_EXCEPTION;
    }

    type = JS_GetPropertyStr(ctx, argv[0], "type");
    str = JS_ToCString(ctx, type);
    if (str != NULL) {
	JSValue events = JS_GetPropertyStr(ctx, jsThis, "w3m_events");

	if (JS_IsArray(ctx, events)) {
	    int i;
	    for (i = 0; ; i++) {
		JSValue event = JS_GetPropertyUint32(ctx, events, i);
		JSValue type2;
		const char *str2;

		if (!JS_IsObject(event)) {
		    break;
		}

		type2 = JS_GetPropertyStr(ctx, event, "type");
		str2 = JS_ToCString(ctx, type2);

		if (str2 != NULL) {
		    if (strcmp(str, str2) == 0) {
			JSValue listener = JS_GetPropertyStr(ctx, event, "listener");
			if (JS_IsFunction(ctx, listener)) {
			    JS_FreeValue(ctx, JS_Call(ctx, listener, event, 1, argv));
			} else if (JS_IsObject(listener)) {
			    JSValue handle = JS_GetPropertyStr(ctx, listener, "handleEvent");
			    if (JS_IsFunction(ctx, handle)) {
				JS_FreeValue(ctx, JS_Call(ctx, handle, listener, 1, argv));
			    }
			    JS_FreeValue(ctx, handle);
			}
			JS_FreeValue(ctx, listener);
		    }
		    JS_FreeCString(ctx, str2);
		}
		JS_FreeValue(ctx, type2);
		JS_FreeValue(ctx, event);
	    }
	}
	JS_FreeValue(ctx, events);
	JS_FreeCString(ctx, str);
    }
    JS_FreeValue(ctx, type);

    return JS_TRUE;
}

static JSValue
window_open(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    /* state was allocated by GC_MALLOC_UNCOLLECTABLE() */
    WindowState *state = JS_GetOpaque(jsThis, WindowClassID);
    OpenWindow *w = malloc(sizeof(OpenWindow));

    if (argc == 0) {
	w->url = w->name = "";
    } else {
	const char *str;
	size_t len;

	str = JS_ToCStringLen(ctx, &len, argv[0]);
	w->url = allocStr(str, len);
	JS_FreeCString(ctx, str);

	if (argc >= 2) {
	    str = JS_ToCStringLen(ctx, &len, argv[1]);
	    w->name = allocStr(str, len);
	    JS_FreeCString(ctx, str);
	} else {
	    w->name = "";
	}
    }

    pushValue(state->win, (void *)w);

    return JS_DupValue(ctx, jsThis); /* XXX segfault without this by returining jsThis. */
}

static JSValue
window_close(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    WindowState *state = JS_GetOpaque(jsThis, WindowClassID);

    state->close = TRUE;

    return JS_UNDEFINED;
}

static JSValue
window_alert(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    const char *str;
    size_t len;

    if (argc < 1) {
	return JS_EXCEPTION;
    }

    str = JS_ToCStringLen(ctx, &len, argv[0]);
    if (!str) {
	return JS_EXCEPTION;
    }
    alert_msg = allocStr(str, len);
    JS_FreeCString(ctx, str);

    return JS_UNDEFINED;
}

static JSValue
window_confirm(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    const char *str;
    char *ans;

    if (argc < 1) {
	return JS_EXCEPTION;
    }

    str = JS_ToCString(ctx, argv[0]);
    if (!str) {
	return JS_EXCEPTION;
    }
    ans = inputChar(Sprintf("%s (y/n)", u2ic(str))->ptr);
    JS_FreeCString(ctx, str);

    if (ans && TOLOWER(*ans) == 'y') {
	return JS_TRUE;
    } else {
	return JS_FALSE;
    }
}

static JSValue
window_get_computed_style(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    if (argc < 1) {
	return JS_EXCEPTION;
    }

    return JS_GetPropertyStr(ctx, argv[0], "style");
}

static JSValue
window_scroll(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    return JS_UNDEFINED;
}

static JSValue
window_get_selection(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    log_msg("XXX: Window.getSelection");

    /* XXX getSelection() should return Selection object. */
    return JS_NewString(ctx, "");
}

static JSValue
window_match_media(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue result = JS_NewObject(ctx); /* XXX MediaQueryList */
    const char *media;
    JSValue flag = JS_FALSE;

    if (argc < 1) {
	return JS_EXCEPTION;
    }

    log_msg("XXX: Window.matchMedia");
    log_to_file(ctx, "XXX:", argc, argv);

    if ((media = JS_ToCString(ctx, argv[0])) != NULL) {
	if (strcasecmp(media, "(display-mode: standalone)") == 0) {
	    log_msg("-> TRUE");
	    flag = JS_TRUE;
	}
	JS_FreeCString(ctx, media);
    }

    JS_SetPropertyStr(ctx, result, "matches", flag);

    return result;
}

static JSValue
window_focus(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    return JS_UNDEFINED;
}

static JSValue
window_blur(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    return JS_UNDEFINED;
}

static struct {
    char *name;
    JSCFunction *func;
} WindowFuncs[] = {
    { "open", window_open } ,
    { "close", window_close },
    { "alert", window_alert },
    { "confirm", window_confirm },
    { "getComputedStyle", window_get_computed_style },
    { "scroll", window_scroll },
    { "scrollTo", window_scroll },
    { "scrollBy", window_scroll },
    { "getSelection", window_get_selection },
    { "matchMedia", window_match_media },
    { "focus", window_focus },
    { "blur", window_blur },
    { "addEventListener", add_event_listener },
    { "removeEventListener", remove_event_listener },
    { "dispatchEvent", dispatch_event },
};

static void
location_final(JSRuntime *rt, JSValue val) {
    GC_FREE(JS_GetOpaque(val, LocationClassID));
}

static const JSClassDef LocationClass = {
    "Location", location_final, NULL /* gc_mark */, NULL /* call */, NULL
};

static JSValue
location_new(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue obj = JS_NewObjectClass(ctx, LocationClassID);
    LocationState *state = (LocationState *)GC_MALLOC_UNCOLLECTABLE(sizeof(LocationState));

    bzero(state, sizeof(LocationState));
    if (argc > 0) {
	if ((state->url = get_str(ctx, argv[0])) == NULL) {
	    return JS_EXCEPTION;
	}

	if (argc > 1) {
	    Str base = get_str(ctx, argv[1]);
	    if (base != NULL) {
		state->url = Sprintf("%s/%s", base->ptr, state->url->ptr);
	    }
	}

#if 0
	{
	    FILE *fp = fopen("scriptlog.txt", "a");
	    fprintf(fp, "Location: %s\n", state->url->ptr);
	    fclose(fp);
	}
#endif

	parseURL(state->url->ptr, &state->pu, NULL);
    } else {
	state->url = Strnew();
    }
    state->refresh = 0;

    JS_SetOpaque(obj, state);

    return obj;
}

static JSValue
location_replace(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    LocationState *state = JS_GetOpaque(jsThis, LocationClassID);
    Str str;

    if (argc < 1 || (str = get_str(ctx, argv[0])) == NULL) {
	return JS_EXCEPTION;
    }

    state->url = str;
    parseURL(state->url->ptr, &state->pu, NULL);
    state->refresh |= JS_LOC_REFRESH;

    return JS_UNDEFINED;
}

static JSValue
location_reload(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    LocationState *state = JS_GetOpaque(jsThis, LocationClassID);

    state->refresh |= JS_LOC_REFRESH;

    return JS_UNDEFINED;
}

static JSValue
location_href_get(JSContext *ctx, JSValueConst jsThis)
{
    LocationState *state = JS_GetOpaque(jsThis, LocationClassID);

    return JS_NewStringLen(ctx, state->url->ptr, state->url->length);
}

static JSValue
location_href_set(JSContext *ctx, JSValueConst jsThis, JSValueConst val)
{
    LocationState *state = JS_GetOpaque(jsThis, LocationClassID);
    Str str;

    if ((str = get_str(ctx, val)) == NULL) {
	return JS_EXCEPTION;
    }

    state->url = str;
    parseURL(state->url->ptr, &state->pu, NULL);
    state->refresh |= JS_LOC_REFRESH;

    return JS_UNDEFINED;
}

static JSValue
location_to_string(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    return location_href_get(ctx, jsThis);
}

static const char*
get_url_scheme_str(int scheme)
{
    switch (scheme) {
    case SCM_HTTP:
	return "http:";
    case SCM_GOPHER:
	return "gopher:";
    case SCM_FTP:
	return "ftp:";
    case SCM_LOCAL:
	return "file:";
    case SCM_NNTP:
	return "nntp:";
    case SCM_NEWS:
	return "news:";
#ifndef USE_W3MMAILER
    case SCM_MAILTO:
	return "mailto:";
#endif
#ifdef USE_SSL
    case SCM_HTTPS:
	return "https:";
#endif
    case SCM_JAVASCRIPT:
	return "javascript:";
    default:
	return "";
    }
}

static JSValue
location_protocol_get(JSContext *ctx, JSValueConst jsThis)
{
    LocationState *state = JS_GetOpaque(jsThis, LocationClassID);

    return JS_NewString(ctx, get_url_scheme_str(state->pu.scheme));
}

static JSValue
location_protocol_set(JSContext *ctx, JSValueConst jsThis, JSValueConst val)
{
    LocationState *state = JS_GetOpaque(jsThis, LocationClassID);
    Str s;
    char *p;

    if ((s = get_str(ctx, val)) == NULL) {
	return JS_EXCEPTION;
    }

    Strcat_charp(s, ":");
    p = allocStr(s->ptr, s->length);
    state->pu.scheme = getURLScheme(&p);
    state->url = parsedURL2Str(&state->pu);
    state->refresh |= JS_LOC_REFRESH;

    return JS_UNDEFINED;
}

static JSValue
location_host_get(JSContext *ctx, JSValueConst jsThis)
{
    LocationState *state = JS_GetOpaque(jsThis, LocationClassID);
    Str s;

    s = Strnew();
    if (state->pu.host) {
	Strcat_charp_n(s, state->pu.host, strlen(state->pu.host));
	if (state->pu.has_port)
	    Strcat(s, Sprintf(":%d", state->pu.port));
    }
    return JS_NewStringLen(ctx, s->ptr, s->length);
}

static JSValue
location_host_set(JSContext *ctx, JSValueConst jsThis, JSValueConst val)
{
    LocationState *state = JS_GetOpaque(jsThis, LocationClassID);
    char *p, *q;

    if ((p = get_cstr(ctx, val)) == NULL) {
	return JS_EXCEPTION;
    }

    if ((q = strchr(p, ':')) != NULL) {
	*q++ = '\0';
	state->pu.port = atoi(q);
	state->pu.has_port = 1;
    }
    state->pu.host = Strnew_charp(p)->ptr;
    state->url = parsedURL2Str(&state->pu);
    state->refresh |= JS_LOC_REFRESH;

    return JS_UNDEFINED;
}

static JSValue
location_hostname_get(JSContext *ctx, JSValueConst jsThis)
{
    LocationState *state = JS_GetOpaque(jsThis, LocationClassID);
    Str s;

    s = Strnew();
    if (state->pu.host) {
	Strcat_charp_n(s, state->pu.host, strlen(state->pu.host));
    }
    return JS_NewStringLen(ctx, s->ptr, s->length);
}

static JSValue
location_hostname_set(JSContext *ctx, JSValueConst jsThis, JSValueConst val)
{
    LocationState *state = JS_GetOpaque(jsThis, LocationClassID);
    char *str;

    if ((str = get_cstr(ctx, val)) == NULL) {
	return JS_EXCEPTION;
    }

    state->pu.host = str;
    state->url = parsedURL2Str(&state->pu);
    state->refresh |= JS_LOC_REFRESH;

    return JS_UNDEFINED;
}

static JSValue
location_port_get(JSContext *ctx, JSValueConst jsThis)
{
    LocationState *state = JS_GetOpaque(jsThis, LocationClassID);
    Str s;

    if (state->pu.has_port)
	s = Sprintf("%d", state->pu.port);
    else
	s = Strnew();
    return JS_NewStringLen(ctx, s->ptr, s->length);
}

static JSValue
location_port_set(JSContext *ctx, JSValueConst jsThis, JSValueConst val)
{
    LocationState *state = JS_GetOpaque(jsThis, LocationClassID);

    if (JS_IsString(val)) {
	const char *str;

	str = JS_ToCString(ctx, val);
	state->pu.port = atoi(str);
	JS_FreeCString(ctx, str);
    } else if (JS_IsNumber(val)) {
	int i;
	JS_ToInt32(ctx, &i, val);
	state->pu.port = i;
    } else {
	return JS_EXCEPTION;
    }
    state->url = parsedURL2Str(&state->pu);
    state->refresh |= JS_LOC_REFRESH;

    return JS_UNDEFINED;
}

static JSValue
location_pathname_get(JSContext *ctx, JSValueConst jsThis)
{
    LocationState *state = JS_GetOpaque(jsThis, LocationClassID);
    Str s;

    s = Strnew();
    if (state->pu.file) {
	Strcat_charp_n(s, state->pu.file, strlen(state->pu.file));
    }
    return JS_NewStringLen(ctx, s->ptr, s->length);
}

static JSValue
location_pathname_set(JSContext *ctx, JSValueConst jsThis, JSValueConst val)
{
    LocationState *state = JS_GetOpaque(jsThis, LocationClassID);
    char *str;

    if ((str = get_cstr(ctx, val)) == NULL) {
	return JS_EXCEPTION;
    }

    state->pu.file = str;
    state->url = parsedURL2Str(&state->pu);
    state->refresh |= JS_LOC_REFRESH;

    return JS_UNDEFINED;
}

static JSValue
location_search_params_get(JSContext *ctx, JSValueConst jsThis)
{
    JSValue params = JS_GetPropertyStr(ctx, jsThis, "w3m_searchParams");

    if (!JS_IsObject(params)) {
	LocationState *state = JS_GetOpaque(jsThis, LocationClassID);
	char *script;

	if (state->pu.query) {
	    script = Sprintf("new URLSearchParams(\"%s\");", state->pu.query)->ptr;
	} else {
	    script = "new URLSearchParams();";
	}

	params = JS_Eval(ctx, script, strlen(script), "<input>", EVAL_FLAG);
	JS_SetPropertyStr(ctx, jsThis, "w3m_searchParams", JS_DupValue(ctx, params));
    }

    return params;
}

static JSValue
location_search_get(JSContext *ctx, JSValueConst jsThis)
{
#if 1
    const char script[] =
	"{"
	"  let params = this.searchParams.toString();"
	"  if (params) {"
	"    params = \"?\" + params;"
	"  }"
	"  params;"
	"}";
    return JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1, "<input>", EVAL_FLAG);
#else
    LocationState *state = JS_GetOpaque(jsThis, LocationClassID);
    Str s;

    s = Strnew();
    if (state->pu.query) {
	Strcat_charp(s, "?");
	Strcat_charp_n(s, state->pu.query, strlen(state->pu.query));
    }
    return JS_NewStringLen(ctx, s->ptr, s->length);
#endif
}

static JSValue
location_search_set(JSContext *ctx, JSValueConst jsThis, JSValueConst val)
{
    LocationState *state;
    char *query;
    char *script;
    JSValue params;

    if ((query = get_cstr(ctx, val)) == NULL) {
	return JS_EXCEPTION;
    }

    script = Sprintf("new URLSearchParams(\"%s\");", query)->ptr;
    params = JS_Eval(ctx, script, strlen(script), "<input>", EVAL_FLAG);
    JS_SetPropertyStr(ctx, jsThis, "w3m_searchParams", JS_DupValue(ctx, params));

    state = JS_GetOpaque(jsThis, LocationClassID);
    state->pu.query = query;
    state->url = parsedURL2Str(&state->pu);
    state->refresh |= JS_LOC_REFRESH;

    return JS_UNDEFINED;
}

static JSValue
location_hash_get(JSContext *ctx, JSValueConst jsThis)
{
    LocationState *state = JS_GetOpaque(jsThis, LocationClassID);
    Str s;

    s = Strnew();
    if (state->pu.label) {
	Strcat_charp(s, "#");
	Strcat_charp_n(s, state->pu.label, strlen(state->pu.label));
    }
    return JS_NewStringLen(ctx, s->ptr, s->length);
}

static JSValue
location_hash_set(JSContext *ctx, JSValueConst jsThis, JSValueConst val)
{
    LocationState *state = JS_GetOpaque(jsThis, LocationClassID);
    char *str;

    if ((str = get_cstr(ctx, val)) == NULL) {
	return JS_EXCEPTION;
    }

    state->pu.label = str;
    state->url = parsedURL2Str(&state->pu);
    state->refresh |= JS_LOC_HASH;

    return JS_UNDEFINED;
}

static JSValue
location_username_get(JSContext *ctx, JSValueConst jsThis)
{
    LocationState *state = JS_GetOpaque(jsThis, LocationClassID);
    Str s;

    s = Strnew();
    if (state->pu.user) {
	Strcat_charp_n(s, state->pu.user, strlen(state->pu.user));
    }
    return JS_NewStringLen(ctx, s->ptr, s->length);
}

static JSValue
location_username_set(JSContext *ctx, JSValueConst jsThis, JSValueConst val)
{
    LocationState *state = JS_GetOpaque(jsThis, LocationClassID);
    char *str;

    if ((str = get_cstr(ctx, val)) == NULL) {
	return JS_EXCEPTION;
    }

    state->pu.user = str;
    state->url = parsedURL2Str(&state->pu);
    state->refresh |= JS_LOC_REFRESH;

    return JS_UNDEFINED;
}

static JSValue
location_password_get(JSContext *ctx, JSValueConst jsThis)
{
    LocationState *state = JS_GetOpaque(jsThis, LocationClassID);
    Str s;

    s = Strnew();
    if (state->pu.pass) {
	Strcat_charp_n(s, state->pu.pass, strlen(state->pu.pass));
    }
    return JS_NewStringLen(ctx, s->ptr, s->length);
}

static JSValue
location_password_set(JSContext *ctx, JSValueConst jsThis, JSValueConst val)
{
    LocationState *state = JS_GetOpaque(jsThis, LocationClassID);
    char *str;

    if ((str = get_cstr(ctx, val)) == NULL) {
	return JS_EXCEPTION;
    }

    state->pu.pass = str;
    state->url = parsedURL2Str(&state->pu);
    state->refresh |= JS_LOC_REFRESH;

    return JS_UNDEFINED;
}

static JSValue
location_origin_get(JSContext *ctx, JSValueConst jsThis)
{
    const char script[] =
	"this.protocol + \"//\" + this.hostname + (this.port ? \":\" + this.port : \"\")";
    return backtrace(ctx, script,
		     JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1, "<input>", EVAL_FLAG));
}

static const JSCFunctionListEntry LocationFuncs[] = {
    JS_CFUNC_DEF("replace", 1, location_replace),
    JS_CFUNC_DEF("reload", 1, location_reload),
    JS_CFUNC_DEF("toString", 1, location_to_string),
    JS_CGETSET_DEF("href", location_href_get, location_href_set),
    JS_CGETSET_DEF("protocol", location_protocol_get, location_protocol_set),
    JS_CGETSET_DEF("host", location_host_get, location_host_set),
    JS_CGETSET_DEF("hostname", location_hostname_get, location_hostname_set),
    JS_CGETSET_DEF("port", location_port_get, location_port_set),
    JS_CGETSET_DEF("pathname", location_pathname_get, location_pathname_set),
    JS_CGETSET_DEF("search", location_search_get, location_search_set),
    JS_CGETSET_DEF("searchParams", location_search_params_get, NULL),
    JS_CGETSET_DEF("hash", location_hash_get, location_hash_set),
    JS_CGETSET_DEF("username", location_username_get, location_username_set),
    JS_CGETSET_DEF("password", location_password_get, location_password_set),
    JS_CGETSET_DEF("origin", location_origin_get, NULL),

    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Location", JS_PROP_CONFIGURABLE),
};

static const JSClassDef CanvasRenderingContext2DClass = {
    "CanvasRenderingContext2D", NULL, NULL /* gc_mark */, NULL /* call */, NULL
};

static JSValue
canvas_rendering_context2d_new(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    return JS_NewObjectClass(ctx, CanvasRenderingContext2DClassID);
}

static JSValue
canvas2d_ignore(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    return JS_UNDEFINED;
}

static JSValue
canvas2d_fill_text(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    log_to_file(ctx, "Canvas:", 1, argv);

    return JS_UNDEFINED;
}

static int
get_num_chars(JSContext *ctx, JSValue str)
{
    JSValue prop;
    JSValue ret;
    int num;

    prop = JS_GetPropertyStr(ctx, str, "length");
    ret = JS_Call(ctx, prop, str, 0, NULL);
    if (JS_IsNumber(ret)) {
	JS_ToInt32(ctx, &num, ret);
    } else {
	num = 0;
    }
    JS_FreeValue(ctx, prop);
    JS_FreeValue(ctx, ret);

    return num;
}

static JSValue
canvas2d_measure_text(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue tm;
    int num;

    if (argc < 1) {
	num = 0;
    } else {
	num = get_num_chars(ctx, argv[0]);
    }

    tm = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, tm, "width", JS_NewFloat64(ctx, term_ppc * num));
    JS_SetPropertyStr(ctx, tm, "emHeightAscent", JS_NewFloat64(ctx, term_ppl * 0.8));
    JS_SetPropertyStr(ctx, tm, "emHeightDescent", JS_NewFloat64(ctx, term_ppl * 0.2));
    /* XXX */

    return tm;
}

static const JSCFunctionListEntry CanvasRenderingContext2DFuncs[] = {
    JS_CFUNC_DEF("arc", 1, canvas2d_ignore),
    JS_CFUNC_DEF("arcTo", 1, canvas2d_ignore),
    JS_CFUNC_DEF("beginPath", 1, canvas2d_ignore),
    JS_CFUNC_DEF("clearRect", 1, canvas2d_ignore),
    JS_CFUNC_DEF("closePath", 1, canvas2d_ignore),
    JS_CFUNC_DEF("drawFocusIfNeeded", 1, canvas2d_ignore),
    JS_CFUNC_DEF("ellips", 1, canvas2d_ignore),
    JS_CFUNC_DEF("fillRect", 1, canvas2d_ignore),
    JS_CFUNC_DEF("fillText", 1, canvas2d_fill_text),
    JS_CFUNC_DEF("lineTo", 1, canvas2d_ignore),
    JS_CFUNC_DEF("measureText", 1, canvas2d_measure_text),
    JS_CFUNC_DEF("moveTo", 1, canvas2d_ignore),
    JS_CFUNC_DEF("rect", 1, canvas2d_ignore),
    JS_CFUNC_DEF("save", 1, canvas2d_ignore),
    JS_CFUNC_DEF("scale", 1, canvas2d_ignore),
    JS_CFUNC_DEF("setLineDash", 1, canvas2d_ignore),
    JS_CFUNC_DEF("stroke", 1, canvas2d_ignore),
    JS_CFUNC_DEF("strokeText", 1, canvas2d_ignore),
    JS_CFUNC_DEF("strokeRect", 1, canvas2d_ignore),

    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Canvas", JS_PROP_CONFIGURABLE),
};

static void
html_form_element_final(JSRuntime *rt, JSValue val) {
    GC_FREE(JS_GetOpaque(val, HTMLFormElementClassID));
}

static const JSClassDef HTMLFormElementClass = {
    "HTMLFormElement", html_form_element_final, NULL /* gc_mark */, NULL /* call */, NULL
};

static void
init_children(JSContext *ctx, JSValue obj, const char *tagname)
{
    JSValue children = JS_Eval(ctx, "new HTMLCollection();", 21, "<input>", EVAL_FLAG);

    /* Node */
    JS_SetPropertyStr(ctx, obj, "childNodes", children); /* XXX NodeList */

    /* Element */
    JS_SetPropertyStr(ctx, obj, "children", JS_DupValue(ctx, children));

    if (strcasecmp(tagname, "FORM") == 0) {
	JS_SetPropertyStr(ctx, obj, "elements", JS_DupValue(ctx, children));
    } else if (strcasecmp(tagname, "SELECT") == 0) {
	JS_SetPropertyStr(ctx, obj, "options", JS_DupValue(ctx, children));
    }
}

static JSValue
style_get_property_value(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    return JS_NewString(ctx, "");
}

static void
set_element_property(JSContext *ctx, JSValue obj, JSValue tagname)
{
    const char *str = JS_ToCString(ctx, tagname);
    JSValue style;

    init_children(ctx, obj, str);

    /* Node */
    JS_SetPropertyStr(ctx, obj, "parentNode", JS_NULL);
    if (strcasecmp(str, "#text") == 0) {
	JS_SetPropertyStr(ctx, obj, "nodeType", JS_NewInt32(ctx, 3)); /* TEXT_NODE */
	JS_SetPropertyStr(ctx, obj, "data", JS_NewString(ctx, "")); /* XXX CharacterData.data */
    } else if (strcasecmp(str, "#comment") == 0) {
	JS_SetPropertyStr(ctx, obj, "nodeType", JS_NewInt32(ctx, 8)); /* COMMENT_NODE */
	JS_SetPropertyStr(ctx, obj, "data", JS_NewString(ctx, "")); /* XXX CharacterData.data */
    } else {
	JS_SetPropertyStr(ctx, obj, "nodeType", JS_NewInt32(ctx, 1)); /* ELEMENT_NODE */
    }
    JS_SetPropertyStr(ctx, obj, "nodeValue", JS_NULL);
    JS_SetPropertyStr(ctx, obj, "nodeName", JS_DupValue(ctx, tagname));

    /* Element */
    JS_SetPropertyStr(ctx, obj, "tagName", JS_DupValue(ctx, tagname));
    JS_SetPropertyStr(ctx, obj, "parentElement", JS_NULL);
    JS_SetPropertyStr(ctx, obj, "querySelector",
		      JS_Eval(ctx, "document.querySelector;", 23, "<input>", EVAL_FLAG));
    JS_SetPropertyStr(ctx, obj, "querySelectorAll",
		      JS_Eval(ctx, "document.querySelectorAll;", 26, "<input>", EVAL_FLAG));
    JS_SetPropertyStr(ctx, obj, "getElementsByTagName",
		      JS_Eval(ctx, "document.getElementsByTagName;", 30, "<input>", EVAL_FLAG));
    JS_SetPropertyStr(ctx, obj, "getElementsByClassName",
		      JS_Eval(ctx, "document.getElementsByClassName;", 32, "<input>", EVAL_FLAG));

    JS_SetPropertyStr(ctx, obj, "clientWidth", JS_NewInt32(ctx, term_ppc));
    JS_SetPropertyStr(ctx, obj, "clientHeight", JS_NewInt32(ctx, term_ppl));
    JS_SetPropertyStr(ctx, obj, "clientLeft", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, obj, "clientTop", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, obj, "scrollWidth", JS_NewInt32(ctx, term_ppc));
    JS_SetPropertyStr(ctx, obj, "scrollHeight", JS_NewInt32(ctx, term_ppl));
    JS_SetPropertyStr(ctx, obj, "scrollLeft", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, obj, "scrollTop", JS_NewInt32(ctx, 0));

    JS_SetPropertyStr(ctx, obj, "classList",
		      JS_Eval(ctx, "new DOMTokenList();", 19, "<input>", EVAL_FLAG));;

    /* HTMLElement */
    style = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, style, "fontSize",
		      JS_NewString(ctx, Sprintf("%dpx", term_ppl)->ptr));
    JS_SetPropertyStr(ctx, style, "zoom", JS_NewString(ctx, "normal"));
    JS_SetPropertyStr(ctx, style, "backgroundColor", JS_NewString(ctx, "black"));
    JS_SetPropertyStr(ctx, style, "foregroundColor", JS_NewString(ctx, "white"));
    JS_SetPropertyStr(ctx, style, "getPropertyValue",
		      JS_NewCFunction(ctx, style_get_property_value, "getPropertyValue", 1));
    JS_SetPropertyStr(ctx, style, "whiteSpace", JS_NewString(ctx, "normal"));
    JS_SetPropertyStr(ctx, style, "zIndex", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, obj, "style", style);

    JS_SetPropertyStr(ctx, obj, "offsetWidth", JS_NewInt32(ctx, term_ppc));
    JS_SetPropertyStr(ctx, obj, "offsetHeight", JS_NewInt32(ctx, term_ppl));
    JS_SetPropertyStr(ctx, obj, "offsetLeft", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, obj, "offsetTop", JS_NewInt32(ctx, 0));

    JS_SetPropertyStr(ctx, obj, "dataset", JS_NewObject(ctx));

    /* XXX HTMLSelectElement */
    JS_SetPropertyStr(ctx, obj, "disabled", JS_FALSE);

    /* XXX HTMLIFrameElement */
    if (strcasecmp(str, "IFRAME") == 0) {
	const char script[] =
	    "{"
	    "  let doc = Object.assign(new Document(), document);"
	    "  w3m_initDocumentTree(doc);"
	    "  doc;"
	    "}";
	JS_SetPropertyStr(ctx, obj, "contentDocument",
			  JS_Eval(ctx, script, sizeof(script) - 1, "<input>", EVAL_FLAG));
    }

    JS_FreeCString(ctx, str);
}

static JSValue
html_form_element_new(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue obj;
    CtxState *ctxstate;
    HTMLFormElementState *formstate;

    ctxstate = JS_GetContextOpaque(ctx);
    if (JS_IsUndefined(ctxstate->tagnames[TN_FORM])) {
	ctxstate->tagnames[TN_FORM] = JS_NewString(ctx, "FORM");
    }

    obj = JS_NewObjectClass(ctx, HTMLFormElementClassID);
    set_element_property(ctx, obj, ctxstate->tagnames[TN_FORM]);

    formstate = (HTMLFormElementState *)GC_MALLOC_UNCOLLECTABLE(sizeof(HTMLFormElementState));
    memset(formstate, 0, sizeof(*formstate));

    JS_SetOpaque(obj, formstate);

    return obj;
}

static JSValue
html_form_element_submit(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    HTMLFormElementState *state = JS_GetOpaque(jsThis, HTMLFormElementClassID);

    state->submit = 1;

    return JS_UNDEFINED;
}

static JSValue
html_form_element_reset(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    HTMLFormElementState *state = JS_GetOpaque(jsThis, HTMLFormElementClassID);

    /* reset() isn't implemented (see script.c) */
    state->reset = 1;

    return JS_UNDEFINED;
}

static const JSClassDef HTMLElementClass = {
    "HTMLElement", NULL /* finalizer */, NULL /* gc_mark */, NULL /* call */, NULL
};

static JSValue
html_element_new(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue obj;

    if (argc < 1 || !JS_IsString(argv[0])) {
	return JS_EXCEPTION;
    }

    obj = JS_NewObjectClass(ctx, HTMLElementClassID);
    set_element_property(ctx, obj, argv[0]);

    return obj;
}

static const JSClassDef HTMLImageElementClass = {
    "HTMLImageElement", NULL /* finalizer */, NULL /* gc_mark */, NULL /* call */, NULL
};

static JSValue
html_image_element_new(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue obj;
    CtxState *ctxstate;

    ctxstate = JS_GetContextOpaque(ctx);
    if (JS_IsUndefined(ctxstate->tagnames[TN_IMG])) {
	ctxstate->tagnames[TN_IMG] = JS_NewString(ctx, "IMG");
    }

    obj = JS_NewObjectClass(ctx, HTMLImageElementClassID);
    set_element_property(ctx, obj, ctxstate->tagnames[TN_IMG]);

    return obj;
}

static const JSClassDef HTMLSelectElementClass = {
    "HTMLSelectElement", NULL /* finalizer */, NULL /* gc_mark */, NULL /* call */, NULL
};

static JSValue
html_select_element_new(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue obj;
    CtxState *ctxstate;

    ctxstate = JS_GetContextOpaque(ctx);
    if (JS_IsUndefined(ctxstate->tagnames[TN_SELECT])) {
	ctxstate->tagnames[TN_SELECT] = JS_NewString(ctx, "SELECT");
    }

    obj = JS_NewObjectClass(ctx, HTMLSelectElementClassID);
    set_element_property(ctx, obj, ctxstate->tagnames[TN_SELECT]);

    return obj;
}

static const JSClassDef HTMLScriptElementClass = {
    "HTMLScriptElement", NULL /* finalizer */, NULL /* gc_mark */, NULL /* call */, NULL
};

static JSValue
html_script_element_new(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue obj;
    CtxState *ctxstate;

    ctxstate = JS_GetContextOpaque(ctx);
    if (JS_IsUndefined(ctxstate->tagnames[TN_SCRIPT])) {
	ctxstate->tagnames[TN_SCRIPT] = JS_NewString(ctx, "SCRIPT");
    }

    obj = JS_NewObjectClass(ctx, HTMLScriptElementClassID);
    set_element_property(ctx, obj, ctxstate->tagnames[TN_SCRIPT]);

    return obj;
}

static const JSClassDef HTMLCanvasElementClass = {
    "HTMLCanvasElement", NULL /* finalizer */, NULL /* gc_mark */, NULL /* call */, NULL
};

static JSValue
html_canvas_element_new(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue obj;
    CtxState *ctxstate;

    ctxstate = JS_GetContextOpaque(ctx);
    if (JS_IsUndefined(ctxstate->tagnames[TN_CANVAS])) {
	ctxstate->tagnames[TN_CANVAS] = JS_NewString(ctx, "CANVAS");
    }

    obj = JS_NewObjectClass(ctx, HTMLCanvasElementClassID);
    set_element_property(ctx, obj, ctxstate->tagnames[TN_CANVAS]);

    return obj;
}

static const JSClassDef SVGElementClass = {
    "SVGElement", NULL /* finalizer */, NULL /* gc_mark */, NULL /* call */, NULL
};

static JSValue
svg_element_new(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue obj;

    /* XXX */
    if (argc < 1 || !JS_IsString(argv[0])) {
	return JS_EXCEPTION;
    }

    obj = JS_NewObjectClass(ctx, SVGElementClassID);
    set_element_property(ctx, obj, argv[0]);

    return obj;
}

static const JSClassDef ElementClass = {
    "Element", NULL /* finalizer */, NULL /* gc_mark */, NULL /* call */, NULL
};

static JSValue
element_new(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue obj;

    if (argc < 1 || !JS_IsString(argv[0])) {
	return JS_EXCEPTION;
    }

    obj = JS_NewObjectClass(ctx, ElementClassID);
    set_element_property(ctx, obj, argv[0]);

    return obj;
}

static JSValue
element_append_child(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue val;
    JSValue prop;
    JSValue vret;
    int32_t iret;

    if (argc < 1  /*  appendChild: 1, insertBefore: 2*/) {
	return JS_EXCEPTION;
    }

#if 0
    FILE *fp = fopen("w3mlog.txt", "a");
    fprintf(fp, "Ref count %d\n", ((JSRefCountHeader*)JS_VALUE_GET_PTR(argv[0]))->ref_count);
    fclose(fp);
#endif

    val = JS_GetPropertyStr(ctx, argv[0], "id");
    if (JS_IsString(val)) {
	const char *id = JS_ToCString(ctx, val);
	JS_SetPropertyStr(ctx, jsThis, id, JS_DupValue(ctx, argv[0]));
	JS_FreeCString(ctx, id);
    }
    JS_FreeValue(ctx, val);

    JS_SetPropertyStr(ctx, argv[0], "parentNode", JS_DupValue(ctx, jsThis));
    JS_SetPropertyStr(ctx, argv[0], "parentElement", JS_DupValue(ctx, jsThis));

#if 0
    fp = fopen("w3mlog.txt", "a");
    fprintf(fp, "Ref count %d after JS_SetPropertyStr(JS_DupValue())\n",
	    ((JSRefCountHeader*)JS_VALUE_GET_PTR(argv[0]))->ref_count);
    fclose(fp);
#endif

    val = JS_GetPropertyStr(ctx, jsThis, "children");
    prop = JS_GetPropertyStr(ctx, val, "push");
    vret = JS_Call(ctx, prop, val, 1, argv);
    JS_FreeValue(ctx, val);
    JS_FreeValue(ctx, prop);

    JS_ToInt32(ctx, &iret, vret);
    JS_FreeValue(ctx, vret);

#if 0
    fp = fopen("w3mlog.txt", "a");
    fprintf(fp, "Ref count %d after array.push()\n",
	    ((JSRefCountHeader*)JS_VALUE_GET_PTR(argv[0]))->ref_count);
    fclose(fp);
#endif

    if (iret > 0) {
	return JS_DupValue(ctx, argv[0]); /* XXX segfault without this by returining argv[0]. */
    } else {
	return JS_EXCEPTION;
    }
}

static JSValue
element_remove_child(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue children, prop, argv2[2];

    if (argc < 1) {
	return JS_EXCEPTION;
    }

    children = JS_GetPropertyStr(ctx, jsThis, "childNodes");
    prop = JS_GetPropertyStr(ctx, children, "indexOf");
    argv2[0] = JS_Call(ctx, prop, children, 1, argv);
    JS_FreeValue(ctx, prop);
    if (JS_IsNumber(argv2[0])) {
	argv2[1] = JS_NewInt32(ctx, 1);
	prop = JS_GetPropertyStr(ctx, children, "splice");
	JS_FreeValue(ctx, JS_Call(ctx, prop, children, 2, argv2));
	JS_FreeValue(ctx, prop);
	JS_FreeValue(ctx, argv2[1]);
    }
    JS_FreeValue(ctx, argv2[0]);
    JS_FreeValue(ctx, children);

    return JS_DupValue(ctx, argv[0]); /* XXX segfault without this by returining argv[0]. */
}

static JSValue
element_replace_child(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue children, prop, idx, ret;
    int i;

    if (argc < 2) {
	return JS_EXCEPTION;
    }

    children = JS_GetPropertyStr(ctx, jsThis, "childNodes");
    prop = JS_GetPropertyStr(ctx, children, "indexOf");
    idx = JS_Call(ctx, prop, children, 1, argv + 1);
    JS_FreeValue(ctx, prop);
    JS_ToInt32(ctx, &i, idx);
    JS_FreeValue(ctx, idx);

    if (i >= 0) {
	ret = JS_DupValue(ctx, argv[1]);
	JS_SetPropertyUint32(ctx, children, i, JS_DupValue(ctx, argv[0]));
    } else {
	ret = JS_UNDEFINED;
    }

    JS_FreeValue(ctx, children);

    return ret;
}

static JSValue
element_remove(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    const char script[] = "if (this.parentElement) { this.parentElement.removeChild(this); }";
    return backtrace(ctx, script,
		     JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1, "<input>", EVAL_FLAG));
}

static JSValue element_set_attribute(JSContext *ctx, JSValueConst jsThis, int argc,
				     JSValueConst *argv);
static JSValue element_get_attribute(JSContext *ctx, JSValueConst jsThis, int argc,
				     JSValueConst *argv);

static JSValue
element_remove_attribute(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    if (argc < 1) {
	return JS_EXCEPTION;
    }

    return JS_UNDEFINED;
}

static JSValue
element_has_attribute(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue ret = JS_FALSE;

    if (argc < 1) {
	return JS_EXCEPTION;
    }

    if (JS_IsString(argv[0])) {
	const char *key = JS_ToCString(ctx, argv[0]);
	JSValue prop = JS_GetPropertyStr(ctx, jsThis, key);
	if (!JS_IsUndefined(prop)) {
	    ret = JS_TRUE;
	}
	JS_FreeCString(ctx, key);
	JS_FreeValue(ctx, prop);
    }

    return ret;
}

static JSValue
element_get_bounding_client_rect(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue rect;

    rect = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, rect, "x", JS_NewFloat64(ctx, 0.0));
    JS_SetPropertyStr(ctx, rect, "y", JS_NewFloat64(ctx, 0.0));
    JS_SetPropertyStr(ctx, rect, "width", JS_NewFloat64(ctx, term_ppc));
    JS_SetPropertyStr(ctx, rect, "height", JS_NewFloat64(ctx, term_ppl));
    JS_SetPropertyStr(ctx, rect, "left", JS_NewFloat64(ctx, 0.0));
    JS_SetPropertyStr(ctx, rect, "top", JS_NewFloat64(ctx, 0.0));
    JS_SetPropertyStr(ctx, rect, "right", JS_NewFloat64(ctx, 10.0));
    JS_SetPropertyStr(ctx, rect, "bottom", JS_NewFloat64(ctx, 10.0));

    return rect;
}

static JSValue
element_get_client_rects(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue rects = JS_NewArray(ctx);
    JSValue prop = JS_GetPropertyStr(ctx, rects, "push");
    JSValue rect = element_get_bounding_client_rect(ctx, jsThis, argc, argv);

    JS_FreeValue(ctx, JS_Call(ctx, prop, rects, 1, &rect));
    JS_FreeValue(ctx, prop);
    JS_FreeValue(ctx, rect);

    return rects;
}

static JSValue
element_has_child_nodes(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    const char script[] = "if (this.childNodes.length == 0) { true; } else { false; }";
    return backtrace(ctx, script,
		     JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1, "<input>", EVAL_FLAG));
}

static JSValue
element_first_child_get(JSContext *ctx, JSValueConst jsThis)
{
    const char script[] =
	"if (this.childNodes.length > 0) {"
	"  this.childNodes[0];"
	"} else {"
	"  null;"
	"}";

    return backtrace(ctx, script,
		     JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1, "<input>", EVAL_FLAG));
}

static JSValue
element_last_child_get(JSContext *ctx, JSValueConst jsThis)
{
    const char script[] =
	"if (this.childNodes.length > 0) {"
	"  this.childNodes[this.childNodes.length - 1];"
	"} else {"
	"  null;"
	"}";

    return backtrace(ctx, script,
		     JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1, "<input>", EVAL_FLAG));
}

static JSValue
element_next_sibling_get(JSContext *ctx, JSValueConst jsThis)
{
    const char script[] =
	"if (this.parentNode && this.parentNode.childNodes.length >= 2) {"
	"  let element = null;"
	"  for (let i = this.parentNode.childNodes.length - 2; i >= 0; i--) {"
	"    if (this.parentNode.childNodes[i] == this) {"
	"      element = this.parentNode.childNodes[i + 1];"
	"      break;"
	"    }"
	"  }"
	"  element;"
	"} else {"
	"  null;"
	"}";

    return backtrace(ctx, script,
		     JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1, "<input>", EVAL_FLAG));
}

static JSValue
element_previous_sibling_get(JSContext *ctx, JSValueConst jsThis)
{
    const char script[] =
	"if (this.parentNode) {"
        "  let element = null;"
	"  for (let i = 1; i < this.parentNode.childNodes.length; i++) {"
	"    if (this.parentNode.childNodes[i] == this) {"
	"      element = this.parentNode.childNodes[i - 1];"
	"      break;"
	"    }"
	"  }"
	"  element;"
	"} else {"
	"  null;"
	"}";

    return backtrace(ctx, script,
		     JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1, "<input>", EVAL_FLAG));
}

static JSValue
element_owner_document_get(JSContext *ctx, JSValueConst jsThis)
{
    /* XXX */
    return JS_Eval(ctx, "document;", 9, "<input>", EVAL_FLAG);
}

static JSValue
element_content_window_get(JSContext *ctx, JSValueConst jsThis)
{
    /* XXX */
    return JS_Eval(ctx, "window;", 7, "<input>", EVAL_FLAG);
}

static JSValue
element_matches(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    if (argc < 1) {
	return JS_EXCEPTION;
    }

    log_msg("XXX: Element.matches");
    /* XXX argv[0] is CSS selector. */

    return JS_FALSE;
}

static JSValue
element_attributes_get(JSContext *ctx, JSValueConst jsThis)
{
    const char script[] = "w3m_elementAttributes(this);";
    return backtrace(ctx, script,
		     JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1, "<input>", EVAL_FLAG));
}

static JSValue
element_compare_document_position(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    log_msg("XXX: Node.compareDocumentPosition");
    return JS_NewInt32(ctx, 4); /* DOCUMENT_POSITION_FOLLOWING */
}

static JSValue
element_closest(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    /* XXX */
    log_msg("XXX: Element.closest");
    return JS_GetPropertyStr(ctx, jsThis, "parentElement");
}

static JSValue
element_offset_parent_get(JSContext *ctx, JSValueConst jsThis)
{
    /* XXX */
    log_msg("XXX: HTMLElement.offsetParent");
    return JS_GetPropertyStr(ctx, jsThis, "parentElement");
}

static JSValue
element_do_scroll(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    /* XXX */
    return JS_UNDEFINED;
}

static JSValue
element_get_elements_by_tag_name(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    const char *tag;
    char *script;

    if (argc < 1 || !JS_IsString(argv[0])) {
	return JS_EXCEPTION;
    }

    tag = JS_ToCString(ctx, argv[0]);
    /* See script_buf2js() in script.c */
    script = Sprintf("{"
		     "  let elements = new HTMLCollection();"
		     "  w3m_getElementsByTagName(this, \"%s\", elements);"
		     "  if (elements.length == 0) {"
		     "    let element = new HTMLElement(\"span\");"
		     "    element.name = \"%s\";"
		     "    element.value = \"\";"
		     "    document.body.appendChild(element);"
		     "    elements.push(element);"
		     "  }"
		     "  elements;"
		     "}", tag, tag)->ptr;
    JS_FreeCString(ctx, tag);

    return backtrace(ctx, script,
		     JS_EvalThis(ctx, jsThis, script, strlen(script), "<input>", EVAL_FLAG));
}

static JSValue
element_get_elements_by_class_name(JSContext *ctx, JSValueConst jsThis,
				   int argc, JSValueConst *argv)
{
    const char *tag;
    char *script;

    if (argc < 1 || !JS_IsString(argv[0])) {
	return JS_EXCEPTION;
    }

    tag = JS_ToCString(ctx, argv[0]);
    /* See script_buf2js() in script.c */
    script = Sprintf("{"
		     "  let elements = new HTMLCollection();"
		     "  w3m_getElementsByClassName(document, \"%s\", elements);"
		     "  if (elements.length == 0) {"
		     "    let element = new HTMLElement(\"span\");"
		     "    element.className = \"%s\";"
		     "    element.value = \"\";"
		     "    document.body.appendChild(element);"
		     "    elements.push(element);"
		     "  }"
		     "  elements;"
		     "};", tag, tag)->ptr;
    JS_FreeCString(ctx, tag);

    return backtrace(ctx, script,
		     JS_EvalThis(ctx, jsThis, script, strlen(script), "<input>", EVAL_FLAG));
}

static JSValue
element_clone_node(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    if (argc >= 1 && JS_ToBool(ctx, argv[0])) {
	const char script[] =
	    "{"
	    "  let element = Object.assign(new HTMLElement(this.tagName), this); /* XXX */"
	    "  element.parentNode = element.parentElement = null;"
	    "  element.childNodes = element.children = new HTMLCollection();"
	    "  w3m_cloneNode(element, this);"
	    "  element;"
	    "}";
	return backtrace(ctx, script,
			 JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1,
				     "<input>", EVAL_FLAG));
    } else {
	const char script[] =
	    "{"
	    "  let element = Object.assign(new HTMLElement(this.tagName), this); /* XXX */"
	    "  element.parentNode = element.parentElement = null;"
	    "  element.childNodes = element.children = new HTMLCollection();"
	    "  element;"
	    "}";
	return backtrace(ctx, script,
			 JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1,
				     "<input>", EVAL_FLAG));
    }
}

static JSValue
element_text_content_get(JSContext *ctx, JSValueConst jsThis)
{
    const char script[] =
	"function w3m_getChildrenText(element) {"
	"  text = \"\";"
	"  for (let i = 0; i < element.children.length; i++) {"
	"    if (element.children[i].nodeName === \"#text\") {"
	"      text += element.children[i].nodeValue;"
	"    }"
	"    text += w3m_getChildrenText(element.children[i]);"
	"  }"
	"  return text;"
	"}"
	"w3m_getChildrenText(this);";

    return backtrace(ctx, script,
		     JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1, "<input>", EVAL_FLAG));
}

#ifdef USE_LIBXML2
static void
create_tree_from_html(JSContext *ctx, JSValue parent, const char *html) {
    xmlDoc *doc = htmlReadMemory(html, strlen(html), "", "utf-8",
				 HTML_PARSE_RECOVER | HTML_PARSE_NOERROR |
				 HTML_PARSE_NOWARNING /*| HTML_PARSE_NOIMPLIED*/);
    xmlNode *node;
    /*
     * <link/><table></table><a href='/a'>a</a><input type='checkbox'/>
     * -> With HTML_PARSE_NOIMPLIED
     *    <link> - <table>
     *             <a>
     *             <input>
     *    Without HTMLPARSE_NOIMPLIED
     *    <html> - <head> - <link>
     *             <body> - <table>
     *                      <a>
     *                      <input>
     */
    for (node = xmlDocGetRootElement(doc)->children /* head */; node; node = node->next) {
	create_tree(ctx, node->children, JS_DupValue(ctx, parent), 1);
    }
    xmlFreeDoc(doc);
    xmlCleanupParser();
}
#endif

static JSValue
element_text_content_set(JSContext *ctx, JSValueConst jsThis, JSValueConst val)
{
    JSValue p_nodename = JS_GetPropertyStr(ctx, jsThis, "nodeName");
    const char *str = JS_ToCString(ctx, p_nodename);
    char *p;

    init_children(ctx, jsThis, str);
    JS_FreeCString(ctx, str);
    JS_FreeValue(ctx, p_nodename);

#ifdef USE_LIBXML2
#ifdef DOM_DEBUG
    FILE *fp = fopen("domlog.txt", "a");
    fprintf(fp, "=== innerHTML: %s\n", JS_ToCString(ctx, val));
    fclose(fp);
#endif

    str = JS_ToCString(ctx, val);
    if ((p = strchr(str, '<')) && strchr(p + 1, '>')) {
	create_tree_from_html(ctx, jsThis, str);
    } else
#endif
    {
	JSValue nodename = JS_NewStringLen(ctx, "#text", 5);
	JSValue element = element_new(ctx, jsThis, 1, &nodename);

	JS_SetPropertyStr(ctx, element, "nodeValue", JS_DupValue(ctx, val));
	JS_SetPropertyStr(ctx, element, "isModified", JS_TRUE);

	JS_FreeValue(ctx, element_append_child(ctx, jsThis, 1, &element));

	JS_FreeValue(ctx, nodename);
	JS_FreeValue(ctx, element);
    }
#ifdef USE_LIBXML2
    JS_FreeCString(ctx, str);
#endif

    return JS_UNDEFINED;
}

static JSValue
contains(JSContext *ctx, JSValueConst jsThis, JSValueConst other)
{
    JSValue children = JS_GetPropertyStr(ctx, jsThis, "childNodes");
    int i;
    JSValue ret = JS_FALSE;

    for (i = 0; ;i++) {
	JSValue child = JS_GetPropertyUint32(ctx, children, i);
	if (!JS_IsObject(child)) {
	    break;
	}
	/* XXX */
	if (JS_VALUE_GET_PTR(other) == JS_VALUE_GET_PTR(child)) {
	    ret = JS_TRUE;
	} else {
	    ret = contains(ctx, child, other);
	}

	JS_FreeValue(ctx, child);

	if (JS_VALUE_GET_BOOL(ret) == JS_VALUE_GET_BOOL(JS_TRUE)) {
	    break;
	}
    }

    JS_FreeValue(ctx, children);

    return ret;
}

static JSValue
element_contains(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    if (argc < 1) {
	return JS_EXCEPTION;
    }

    return contains(ctx, jsThis, argv[0]);
}

static JSValue
element_insert_adjacent_html(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
#ifdef USE_LIBXML2
    const char *pos;
    const char *html;
    JSValue parent;

    if (argc < 2 || (pos = JS_ToCString(ctx, argv[0])) == NULL) {
	return JS_EXCEPTION;
    }
    if (strcmp(pos, "beforebegin") == 0 || strcmp(pos, "afterend") == 0) {
	parent = JS_GetPropertyStr(ctx, jsThis, "parentElement"); /* XXX */
    } else if (strcmp(pos, "afterbegin") == 0 || strcmp(pos, "beforeend") == 0) {
	parent = JS_DupValue(ctx, jsThis); /* XXX */
    } else {
	JS_FreeCString(ctx, pos);
	return JS_EXCEPTION;
    }

    html = JS_ToCString(ctx, argv[1]);
    if (html != NULL) {
	create_tree_from_html(ctx, parent, html);
	JS_FreeCString(ctx, html);
    }
    JS_FreeValue(ctx, parent);
    JS_FreeCString(ctx, pos);
#else
    if (argc < 2) {
	return JS_EXCEPTION;
    } else {
	JSValue nodename = JS_NewStringLen(ctx, "#text", 5);
	JSValue element = element_new(ctx, jsThis, 1, &nodename);

	JS_SetPropertyStr(ctx, element, "nodeValue", JS_DupValue(ctx, argv[1]));
	JS_FreeValue(ctx, element_append_child(ctx, jsThis, 1, &element));

	JS_FreeValue(ctx, nodename);
	JS_FreeValue(ctx, element);
    }
#endif

    return JS_UNDEFINED;
}

static JSValue
element_get_attribute_node(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    return JS_NULL;
}

static JSValue
element_child_count_get(JSContext *ctx, JSValueConst jsThis)
{
    const char script[] = "this.children.length;";
    return backtrace(ctx, script,
		     JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1, "<input>", EVAL_FLAG));
}

static JSValue
element_class_name_get(JSContext *ctx, JSValueConst jsThis)
{
    const char script[] = "this.classList.value;";
    return backtrace(ctx, script,
		     JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1, "<input>", EVAL_FLAG));
}

static JSValue
element_class_name_set(JSContext *ctx, JSValueConst jsThis, JSValueConst val)
{
    const char *name = JS_ToCString(ctx, val);
    char *script = Sprintf("this.classList.value = \"%s\";", name)->ptr;
    JS_FreeCString(ctx, name);

    return backtrace(ctx, script,
		     JS_EvalThis(ctx, jsThis, script, strlen(script), "<input>", EVAL_FLAG));
}

static JSValue
element_click(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JS_SetPropertyStr(ctx, jsThis, "checked", JS_TRUE);

    return JS_UNDEFINED;
}

static JSValue
element_focus_or_blur(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    return JS_UNDEFINED;
}

static JSValue
element_select(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    log_msg("XXX: HTMLInputElement.select");
    return JS_UNDEFINED;
}

static JSValue
element_get_context(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    return canvas_rendering_context2d_new(ctx, jsThis, 0, NULL);
}

static JSValue
element_to_data_url(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    log_msg("XXX: HTMLCanvasElement.toDataURL");
    return JS_NewString(ctx, "data:,");
}

static JSValue
element_href_set(JSContext *ctx, JSValueConst jsThis, JSValueConst val)
{
    Str str;
    ParsedURL pu;

    if ((str = get_str(ctx, val)) == NULL) {
	return JS_EXCEPTION;
    }

    parseURL(str->ptr, &pu, NULL);
    JS_SetPropertyStr(ctx, jsThis, "host",
		      JS_NewString(ctx, pu.host ?
				   (pu.has_port ? Sprintf("%s:%d", pu.host, pu.port)->ptr : pu.host) : ""));
    JS_SetPropertyStr(ctx, jsThis, "hostname",
		      JS_NewString(ctx, pu.host ? pu.host : ""));
    JS_SetPropertyStr(ctx, jsThis, "hash",
		      JS_NewString(ctx, pu.label ? Sprintf("#%s", pu.label)->ptr : ""));
    JS_SetPropertyStr(ctx, jsThis, "pathname",
		      JS_NewString(ctx, pu.file ? pu.file : ""));
    JS_SetPropertyStr(ctx, jsThis, "search",
		      JS_NewString(ctx, pu.query ? Sprintf("?%s", pu.query)->ptr : ""));
    JS_SetPropertyStr(ctx, jsThis, "protocol", JS_NewString(ctx, get_url_scheme_str(pu.scheme)));
    JS_SetPropertyStr(ctx, jsThis, "port",
		      JS_NewString(ctx, pu.has_port ? Sprintf("%d", pu.port)->ptr : ""));
    JS_SetPropertyStr(ctx, jsThis, "username",
		      JS_NewString(ctx, pu.user ? pu.user : ""));
    JS_SetPropertyStr(ctx, jsThis, "password",
		      JS_NewString(ctx, pu.pass ? pu.pass : ""));

    return JS_UNDEFINED;
}

static JSValue
element_href_get(JSContext *ctx, JSValueConst jsThis)
{
    const char script[] =
	"(this.protocol ? this.protocol + \"//\" : \"\") + "
	"(this.username ? (this.password ? this.username + \":\" + this.password : this.username) "
	                 " + \"@\" : \"\") + "
	"this.host + this.pathname + this.hash + this.search;";

    return backtrace(ctx, script,
		     JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1, "<input>", EVAL_FLAG));
}

static const JSCFunctionListEntry ElementFuncs[] = {
    /* Node (see DocumentFuncs) */
    JS_CGETSET_DEF("ownerDocument", element_owner_document_get, NULL),
    JS_CFUNC_DEF("appendChild", 1, element_append_child),
    JS_CFUNC_DEF("insertBefore", 1, element_append_child), /* XXX */
    JS_CFUNC_DEF("removeChild", 1, element_remove_child),
    JS_CFUNC_DEF("replaceChild", 1, element_replace_child),
    JS_CFUNC_DEF("hasChildNodes", 1, element_has_child_nodes),
    JS_CGETSET_DEF("firstChild", element_first_child_get, NULL),
    JS_CGETSET_DEF("lastChild", element_last_child_get, NULL),
    JS_CGETSET_DEF("nextSibling", element_next_sibling_get, NULL),
    JS_CGETSET_DEF("previousSibling", element_previous_sibling_get, NULL),
    JS_CFUNC_DEF("compareDocumentPosition", 1, element_compare_document_position),
    JS_CFUNC_DEF("cloneNode", 1, element_clone_node),
    JS_CGETSET_DEF("textContent", element_text_content_get, element_text_content_set),
    JS_CFUNC_DEF("contains", 1, element_contains),

    /* EventTarget */
    JS_CFUNC_DEF("addEventListener", 1, add_event_listener),
    JS_CFUNC_DEF("removeEventListener", 1, remove_event_listener),
    JS_CFUNC_DEF("dispatchEvent", 1, dispatch_event),

    /* Element */
    JS_CGETSET_DEF("firstElementChild", element_first_child_get, NULL),
    JS_CGETSET_DEF("lastElementChild", element_last_child_get, NULL),
    JS_CFUNC_DEF("closest", 1, element_closest),
    JS_CFUNC_DEF("setAttribute", 1, element_set_attribute),
    JS_CFUNC_DEF("getAttribute", 1, element_get_attribute),
    JS_CFUNC_DEF("removeAttribute", 1, element_remove_attribute),
    JS_CFUNC_DEF("hasAttribute", 1, element_has_attribute),
    JS_CGETSET_DEF("attributes", element_attributes_get, NULL),
    JS_CFUNC_DEF("matches", 1, element_matches),
    JS_CFUNC_DEF("getBoundingClientRect", 1, element_get_bounding_client_rect),
    JS_CFUNC_DEF("getClientRects", 1, element_get_client_rects),
    JS_CFUNC_DEF("getElementsByTagName", 1, element_get_elements_by_tag_name),
    JS_CFUNC_DEF("getElementsByTagNameNS", 1, element_get_elements_by_tag_name),
    JS_CFUNC_DEF("getElementsByClassName", 1, element_get_elements_by_class_name),
    JS_CGETSET_DEF("innerHTML", element_text_content_get, element_text_content_set),
    JS_CFUNC_DEF("insertAdjacentHTML", 1, element_insert_adjacent_html),
    JS_CFUNC_DEF("getAttributeNode", 1, element_get_attribute_node),
    JS_CGETSET_DEF("childElementCount", element_child_count_get, NULL),
    JS_CGETSET_DEF("className", element_class_name_get, element_class_name_set),
    JS_CFUNC_DEF("remove", 1, element_remove),

    /* HTMLElement */
    JS_CFUNC_DEF("doScroll", 1, element_do_scroll), /* XXX Obsolete API */
    JS_CGETSET_DEF("offsetParent", element_offset_parent_get, NULL),
    JS_CGETSET_DEF("innerText", element_text_content_get, element_text_content_set),
    JS_CFUNC_DEF("click", 1, element_click),
    JS_CFUNC_DEF("focus", 1, element_focus_or_blur),
    JS_CFUNC_DEF("blur", 1, element_focus_or_blur),

    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "HTMLElement", JS_PROP_CONFIGURABLE),

    /* XXX HTMLAnchorElement */
    JS_CGETSET_DEF("href", element_href_get, element_href_set),

    /* XXX HTMLInputElement */
    JS_CFUNC_DEF("select", 1, element_select),

    /* XXX HTMLIFrameElement only */
    JS_CGETSET_DEF("contentWindow", element_content_window_get, NULL),

    /* XXX HTMLCanvasElement only */
    JS_CFUNC_DEF("getContext", 1, element_get_context),
    JS_CFUNC_DEF("toDataURL", 1, element_to_data_url),

    /* XXX HTMLFormElement */
    JS_CFUNC_DEF("submit", 1, html_form_element_submit),
    JS_CFUNC_DEF("reset", 1, html_form_element_reset),
};

static JSValue
element_set_attribute(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    if (argc < 2) {
	return JS_EXCEPTION;
    }

    if (JS_IsString(argv[0])) {
	const char *key = JS_ToCString(ctx, argv[0]);
	size_t i;

	for (i = 0; i < sizeof(ElementFuncs) / sizeof(ElementFuncs[0]); i++) {
	    if (strcmp(key, ElementFuncs[i].name) == 0) {
		if (ElementFuncs[i].def_type == JS_DEF_CGETSET) {
		    (*ElementFuncs[i].u.getset.set.setter)(ctx, jsThis,
							   JS_DupValue(ctx, argv[1]));
		    goto end;
		}
	    }
	}

	JS_SetPropertyStr(ctx, jsThis, key, JS_DupValue(ctx, argv[1]));

    end:
	JS_FreeCString(ctx, key);
    }

    return JS_UNDEFINED;
}

static JSValue
element_get_attribute(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    if (argc < 1) {
	return JS_EXCEPTION;
    }

    if (JS_IsString(argv[0])) {
	const char *key = JS_ToCString(ctx, argv[0]);
	size_t i;
	JSValue prop;

	for (i = 0; i < sizeof(ElementFuncs) / sizeof(ElementFuncs[0]); i++) {
	    if (strcmp(key, ElementFuncs[i].name) == 0) {
		if (ElementFuncs[i].def_type == JS_DEF_CGETSET) {
		    prop = (*ElementFuncs[i].u.getset.get.getter)(ctx, jsThis);
		    goto end;
		}
	    }
	}

	prop = JS_GetPropertyStr(ctx, jsThis, key);

    end:
	JS_FreeCString(ctx, key);

	if (!JS_IsUndefined(prop)) {
	    return prop;
	} else {
	    JS_FreeValue(ctx, prop);
	}
    }

    return JS_NULL;
}

static void
history_final(JSRuntime *rt, JSValue val) {
    GC_FREE(JS_GetOpaque(val, HistoryClassID));
}

static const JSClassDef HistoryClass = {
    "History", history_final, NULL /* gc_mark */, NULL /* call */, NULL
};

static JSValue
history_new(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue obj;
    HistoryState *state;

    obj = JS_NewObjectClass(ctx, HistoryClassID);

    state = (HistoryState *)GC_MALLOC_UNCOLLECTABLE(sizeof(HistoryState));
    state->pos = 0;

    JS_SetOpaque(obj, state);

    return obj;
}

static JSValue
history_back(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    HistoryState *state = JS_GetOpaque(jsThis, HistoryClassID);

    state->pos = -1;

    return JS_UNDEFINED;
}

static JSValue
history_forward(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    HistoryState *state = JS_GetOpaque(jsThis, HistoryClassID);

    state->pos = 1;

    return JS_UNDEFINED;
}

static JSValue
history_go(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    HistoryState *state = JS_GetOpaque(jsThis, HistoryClassID);
    int i;

    if (argc < 1 || !JS_IsNumber(argv[0])) {
	return JS_EXCEPTION;
    }

    JS_ToInt32(ctx, &i, argv[0]);
    state->pos = i;

    return JS_UNDEFINED;
}

static JSValue
history_push_state(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    log_msg("XXX: History.pushState");
    return JS_UNDEFINED;
}

static JSValue
history_replace_state(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    log_msg("XXX: History.replaceState");
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry HistoryFuncs[] = {
    JS_CFUNC_DEF("back", 1, history_back),
    JS_CFUNC_DEF("forward", 1, history_forward),
    JS_CFUNC_DEF("go", 1, history_go),
    JS_CFUNC_DEF("pushState", 1, history_push_state),
    JS_CFUNC_DEF("replaceState", 1, history_replace_state),

    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "History", JS_PROP_CONFIGURABLE),
};

static void
navigator_final(JSRuntime *rt, JSValue val) {
    GC_FREE(JS_GetOpaque(val, NavigatorClassID));
}

static const JSClassDef NavigatorClass = {
    "Navigator", navigator_final, NULL /* gc_mark */, NULL /* call */, NULL
};

static JSValue
navigator_new(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue obj = JS_NewObjectClass(ctx, NavigatorClassID);

    JS_SetPropertyStr(ctx, obj, "product", JS_NewString(ctx, "Gecko")); /* XXX Deprecated */

    return obj;
}

static char *
user_agent(void)
{
    return (UserAgent && *UserAgent) ? UserAgent : w3m_version;
}

static JSValue
navigator_appcodename_get(JSContext *ctx, JSValueConst jsThis)
{
    char *name = user_agent();
    char *p;

    if ((p = strchr(name, '/'))) {
	return JS_NewStringLen(ctx, name, p - name);
    } else {
	return JS_NewString(ctx, name);
    }
}

static JSValue
navigator_appname_get(JSContext *ctx, JSValueConst jsThis)
{
    if (strncasecmp(user_agent(), "Mozilla", 7) == 0) {
	return JS_NewString(ctx, "Netscape");
    } else {
	return JS_NewString(ctx, "w3m");
    }
}

static JSValue
navigator_appversion_get(JSContext *ctx, JSValueConst jsThis)
{
    char *ua = user_agent();
    char *version;
    struct utsname unamebuf;
#if LANG == JA
    const char *lang = "ja-JP";
#else
    const char *lang = "en-US";
#endif
    char *platform;
    int n;

    if ((version = strchr(ua, '/'))) {
	version++;
	if (*version) {
	    goto end;
	}
    }

    if (uname(&unamebuf) == -1) {
	platform = "Unknown";
    } else {
	platform = unamebuf.sysname;
    }

    if (version && (n = strspn(version, "0123456789.")) > 0) {
	Str rnum = Strnew();
	Strcopy_charp_n(rnum, version, n);
	version = Sprintf("%s (%s; %s)", rnum->ptr, platform, lang)->ptr;
	Strfree(rnum);
    } else {
	version = Sprintf("(%s; %s)", platform, lang)->ptr;
    }

 end:
    return JS_NewString(ctx, version);
}

static JSValue
navigator_language_get(JSContext *ctx, JSValueConst jsThis)
{
#if LANG == JA
    const char lang[] = "ja-JP";
#else
    const char lang[] = "en-US";
#endif

    return JS_NewStringLen(ctx, lang, sizeof(lang) - 1);
}

static JSValue
navigator_vendor_get(JSContext *ctx, JSValueConst jsThis)
{
    if (strncasecmp(user_agent(), "Mozilla", 7) == 0) {
	return JS_NewString(ctx, "Google Inc,");
    } else {
	return JS_NewString(ctx, "w3m");
    }
}

static JSValue
navigator_online_get(JSContext *ctx, JSValueConst jsThis)
{
    return JS_TRUE;
}

static JSValue
navigator_useragent_get(JSContext *ctx, JSValueConst jsThis)
{
    return JS_NewString(ctx, user_agent());
}

static JSValue
navigator_cookieenabled_get(JSContext *ctx, JSValueConst jsThis)
{
    if (use_cookie) {
	return JS_TRUE;
    } else {
	return JS_FALSE;
    }
}

static JSValue
navigator_send_beacon(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    log_msg("XXX: Navigator.sendBeacon");

    return JS_UNDEFINED;
}

static const JSCFunctionListEntry NavigatorFuncs[] = {
    JS_CGETSET_DEF("appCodeName", navigator_appcodename_get, NULL),
    JS_CGETSET_DEF("appName", navigator_appname_get, NULL),
    JS_CGETSET_DEF("appVersion", navigator_appversion_get, NULL),
    JS_CGETSET_DEF("userAgent", navigator_useragent_get, NULL),
    JS_CGETSET_DEF("cookieEnabled", navigator_cookieenabled_get, NULL),
    JS_CGETSET_DEF("language", navigator_language_get, NULL),
    JS_CGETSET_DEF("vendor", navigator_vendor_get, NULL),
    JS_CGETSET_DEF("onLine", navigator_online_get, NULL),
    JS_CFUNC_DEF("sendBeacon", 1, navigator_send_beacon),

    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Navigator", JS_PROP_CONFIGURABLE),
};

static void
document_final(JSRuntime *rt, JSValue val) {
    GC_FREE(JS_GetOpaque(val, DocumentClassID));
}

static const JSClassDef DocumentClass = {
    "Document", document_final, NULL /* gc_mark */, NULL /* call */, NULL
};

static JSValue
document_new(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue obj;
    DocumentState *state;

    /* Set properties by script_buf2js() in script.c */
    obj = JS_NewObjectClass(ctx, DocumentClassID);

    state = (DocumentState *)GC_MALLOC_UNCOLLECTABLE(sizeof(DocumentState));
    state->write = NULL;
    state->cookie = NULL;

    JS_SetOpaque(obj, state);

    return obj;
}

static JSValue
document_open(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    DocumentState *state = JS_GetOpaque(jsThis, DocumentClassID);

    state->open = 1;

    return JS_UNDEFINED;
}

static JSValue
document_close(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    log_msg("XXX: Document.close");
    return JS_UNDEFINED;
}

static JSValue
document_write(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    DocumentState *state = JS_GetOpaque(jsThis, DocumentClassID);
    int i;

    if (! state->write)
	state->write = Strnew();

    for (i = 0; i < argc; i++) {
	if (JS_IsString(argv[i])) {
	    const char *str;
	    size_t len;

	    str = JS_ToCStringLen(ctx, &len, argv[i]);
	    Strcat_charp_n(state->write, str, len);
	    JS_FreeCString(ctx, str);
	} else if (JS_IsBool(argv[i])) {
	    Strcat_charp(state->write, JS_ToBool(ctx, argv[i]) ? "true" : "false");
	} else if (JS_IsNumber(argv[i])) {
	    int j;

	    JS_ToInt32(ctx, &j, argv[i]);
	    Strcat(state->write, Sprintf("%d", j));
	}
    }

    return JS_UNDEFINED;
}

static JSValue
document_writeln(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    DocumentState *state = JS_GetOpaque(jsThis, DocumentClassID);

    document_write(ctx, jsThis, argc, argv);
    Strcat_charp(state->write, "\n");

    return JS_UNDEFINED;
}

static JSValue
document_clear(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    log_msg("XXX: Document.clear");
    return JS_UNDEFINED;
}

static JSValue
document_location_get(JSContext *ctx, JSValueConst jsThis)
{
    return JS_Eval(ctx, "location;", 9, "<input>", EVAL_FLAG);
}

static JSValue
document_location_set(JSContext *ctx, JSValueConst jsThis, JSValueConst val)
{
    return location_href_set(ctx, JS_Eval(ctx, "location;", 9, "<input>", EVAL_FLAG), val);
}

static JSValue
document_cookie_get(JSContext *ctx, JSValueConst jsThis)
{
    DocumentState *state = JS_GetOpaque(jsThis, DocumentClassID);

    if (state->cookie) {
	return JS_NewStringLen(ctx, state->cookie->ptr, state->cookie->length);
    } else {
	return JS_NewString(ctx, "");
    }
}

static JSValue
document_cookie_set(JSContext *ctx, JSValueConst jsThis, JSValueConst val)
{
    DocumentState *state = JS_GetOpaque(jsThis, DocumentClassID);
    const char *cookie = JS_ToCString(ctx, val);

    if (cookie != NULL) {
	state->cookie = Strnew_charp(cookie);
	state->cookie_changed = 1;
    }

    return JS_UNDEFINED;
}

static JSValue
document_referrer_get(JSContext *ctx, JSValueConst jsThis)
{
    char *url;

    if (CurrentTab && Currentbuf && Currentbuf->nextBuffer) {
	url = parsedURL2Str(&Currentbuf->nextBuffer->currentURL)->ptr;
    } else {
	url = "";
    }

    return JS_NewString(ctx, url);
}

static JSValue
document_has_focus(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    return JS_TRUE;
}

static JSValue
document_clone_node(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    if (argc >= 1 && JS_ToBool(ctx, argv[0])) {
	const char script[] =
	    "{"
	    "  let doc = Object.assign(new Document(), this);"
	    "  w3m_initDocumentTree(doc);"
	    "  w3m_cloneNode(doc, this);"
	    "  doc;"
	    "}";
	return backtrace(ctx, script,
			 JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1,
				     "<input>", EVAL_FLAG));
    } else {
	const char script[] =
	    "{"
	    "  let doc = Object.assign(new Document(), this);"
	    "  w3m_initDocumentTree(doc);"
	    "  doc;"
	    "}";
	return backtrace(ctx, script,
			 JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1,
				     "<input>", EVAL_FLAG));
    }
}

static const JSCFunctionListEntry DocumentFuncs[] = {
    /* Node (see ElementFuncs) */
    JS_CGETSET_DEF("ownerDocument", element_owner_document_get, NULL),
    JS_CFUNC_DEF("appendChild", 1, element_append_child),
    JS_CFUNC_DEF("insertBefore", 1, element_append_child), /* XXX */
    JS_CFUNC_DEF("removeChild", 1, element_remove_child),
    JS_CFUNC_DEF("replaceChild", 1, element_replace_child),
    JS_CFUNC_DEF("hasChildNodes", 1, element_has_child_nodes),
    JS_CGETSET_DEF("firstChild", element_first_child_get, NULL),
    JS_CGETSET_DEF("lastChild", element_last_child_get, NULL),
    JS_CGETSET_DEF("nextSibling", element_next_sibling_get, NULL),
    JS_CGETSET_DEF("previousSibling", element_previous_sibling_get, NULL),
    JS_CFUNC_DEF("compareDocumentPosition", 1, element_compare_document_position),
    JS_CFUNC_DEF("cloneNode", 1, document_clone_node),
    JS_CGETSET_DEF("textContent", element_text_content_get, element_text_content_set),
    JS_CFUNC_DEF("contains", 1, element_contains),

    /* EventTarget */
    JS_CFUNC_DEF("addEventListener", 1, add_event_listener),
    JS_CFUNC_DEF("removeEventListener", 1, remove_event_listener),
    JS_CFUNC_DEF("dispatchEvent", 1, dispatch_event),

    /* Document */
    JS_CFUNC_DEF("open", 1, document_open),
    JS_CFUNC_DEF("close", 1, document_close),
    JS_CFUNC_DEF("write", 1, document_write),
    JS_CFUNC_DEF("writeln", 1, document_writeln),
    JS_CFUNC_DEF("clear", 1, document_clear), /* deprecated. do nothing */
    JS_CGETSET_DEF("location", document_location_get, document_location_set),
    JS_CGETSET_DEF("cookie", document_cookie_get, document_cookie_set),
    JS_CGETSET_DEF("referrer", document_referrer_get, NULL),
    JS_CFUNC_DEF("hasFocus", 1, document_has_focus),

    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "HTMLDocument", JS_PROP_CONFIGURABLE),
};

static void
log_to_file(JSContext *ctx, const char *head, int argc, JSValueConst *argv)
{
    FILE *fp;

    if ((fp = fopen("scriptlog.txt", "a")) != NULL) {
	int i;

	fwrite(head, strlen(head), 1, fp);

	for (i = 0; i < argc; i++) {
	    const char *str = JS_ToCString(ctx, argv[i]);
	    fwrite(" ", 1, 1, fp);
	    JS_FreeCString(ctx, str);
	    fwrite(str, strlen(str), 1, fp);
	}
	fwrite("\n", 1, 1, fp);

	fclose(fp);
    }
}

static JSValue
console_error(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    log_to_file(ctx, "ERROR:", argc, argv);

    return JS_UNDEFINED;
}

static JSValue
console_log(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    log_to_file(ctx, "LOG:", argc, argv);

    return JS_UNDEFINED;
}

static JSValue
console_warn(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    log_to_file(ctx, "WARN:", argc, argv);

    return JS_UNDEFINED;
}

static JSValue
console_info(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    log_to_file(ctx, "INFO:", argc, argv);

    return JS_UNDEFINED;
}

static void
xml_http_request_final(JSRuntime *rt, JSValue val) {
    GC_FREE(JS_GetOpaque(val, XMLHttpRequestClassID));
}

static const JSClassDef XMLHttpRequestClass = {
    "XMLHttpRequest", xml_http_request_final, NULL /* gc_mark */, NULL /* call */, NULL
};

static JSValue
xml_http_request_new(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue obj;
    XMLHttpRequestState *state;

    obj = JS_NewObjectClass(ctx, XMLHttpRequestClassID);
    JS_SetPropertyStr(ctx, obj, "readyState", JS_NewInt32(ctx, 0));

    state = (XMLHttpRequestState *)GC_MALLOC_UNCOLLECTABLE(sizeof(XMLHttpRequestState));
    JS_SetOpaque(obj, state);

    return obj;
}

static JSValue
xml_http_request_open(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    XMLHttpRequestState *state = JS_GetOpaque(jsThis, XMLHttpRequestClassID);
    const char *str;
    size_t len;

    if (argc < 2) {
	return JS_EXCEPTION;
    }

    str = JS_ToCString(ctx, argv[0]);
    if (str == NULL) {
	goto error;
    } else if (strcasecmp(str, "GET") == 0) {
	state->method = FORM_METHOD_GET;
    } else if (strcasecmp(str, "POST") == 0) {
	state->method = FORM_METHOD_POST;
    } else {
	goto error;
    }
    JS_FreeCString(ctx, str);

    str = JS_ToCStringLen(ctx, &len, argv[1]);
    state->request = allocStr(str, len);
    JS_FreeCString(ctx, str);

    state->extra_headers = newTextList();
    state->override_content_type = override_content_type;
    state->override_user_agent = override_user_agent;
    state->response_headers = NULL;

    JS_SetPropertyStr(ctx, jsThis, "readyState", JS_NewInt32(ctx, 1));

    return JS_UNDEFINED;

error:
    log_msg("XMLHttpRequest: Unknown method");
    JS_FreeCString(ctx, str);
    return JS_EXCEPTION;
}

static JSValue
xml_http_request_add_event_listener(JSContext *ctx, JSValueConst jsThis, int argc,
				    JSValueConst *argv)
{
    const char *str;

    if (argc < 2) {
	return JS_EXCEPTION;
    }

    str = JS_ToCString(ctx, argv[0]);
    JS_SetPropertyStr(ctx, jsThis, str, JS_DupValue(ctx, argv[1]));
    JS_FreeCString(ctx, str);

    return JS_UNDEFINED;
}

static JSValue
xml_http_request_set_request_header(JSContext *ctx, JSValueConst jsThis,
				    int argc, JSValueConst *argv)
{
    XMLHttpRequestState *state = JS_GetOpaque(jsThis, XMLHttpRequestClassID);
    const char *header;

    if (argc < 2) {
	return JS_EXCEPTION;
    }

    header = JS_ToCString(ctx, argv[0]);
    if (header != NULL) {
	const char *value = JS_ToCString(ctx, argv[1]);
	if (value != NULL) {
	    if (strcasecmp(header, "Content-type") == 0) {
		state->override_content_type = TRUE;
	    } else if (strcasecmp(header, "User-agent") == 0) {
		state->override_user_agent = TRUE;
	    }

	    pushValue((GeneralList*)state->extra_headers,
		      Sprintf("%s: %s\r\n", header, value)->ptr);

	    JS_FreeCString(ctx, value);
	}
	JS_FreeCString(ctx, header);
    }

    return JS_UNDEFINED;
}

extern int http_response_code;

static char *
get_suffix(char *encoding)
{
    if (encoding != NULL) {
	if (strcasestr(encoding, "gzip")) {
	    return ".gz";
	} else if (strcasestr(encoding, "compress")) {
	    return ".Z";
	} else if (strcasestr(encoding, "bzip")) {
	    return ".bz2";
	} else if (strcasestr(encoding, "deflate")) {
	    return ".deflate";
	} else if (strcasestr(encoding, "br")) {
	    return ".br";
	}
    }

    return NULL;
}

static JSValue
xml_http_request_send(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    XMLHttpRequestState *state = JS_GetOpaque(jsThis, XMLHttpRequestClassID);
    ParsedURL pu;
    URLOption option;
    URLFile uf, *ouf = NULL;
    HRequest hr;
    unsigned char status = HTST_NORMAL;
    CtxState *ctxstate = JS_GetContextOpaque(ctx);
    Buffer *header_buf;
    char *suffix;
    Str str;
    JSValue response;
    char *ctype;
    const char *beg_events[] = { "loadstart", "onloadstart" };
    const char *end_events[] = { "progress", "load", "onload", "loadend", "onloadend", "onreadystatechange" };
    int i;
    FormList *request = NULL;
    int orig_ct;
    int orig_ua;
    JSValue func;

    if (state->method == FORM_METHOD_POST && argc >= 1) {
	const char *cstr;
	size_t len;

	cstr = JS_ToCStringLen(ctx, &len, argv[0]);
	if (cstr == NULL) {
	    return JS_EXCEPTION;
	}

	request = newFormList(NULL, "post", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	request->body = allocStr(cstr, len);
	request->length = strlen(request->body);
	JS_FreeCString(ctx, cstr);
    }

    option.referer = NULL;
    option.flag = 0;

    parseURL2(state->request, &pu, &ctxstate->buf->currentURL);

    orig_ct = override_content_type;
    orig_ua = override_user_agent;
    override_content_type = state->override_content_type;
    override_user_agent = state->override_user_agent;

    for (i = 0; i < sizeof(beg_events) / sizeof(beg_events[0]); i++) {
	func = JS_GetPropertyStr(ctx, jsThis, beg_events[i]);
	if (!JS_IsFunction(ctx, func)) {
	    JSValue func2 = JS_GetPropertyStr(ctx, func, "handleEvent");
	    JS_FreeValue(ctx, func);
	    func = func2;
	}
	if (JS_IsFunction(ctx, func)) {
	    JS_FreeValue(ctx, JS_Call(ctx, func, jsThis, 0, NULL));
	}
	JS_FreeValue(ctx, func);
    }

    uf = openURL(state->request, &pu, &ctxstate->buf->currentURL, &option, request,
		 state->extra_headers, ouf, &hr, &status);

    override_content_type = orig_ct;
    override_user_agent = orig_ua;

    header_buf = newBuffer(INIT_BUFFER_WIDTH);
    readHeader(&uf, header_buf, FALSE, &pu);
    JS_SetPropertyStr(ctx, jsThis, "status", JS_NewInt32(ctx, http_response_code));
    state->response_headers = header_buf->document_header;

    suffix = get_suffix(checkHeader(header_buf, "Content-encoding"));
    if (suffix != NULL) {
	char *tmpf = tmpfname(TMPF_DFL, suffix)->ptr;
	save2tmp(uf, tmpf);
	ISclose(uf.stream);
	if (!examineFile2(tmpf, &uf)) {
	    goto end;
	}
    }

    str = Strnew();
    while (1) {
	char buf[1024];
	int len;
	len = ISread_n(uf.stream, buf, sizeof(buf));
	if (len <= 0) {
	    break;
	}
	Strcat_charp_n(str, buf, len);
    }
    ISclose(uf.stream);

#if 0
    {
	FILE *fp = fopen("scriptlog.txt", "a");
	TextListItem *i;

	fprintf(fp, "====> RESPONSE %d from %s:\n",
		http_response_code, parsedURL2Str(&ctxstate->buf->currentURL)->ptr);
	for (i = state->response_headers->first; i != NULL; i = i->next) {
	    fprintf(fp, i->ptr);
	    fprintf(fp, "\n");
	}
	fprintf(fp, str->ptr);
	fprintf(fp, "====\n");
	fclose(fp);
    }
#endif

    /* XXX should be the final URL obtained after any redirects. */
    JS_SetPropertyStr(ctx, jsThis, "responseURL",
		      JS_NewString(ctx, parsedURL2Str(&ctxstate->buf->currentURL)->ptr));

    response = JS_NewStringLen(ctx, str->ptr, str->length);
    JS_SetPropertyStr(ctx, jsThis, "responseText", response);

    ctype = checkHeader(header_buf, "Content-Type");
    if (ctype != NULL && strcasestr(ctype, "application/json")) {
	JSValue json = JS_ParseJSON(ctx, str->ptr, str->length, NULL);
	JS_SetPropertyStr(ctx, jsThis, "response", json);
	JS_SetPropertyStr(ctx, jsThis, "responseType", JS_NewString(ctx, "json"));
    } else {
	JS_SetPropertyStr(ctx, jsThis, "response", JS_DupValue(ctx, response)); /* XXX */
	JS_SetPropertyStr(ctx, jsThis, "responseType", JS_NewString(ctx, "text"));
    }

    JS_SetPropertyStr(ctx, jsThis, "readyState", JS_NewInt32(ctx, 4));

end:
    for (i = 0; i < sizeof(end_events) / sizeof(end_events[0]); i++) {
	func = JS_GetPropertyStr(ctx, jsThis, end_events[i]);
	if (!JS_IsFunction(ctx, func)) {
	    JSValue func2 = JS_GetPropertyStr(ctx, func, "handleEvent");
	    JS_FreeValue(ctx, func);
	    func = func2;
	}
	if (JS_IsFunction(ctx, func)) {
	    JS_FreeValue(ctx, JS_Call(ctx, func, jsThis, 0, NULL));
	}
	JS_FreeValue(ctx, func);
    }

    return JS_UNDEFINED;
}

static JSValue
xml_http_request_abort(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    log_msg("XXX: XMLHttpRequest.abort");
    return JS_UNDEFINED;
}

static JSValue
xml_http_request_get_all_response_headers(JSContext *ctx, JSValueConst jsThis, int argc,
					  JSValueConst *argv)
{
    XMLHttpRequestState *state = JS_GetOpaque(jsThis, XMLHttpRequestClassID);
    Str headers = Strnew();
    TextListItem *i;

    for (i = state->response_headers->first; i != NULL; i = i->next) {
	Strcat_charp(headers, i->ptr);
	Strcat_charp(headers, "\r\n");
    }

    return JS_NewString(ctx, headers->ptr);
}

static JSValue
xml_http_request_get_response_header(JSContext *ctx, JSValueConst jsThis, int argc,
				     JSValueConst *argv)
{
    XMLHttpRequestState *state;
    const char *name;
    char *value;
    TextListItem *i;

    if (argc < 1) {
	return JS_EXCEPTION;
    }

    name = JS_ToCString(ctx, argv[0]);
    value = NULL;
    if (name != NULL) {
	state = JS_GetOpaque(jsThis, XMLHttpRequestClassID);

	for (i = state->response_headers->first; i != NULL; i = i->next) {
	    size_t len = strlen(name);
	    if (strncasecmp(i->ptr, name, len) == 0) {
		value = i->ptr + len;
		if (*value == ':') value++;
		while (*value == ' ' || *value == '\t') value++;
		break;
	    }
	}
	JS_FreeCString(ctx, name);
    }

    if (value != NULL) {
	return JS_NewString(ctx, value);
    } else {
	return JS_NULL;
    }
}

static const JSCFunctionListEntry XMLHttpRequestFuncs[] = {
    JS_CFUNC_DEF("open", 1, xml_http_request_open),
    JS_CFUNC_DEF("addEventListener", 1, xml_http_request_add_event_listener),
    JS_CFUNC_DEF("setRequestHeader", 1, xml_http_request_set_request_header),
    JS_CFUNC_DEF("send", 1, xml_http_request_send),
    JS_CFUNC_DEF("abort", 1, xml_http_request_abort),
    JS_CFUNC_DEF("getResponseHeader", 1, xml_http_request_get_response_header),
    JS_CFUNC_DEF("getAllResponseHeaders", 1, xml_http_request_get_all_response_headers),

    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "XMLHttpRequest", JS_PROP_CONFIGURABLE),
};

static struct {
    char *name;
    JSCFunction *func;
} ConsoleFuncs[] = {
    { "error", console_error } ,
    { "log", console_log },
    { "warn", console_warn },
    { "info", console_info },
};

static struct interval_callback {
    JSContext *ctx;
    JSValue func;
    int delay;
    int cur_delay;
    JSValue *argv;
    int argc;
    int is_timeout;
} *interval_callbacks;
static u_int num_interval_callbacks;

static JSValue
set_interval_intern(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv,
		    int is_timeout)
{
    int i;
    int idx;
    void *p;

    if (argc < 1) {
	return JS_EXCEPTION;
    }

    if (!JS_IsFunction(ctx, argv[0])) {
	log_msg("setInterval: not a function");
	return JS_UNDEFINED;
    }

    for (i = 0; i < num_interval_callbacks; i++) {
	if (interval_callbacks[i].ctx == NULL) {
	    idx = i;
	    goto set_callback;
	}
    }

    p = realloc(interval_callbacks, sizeof(*interval_callbacks) * (num_interval_callbacks + 1));
    if (p == NULL) {
	return JS_UNDEFINED;
    }
    interval_callbacks = p;
    idx = num_interval_callbacks++;

 set_callback:
    interval_callbacks[idx].ctx = ctx;
    interval_callbacks[idx].func = JS_DupValue(ctx, argv[0]);

    if (argc >= 2) {
	JS_ToInt32(ctx, &interval_callbacks[idx].cur_delay, argv[1]);
	if (interval_callbacks[idx].cur_delay == 0) {
	    interval_callbacks[idx].cur_delay = 1000;
	}

	if (argc >= 3 &&
	    (interval_callbacks[idx].argv = malloc(sizeof(JSValue) * (argc - 2))) != NULL) {
	    for (i = 2; i < argc; i++) {
		interval_callbacks[idx].argv[i - 2] = JS_DupValue(ctx, argv[i]);
	    }
	    interval_callbacks[idx].argc = argc - 2;
	} else {
	    interval_callbacks[idx].argc = 0;
	    interval_callbacks[idx].argv = NULL;
	}
    } else {
	interval_callbacks[idx].cur_delay = 1000;
	interval_callbacks[idx].argc = 0;
	interval_callbacks[idx].argv = NULL;
    }

    if (is_timeout) {
	interval_callbacks[idx].delay = 0;
    } else {
	interval_callbacks[idx].delay = interval_callbacks[idx].cur_delay;
    }

    return JS_NewInt32(ctx, idx + 1);
}

static JSValue
set_interval(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    return set_interval_intern(ctx, jsThis, argc, argv, 0);
}

static JSValue
set_timeout(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    return set_interval_intern(ctx, jsThis, argc, argv, 1);
}

static void
destroy_interval_callback(struct interval_callback *cb)
{
    int i;

    for (i = 0; i < cb->argc; i++) {
	JS_FreeValue(cb->ctx, cb->argv[i]);
    }
    cb->argv = NULL; /* for garbage collector */
    JS_FreeValue(cb->ctx, cb->func);
    cb->ctx = NULL;
}

static JSValue
clear_interval(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    int idx;

    if (argc < 1) {
	return JS_EXCEPTION;
    }

    JS_ToInt32(ctx, &idx, argv[0]);
    idx --;

    if (0 <= idx && idx < num_interval_callbacks && interval_callbacks[idx].ctx != NULL) {
	destroy_interval_callback(interval_callbacks + idx);
    }

    return JS_UNDEFINED;
}

void trigger_interval(int sec)
{
    int i;
    int msec;
    int num = num_interval_callbacks;

    msec = sec * 1000;
    for (i = 0; i < num; i++) {
	if (interval_callbacks[i].ctx != NULL) {
	    if (interval_callbacks[i].cur_delay > msec) {
		interval_callbacks[i].cur_delay -= msec;
	    } else {
		JSValue val = JS_Call(interval_callbacks[i].ctx,
				      interval_callbacks[i].func,
				      JS_NULL, interval_callbacks[i].argc,
				      interval_callbacks[i].argv);

		/* interval_callbacks[i] can be destroyed in JS_Call() above. */
		if (interval_callbacks[i].ctx != NULL) {
		    JS_FreeValue(interval_callbacks[i].ctx, val);
		    if (interval_callbacks[i].delay > 0) {
			/* interval */
			interval_callbacks[i].cur_delay = interval_callbacks[i].delay;
		    } else {
			/* timeout */
			destroy_interval_callback(interval_callbacks + i);
		    }
		} else {
		    /* XXX val is leaked */
		}
	    }
	}
    }
}

static void
create_class(JSContext *ctx, JSValue gl, JSClassID *id, char *name,
	     const JSCFunctionListEntry *funcs, int num_funcs, const JSClassDef *class,
	     JSValue (*ctor_func)(JSContext *, JSValueConst, int, JSValueConst *))
{
    JSValue proto;
    JSValue ctor;

    if (*id == 0) {
	JS_NewClassID(id);
	JS_NewClass(rt, *id, class);
    }
    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, funcs, num_funcs);
    JS_SetClassProto(ctx, *id, proto);
    ctor = JS_NewCFunction2(ctx, ctor_func, name, 1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, ctor, proto);
    JS_SetPropertyStr(ctx, gl, name, ctor);
}

JSContext *
js_html_init(Buffer *buf)
{
    JSContext *ctx;
    JSValue gl;
    CtxState *ctxstate;
    int i;
    WindowState *state;
    JSValue console;
    const char script[] =
	"var tostring = Object.prototype.toString;"
	"Object.prototype.toString = function() {"
	"  if (this == window) {"
	"    return \"[object Window]\";"
	"  } else {"
	"    return tostring.call(this);"
	"  }"
	"};"
	""
	"globalThis.HTMLCollection = class HTMLCollection extends Array {"
	"  item(index) {"
	"    if (index < 0 || this.length <= index) {"
	"      return null;"
	"    } else {"
	"      return this[index];"
	"    }"
	"  }"
	"};"
	"globalThis.NodeList = class NodeList extends HTMLCollection {};"
	"globalThis.FileList = class FileList extends HTMLCollection {};"
	"globalThis.CSSRuleList = class FileList extends HTMLCollection {};"
	"globalThis.Event = class Event {"
	"  constructor(type) {"
	"    this.type = type;"
	"  }"
	"};"
	""
	"globalThis.NodeFilter = class NodeFilter {"
	"  static FILTER_ACCEPT               = 1;"
	"  static FILTER_REJECT               = 2;"
	"  static FILTER_SKIP                 = 3;"
	"  static SHOW_ALL                    = 0xFFFFFFFF;"
	"  static SHOW_ELEMENT                = 0x00000001;"
	"  static SHOW_ATTRIBUTE              = 0x00000002;"
	"  static SHOW_TEXT                   = 0x00000004;"
	"  static SHOW_CDATA_SECTION          = 0x00000008;"
	"  static SHOW_ENTITY_REFERENCE       = 0x00000010;"
	"  static SHOW_ENTITY                 = 0x00000020;"
	"  static SHOW_PROCESSING_INSTRUCTION = 0x00000040;"
	"  static SHOW_COMMENT                = 0x00000080;"
	"  static SHOW_DOCUMENT               = 0x00000100;"
	"  static SHOW_DOCUMENT_TYPE          = 0x00000200;"
	"  static SHOW_DOCUMENT_FRAGMENT      = 0x00000400;"
	"  static SHOW_NOTATION               = 0x00000800;"
	"};"
	""
        "globalThis.NodeIterator = class NodeIterator {"
	"  constructor(root, ...args) {"
	"    this.next = root;"
	"    this.previous = root;"
	"    /* XXX Ignore args[0] (whatToShow) for now */ "
	"    if (args.length > 1) {"
	"      this.filter = args[1];"
	"    } else {"
	"      this.filter = null;"
	"    }"
	"  }"
	"  nextNode() {"
	"    let ret;"
	"    while (true) {"
	"      ret = this.next;"
	"      if (this.next.children.length > 0) {"
	"        this.next = this.next.children[0];"
	"      } else if (this.next.nextSibling) {"
	"        this.next = this.next.nextSibling;"
	"      } else {"
	"        this.next = this.next.parentNode;"
	"      }"
	"      if (!ret || !this.filter || typeof this.filter !== \"function\" ||"
	"          this.filter(ret) == NodeFilter.FILTER_ACCEPT) {"
	"        break;"
	"      }"
	"    }"
	"    return ret;"
	"  }"
	"};"
	""
	"globalThis.DOMTokenList = class DOMTokenList extends Array {"
	"  get value() {"
	"    return this.join(\" \");"
	"  }"
	"  set value(val) {"
	"    this.splice(0);"
	"    for (let element of val.split(\" \")) {"
	"      this.push(element);"
	"    }"
	"  }"
	"  item(index) {"
	"    return this[index];"
	"  }"
	"  contains(token) {"
	"    let idx = this.indexOf(token);"
	"    if (idx != -1) {"
	"        return true;"
	"    } else {"
	"      return false;"
	"    }"
	"  }"
	"  add(token1, ...tokens) {"
	"    this.push(token1);"
	"    this.concat(tokens);"
	"  }"
	"  remove(token1, ...tokens) {"
	"    let idx = this.indexOf(token1);"
	"    if (idx != -1) {"
	"      this.splice(idx, 1);"
	"    }"
	"    for (let i = 0; i < tokens.length; i++) {"
	"      idx = this.indexOf(tokens[i]);"
	"      if (idx != -1) {"
	"        this.splice(idx, 1);"
	"      }"
	"    }"
	"  }"
	"  replace(oldToken, newToken) {"
	"    let idx = this.indexOf(oldToken);"
	"    if (idx != -1) {"
	"      this.splice(idx, 1, newToken);"
	"    }"
	"  }"
	"  supports(token) {"
	"    if (this.indexOf(token) != -1) {"
	"      return true;"
	"    } else {"
	"      return false;"
	"    }"
	"  }"
	"  toggle(token, ...force) {"
	"    /* fore is ignored for now */"
	"    let idx = this.indexOf(token);"
	"    if (idx != -1) {"
	"      this.splice(idx, 1);"
	"      return false;"
	"    } else {"
	"      this.push(token);"
	"      return true;"
	"    }"
	"  }"
	"  entries() {"
	"    let entries = new Array();"
	"    for (let i = 0; i < this.length; i++) {"
	"      entries.push(new Array(i, this[i]));"
	"    }"
	"    return entries;"
	"  }"
	"  forEach(callback, ...args) {"
	"    for (let i = 0; i < this.length; i++) {"
	"      if (args.length == 0) {"
	"        callback(this[i], i, this);"
	"      } else {"
	"        args[0].callback(this[i], i, this);"
	"      }"
	"    }"
	"  }"
	"  keys() {"
	"    let keys = new Array();"
	"    for (let i = 0; i < this.length; i++) {"
	"      keys.push(i);"
	"    }"
	"    return keys;"
	"  }"
	"  values() { return this; }"
	"};"
	""
	"globalThis.DOMException = class DOMException {"
	"  constructor(...args) {"
	"    if (args.length < 1) {"
	"      this.message = \"\";"
	"    } else {"
	"      this.message = args[0];"
	"    }"
	"    if (args.length < 2) {"
	"      this.name = \"\";"
	"    } else {"
	"      this.name = args[1];"
	"    }"
	"    this.code = 0;"
	"  }"
	"};"
	""
	"globalThis.MutationObserver = class MutationObserver {"
	"  constructor(callback) {"
	"    this.callback = callback;"
	"  }"
	"  observe(target, options) {"
	"    console.log(\"XXX: MutationObserver.observe\");"
	"  }"
	"  disconnect() {"
	"    console.log(\"XXX: MutationObserver.disconnect\");"
	"  }"
	"};"
	""
	"globalThis.IntersectionObserver = class IntersectionObserver {"
	"  constructor(callback) {"
	"    this.callback = callback;"
	"  }"
	"  observe(target) {"
	"    console.log(\"XXX: IntersectionObserver.observe\");"
	"  }"
	"  disconnect() {"
	"    console.log(\"XXX: IntersectionObserver.disconnect\");"
	"  }"
	"};"
	""
	"globalThis.Range = class Range {"
	"  setStart(container, offset) {"
	"    this.start_container = container;"
	"  }"
	"  setEnd(container, offset) {"
	"    this.end_container = container;"
	"  }"
	"  getBoundingClientRect() {"
	"    if (this.start_container) {"
	"      return this.start_container.getBoundingClientRect();"
	"    } else if (this.end_container) {"
	"      return this.end_container.getBoundingClientRect();"
	"    } else {"
	"      return null;"
	"    }"
	"  }"
	"  getClientRects() {"
	"    if (this.start_container) {"
	"      return this.start_container.getClientRects();"
	"    } else if (this.end_container) {"
	"      return this.end_container.getClientRects();"
	"    } else {"
	"      return null;"
	"    }"
	"  }"
	"  get collapsed() {"
	"    return true;"
	"  }"
	"  selectNode(node) {"
	"    console.log(\"XXX: Range.selectNode\");"
	"  }"
	"  selectNodeContents(node) {"
	"    console.log(\"XXX: Range.selectNodeContents\");"
	"  }"
	"  surroundContents(newParent) {"
	"    console.log(\"XXX: Range.surroundContents\");"
	"  }"
	"};"
	""
	"/* Element.attributes */"
	"function w3m_elementAttributes(obj) {"
	"  let attribute_keys = Object.keys(obj);"
	"  let attrs = new Array(); /* XXX NamedNodeMap */"
	"  for (let i = 0; i < attribute_keys.length; i++) {"
	"    let key = attribute_keys[i];"
	"    let value = obj[key];"
	"    /* XXX */"
	"    if (typeof value === \"string\") {"
	"      if (key === \"tagName\" || key === \"innerText\" ||"
	"          key === \"innerHTML\") {"
	"        continue;"
	"      } else if (key === \"className\") {"
	"        key = \"class\";"
	"      }"
	"    } else if (typeof value === \"function\") {"
	"      /* XXX onclick attr is not set in script.c for now. */"
	"      if (key !== \"onclick\") {"
	"        continue;"
	"      }"
	"      value = value.toString();"
	"    } else {"
	"      continue;"
	"    }"
	"    if (value === \"\") {"
	"      continue;"
	"    }"
	"    let attr = new Object();"
	"    attr.name = key;"
	"    attr.value = value;"
	"    attrs.push(attr);"
	"    attrs[key] = value;"
	"  }"
	"  return attrs;"
	"}"
	""
	"globalThis.URLSearchParams = class URLSearchParams {"
	"  constructor(init) {"
	"    this.param_keys = new Array();"
	"    this.param_values = new Array();"
	"    if (!init) {"
	"      return;"
	"    }"
	"    if (init.charAt(0) === \"?\") {"
	"      init = init.slice(1);"
	"    }"
	"    let pairs = init.split(\"&\");"
	"    for (let i = 0; i < pairs.length; i++) {"
	"      let array = pairs[i].split(\"=\");"
	"      this.param_keys.push(array[0]);"
	"      if (array.length == 1) {"
	"        this.param_values.push(\"\");"
	"      } else {"
	"        this.param_values.push(array[1]);"
	"      }"
	"    }"
	"  }"
	"  append(name, value) {"
	"    this.param_keys.push(name);"
	"    this.param_values.push(value);"
	"  }"
	"  delete(name) {"
	"    for(let i = 0; i < this.param_keys.length; i++) {"
	"      if (this.param_keys[i] === name) {"
	"        this.param_keys.splice(i, 1);"
	"        this.param_values.splice(i, 1);"
	"      }"
	"    }"
	"  }"
	"  entries() {"
	"    let entries = new Array();"
	"    for(let i = 0; i < this.param_keys.length; i++) {"
	"      entries.push(new Array(this.param_keys[i], this.param_values[i]));"
	"    }"
	"    return entries;"
	"  }"
	"  getAll(name) {"
	"    let values = new Array();"
	"    for(let i = 0; i < this.param_keys.length; i++) {"
	"      if (this.param_keys[i] === name) {"
	"        values.push(this.param_values[i]);"
	"      }"
	"    }"
	"    return values;"
	"  }"
	"  get(name) {"
	"    for(let i = 0; i < this.param_keys.length; i++) {"
	"      if (this.param_keys[i] === name) {"
	"        return this.param_values[i];"
	"      }"
	"    }"
	"    return null;"
	"  }"
	"  has(name) {"
	"    for(let i = 0; i < this.param_keys.length; i++) {"
	"      if (this.param_keys[i] === name) {"
	"        return true;"
	"      }"
	"    }"
	"    return false;"
	"  }"
	"  keys() {"
	"    return this.param_keys;"
	"  }"
	"  values() {"
	"    return this.param_values;"
	"  }"
	"  toString() {"
	"    let str = \"\";"
	"    let i;"
	"    for(i = 0; i < this.param_keys.length - 1; i++) {"
	"      str += this.param_keys[i];"
	"      str += \"=\";"
	"      str += this.param_values[i];"
	"      str += \"&\";"
	"    }"
	"    str += this.param_keys[i];"
	"    str += \"=\";"
	"    str += this.param_values[i];"
	"    return str;"
	"  }"
	"};"
	""
	"globalThis.MessagePort = class MessagePort {"
	"  postMessage(message, ...transfer) {"
	"    console.log(\"XXX: MessagePort.posMessage\");"
	"  }"
	"  start() {"
	"    console.log(\"XXX: MessagePort.start\");"
	"  }"
	"  close() {"
	"    console.log(\"XXX: MessagePort.close\");"
	"  }"
	"};"
	""
	"globalThis.MessageChannel = class MessageChannel {"
	"  constructor() {"
	"    this.port1 = new MessagePort();"
	"    this.port2 = new MessagePort();"
	"  }"
	"};"
	""
	"function importScripts(...scripts) {"
	"  console.log(\"XXX: importScripts\");"
	"}"
	""
	"function w3m_cloneNode(dst, src) {"
	"  for(let i = 0; i < src.childNodes.length; i++) {"
	"    let element = Object.assign(new HTMLElement(src.childNodes[i].tagName,"
	"                                src.childNodes[i]));"
	"    element.parentNode = element.parentElement = null;"
	"    element.childNodes = element.children = new HTMLCollection();"
	"    dst.appendChild(element);"
	"    w3m_cloneNode(element, src.childNodes[i]);"
	"  }"
	"}";
    const char script2[] =
	"globalThis.URL = class URL extends Location {"
	"  /* XXX */"
	"  static createObjectURL(object) {"
	"    console.log(\"XXX: URL.createObjectURL\");"
	"    return \"blob:null/0\";"
	"  }"
	"  static revokeObjectURL(objectURL) {"
	"    console.log(\"XXX: URL.revokeObjectURL\");"
	"  }"
	"};";

    if (term_ppc == 0) {
	if (!get_pixel_per_cell(&term_ppc, &term_ppl)) {
	    term_ppc = 8;
	    term_ppl = 16;
	}
    }

    if (rt == NULL) {
	rt = JS_NewRuntime();
    }

    ctx = JS_NewContext(rt);
    gl = JS_GetGlobalObject(ctx);

    JS_FreeValue(ctx, backtrace(ctx, script,
				JS_Eval(ctx, script, sizeof(script) - 1, "<input>", EVAL_FLAG)));

    create_class(ctx, gl, &LocationClassID, "Location", LocationFuncs,
		 sizeof(LocationFuncs) / sizeof(LocationFuncs[0]), &LocationClass,
		 location_new);

    create_class(ctx, gl, &HTMLFormElementClassID, "HTMLFormElement", ElementFuncs,
		 sizeof(ElementFuncs) / sizeof(ElementFuncs[0]), &HTMLFormElementClass,
		 html_form_element_new);

    create_class(ctx, gl, &HTMLElementClassID, "HTMLElement", ElementFuncs,
		 sizeof(ElementFuncs) / sizeof(ElementFuncs[0]) - 2, &HTMLElementClass,
		 html_element_new);

    create_class(ctx, gl, &HTMLImageElementClassID, "HTMLImageElement", ElementFuncs,
		 sizeof(ElementFuncs) / sizeof(ElementFuncs[0]) - 2, &HTMLImageElementClass,
		 html_image_element_new);

    create_class(ctx, gl, &HTMLSelectElementClassID, "HTMLSelectElement", ElementFuncs,
		 sizeof(ElementFuncs) / sizeof(ElementFuncs[0]) - 2, &HTMLSelectElementClass,
		 html_select_element_new);

    create_class(ctx, gl, &HTMLScriptElementClassID, "HTMLScriptElement", ElementFuncs,
		 sizeof(ElementFuncs) / sizeof(ElementFuncs[0]) - 2, &HTMLScriptElementClass,
		 html_script_element_new);

    create_class(ctx, gl, &HTMLCanvasElementClassID, "HTMLCanvasElement", ElementFuncs,
		 sizeof(ElementFuncs) / sizeof(ElementFuncs[0]) - 2, &HTMLCanvasElementClass,
		 html_canvas_element_new);

    create_class(ctx, gl, &SVGElementClassID, "SVGElement", ElementFuncs,
		 sizeof(ElementFuncs) / sizeof(ElementFuncs[0]) - 2, &SVGElementClass,
		 svg_element_new);

    create_class(ctx, gl, &ElementClassID, "Element", ElementFuncs,
		 sizeof(ElementFuncs) / sizeof(ElementFuncs[0]) - 2, &ElementClass,
		 element_new);

    create_class(ctx, gl, &HistoryClassID, "History", HistoryFuncs,
		 sizeof(HistoryFuncs) / sizeof(HistoryFuncs[0]), &HistoryClass,
		 history_new);

    create_class(ctx, gl, &NavigatorClassID, "Navigator", NavigatorFuncs,
		 sizeof(NavigatorFuncs) / sizeof(NavigatorFuncs[0]), &NavigatorClass,
		 navigator_new);

    create_class(ctx, gl, &DocumentClassID, "Document", DocumentFuncs,
		 sizeof(DocumentFuncs) / sizeof(DocumentFuncs[0]), &DocumentClass,
		 document_new);

    create_class(ctx, gl, &XMLHttpRequestClassID, "XMLHttpRequest", XMLHttpRequestFuncs,
		 sizeof(XMLHttpRequestFuncs) / sizeof(XMLHttpRequestFuncs[0]), &XMLHttpRequestClass,
		 xml_http_request_new);

    create_class(ctx, gl, &CanvasRenderingContext2DClassID, "CanvasRenderingContext2D",
		 CanvasRenderingContext2DFuncs,
		 sizeof(CanvasRenderingContext2DFuncs) / sizeof(CanvasRenderingContext2DFuncs[0]),
		 &CanvasRenderingContext2DClass, canvas_rendering_context2d_new);

    JS_FreeValue(ctx, backtrace(ctx, script2,
				JS_Eval(ctx, script2, sizeof(script2) - 1, "<input>", EVAL_FLAG)));

    /* window object */
    state = (WindowState *)GC_MALLOC_UNCOLLECTABLE(sizeof(WindowState));
    state->win = newGeneralList();
    state->close = FALSE;
    JS_SetOpaque(gl, state);

    for (i = 0; i < sizeof(WindowFuncs) / sizeof(WindowFuncs[0]); i++) {
	JS_SetPropertyStr(ctx, gl, WindowFuncs[i].name,
			  JS_NewCFunction(ctx, WindowFuncs[i].func, WindowFuncs[i].name, 1));
    }

    /* console object */
    console = JS_NewObject(ctx);
    for (i = 0; i < sizeof(ConsoleFuncs) / sizeof(ConsoleFuncs[0]); i++) {
	JS_SetPropertyStr(ctx, console, ConsoleFuncs[i].name,
			  JS_NewCFunction(ctx, ConsoleFuncs[i].func, ConsoleFuncs[i].name, 1));
    }
    JS_SetPropertyStr(ctx, gl, "console", console);
    JS_SetPropertyStr(ctx, gl, "devicePixelRatio", JS_NewFloat64(ctx, 1.0));
    JS_SetPropertyStr(ctx, gl, "setInterval",
		      JS_NewCFunction(ctx, set_interval, "setInterval", 1));
    JS_SetPropertyStr(ctx, gl, "setTimeout",
		      JS_NewCFunction(ctx, set_timeout, "setTimeout", 1));
    JS_SetPropertyStr(ctx, gl, "clearInterval",
		      JS_NewCFunction(ctx, clear_interval, "clearInterval", 1));
    JS_SetPropertyStr(ctx, gl, "clearTimeout",
		      JS_NewCFunction(ctx, clear_interval, "clearTimeout", 1));

    JS_FreeValue(ctx, gl);

    ctxstate = (CtxState*)GC_MALLOC_UNCOLLECTABLE(sizeof(*ctxstate));
    for (i = 0; i < NUM_TAGNAMES; i++) {
	ctxstate->tagnames[i] = JS_UNDEFINED;
    }
    ctxstate->funcs = NULL;
    ctxstate->nfuncs = 0;
    ctxstate->buf = buf;
    JS_SetContextOpaque(ctx, ctxstate);

    return ctx;
}

void
js_html_final(JSContext *ctx) {
    JSValue gl = JS_GetGlobalObject(ctx);
    CtxState *ctxstate;
    int i;

    for (i = 0; i < num_interval_callbacks; i++) {
	if (interval_callbacks[i].ctx == ctx) {
	    destroy_interval_callback(interval_callbacks + i);
	}
    }

    GC_FREE(JS_GetOpaque(gl, WindowClassID));
    JS_FreeValue(ctx, gl);

    ctxstate = JS_GetContextOpaque(ctx);
    for (i = 0; i < ctxstate->nfuncs; i++) {
	JS_FreeValue(ctx, ctxstate->funcs[i]);
    }
    GC_FREE(ctxstate->funcs);
    GC_FREE(ctxstate);

    JS_FreeContext(ctx);
}

void
js_eval(JSContext *ctx, char *script) {
    JSValue ret = js_eval2(ctx, script);
    JS_FreeValue(ctx, ret);
}

JSValue
js_eval2(JSContext *ctx, char *script) {
#if 0
    char *beg = script;
    char *p;
    Str str = Strnew();
    while ((p = strchr(beg, ';'))) {
	Strcat_charp_n(str, beg, p - beg + 1);
	if (strncmp(p + 1, "base64,", 7) != 0) {
	    Strcat_charp(str, "\n");
	}
        beg = p + 1;
    }
    Strcat_charp(str, beg);
    script = str->ptr;
#elif 0
    char *beg = script;
    char *p;
    const char seq[] = "var returnNodesAry=rangeNode.getElementsByTagName(aTagName);";
    Str str = Strnew();
    while ((p = strstr(beg, seq))) {
	Strcat_charp_n(str, beg, p - beg + sizeof(seq) - 1);
	Strcat_charp(str, "console.log(returnNodesAry);");
        beg = p + sizeof(seq) - 1;
    }
    Strcat_charp(str, beg);
    script = str->ptr;
#elif 0
    /* for facebook.com */
    char *beg = script;
    char *p;
    const char seq[] = "if(g&&b){var c=b.childNodes;";
    Str str = Strnew();
    while ((p = strstr(beg, seq))) {
	Strcat_charp_n(str, beg, p - beg + sizeof(seq) - 1);
	Strcat_charp(str, "c.length = 0;");
        beg = p + sizeof(seq) - 1;
    }
    Strcat_charp(str, beg);
    script = str->ptr;
#elif 0
    /* for acid tests */
    char *beg = script;
    char *p;
    const char seq[] = "setTimeout(update, delay);";
    Str str = Strnew();
    while ((p = strstr(beg, seq))) {
	Strcat_charp_n(str, beg, p - beg + sizeof(seq) - 1);
	Strcat_charp(str, "console.log(log)");
        beg = p + sizeof(seq) - 1;
    }
    Strcat_charp(str, beg);
    script = str->ptr;
#endif
    return backtrace(ctx, script,
		     JS_Eval(ctx, script, strlen(script), "<input>", EVAL_FLAG));
}

JSValue
js_eval2_this(JSContext *ctx, int formidx, char *script) {
    Str str = Sprintf("document.forms[%d];", formidx);
    JSValue form = JS_Eval(ctx, str->ptr, str->length, "<input>", EVAL_FLAG);
    JSValue ret;

    if (strncmp(script, "func_id:", 8) == 0) {
	int idx = atoi(script + 8);
	CtxState *ctxstate = JS_GetContextOpaque(ctx);
	if (ctxstate->nfuncs >= idx + 1) {
	    JSValue jsThis;
	    if (JS_IsUndefined(form)) {
		jsThis = JS_GetGlobalObject(ctx);
	    } else {
		jsThis = JS_NewObject(ctx);
		JS_SetPropertyStr(ctx, jsThis, "form", form);
	    }
	    ret = JS_Call(ctx, ctxstate->funcs[idx], jsThis, 0, NULL);
	    JS_FreeValue(ctx, jsThis);

	    return ret;
	}
    }

    if (JS_IsUndefined(form)) {
	ret = js_eval2(ctx, script);
    } else {
	JSValue jsThis = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, jsThis, "form", form);
	ret = backtrace(ctx, script,
			JS_EvalThis(ctx, jsThis, script, strlen(script), "<input>", EVAL_FLAG));
	JS_FreeValue(ctx, jsThis);
    }

    return ret;
}

char *
js_get_cstr(JSContext *ctx, JSValue value)
{
    char *new_str = get_cstr(ctx, value);
    JS_FreeValue(ctx, value);

    return new_str;
}

Str
js_get_str(JSContext *ctx, JSValue value)
{
    Str new_str = get_str(ctx, value);
    JS_FreeValue(ctx, value);

    return new_str;
}

void
js_reset_functions(JSContext *ctx) {
    CtxState *ctxstate = JS_GetContextOpaque(ctx);
    int i;

    for (i = 0; i < ctxstate->nfuncs; i++) {
	JS_FreeValue(ctx, ctxstate->funcs[i]);
    }
    GC_FREE(ctxstate->funcs);

    ctxstate->funcs = (JSValue*)GC_MALLOC_UNCOLLECTABLE(sizeof(JSValue) * 16);
    ctxstate->nfuncs = 0;
}

Str
js_get_function(JSContext *ctx, char *script) {
    JSValue value = js_eval2(ctx, script);
    Str str;

    if (!JS_IsFunction(ctx, value)) {
	str = NULL;
    } else {
	CtxState *ctxstate = JS_GetContextOpaque(ctx);
	if (ctxstate->nfuncs % 16 == 0) {
	    ctxstate->funcs = GC_REALLOC(ctxstate->funcs,
					 sizeof(JSValue) * (ctxstate->nfuncs + 16));
	}
	ctxstate->funcs[ctxstate->nfuncs] = value;
	str = Sprintf("func_id:%d", ctxstate->nfuncs);
	ctxstate->nfuncs++;
    }

    return str;
}

int
js_get_int(JSContext *ctx, int *i, JSValue value)
{
    if (JS_IsUndefined(value)) {
	JS_FreeValue(ctx, value);

	return 0;
    }

    JS_ToInt32(ctx, i, value);
    JS_FreeValue(ctx, value);

    return 1;
}

/* Regard "true" or "false" string as true or false value. */
int
js_is_true(JSContext *ctx, JSValue value)
{
    const char *str;
    int flag = -1;

    if (JS_IsString(value)) {
	/* http://www.shurey.com/js/samples/6_smp7.html */
	if ((str = JS_ToCString(ctx, value)) != NULL) {
	    if (strcasecmp(str, "false") == 0) {
		flag = 0;
	    } else if (strcasecmp(str, "true") == 0) {
		flag = 1;
	    }
	}
	JS_FreeCString(ctx, str);
    } else if (!JS_IsUndefined(value)) {
	flag = JS_ToBool(ctx, value);
    } else {
	flag = -1;
    }

    JS_FreeValue(ctx, value);

    return flag;
}

static char *
to_upper(char *str) {
    size_t len = strlen(str);
    char *str2 = malloc(len + 1);
    size_t i;
    for (i = 0; i < len + 1; i++) {
	if ('a' <= str[i] && str[i] <= 'z') {
	    str2[i] = str[i] - 0x20;
	} else {
	    str2[i] = str[i];
	}
    }
    return str2;
}

static char *
to_lower(char *str) {
    size_t len = strlen(str);
    char *str2 = malloc(len + 1);
    size_t i;
    for (i = 0; i < len + 1; i++) {
	if ('A' <= str[i] && str[i] <= 'Z') {
	    str2[i] = str[i] + 0x20;
	} else {
	    str2[i] = str[i];
	}
    }
    return str2;
}

void
js_add_event_listener(JSContext *ctx, JSValue jsThis, char *type, char *func)
{
    if (strncmp(func, "func_id:", 8) == 0) {
	int idx = atoi(func + 8);
	CtxState *ctxstate = JS_GetContextOpaque(ctx);
	if (ctxstate->nfuncs >= idx + 1) {
	    JSValue argv[2];
	    argv[0] = JS_NewString(ctx, type);
	    argv[1] = ctxstate->funcs[idx];
	    add_event_listener(ctx, jsThis, 2, argv);
	    JS_FreeValue(ctx, argv[0]);
	}
    }
    JS_FreeValue(ctx, jsThis);
}
#endif

#ifdef USE_LIBXML2

#ifdef LIBXML_TREE_ENABLED
static void
push_node_to(JSContext *ctx, JSValue node, char *array_name)
{
    JSValue array = js_eval2(ctx, array_name);
    JSValue prop = JS_GetPropertyStr(ctx, array, "push");
    JS_FreeValue(ctx, JS_Call(ctx, prop, array, 1, &node));
    JS_FreeValue(ctx, prop);
    JS_FreeValue(ctx, array);
}

static JSValue
find_form(JSContext *ctx, xmlNode *node)
{
#ifndef USE_LIBXML2
    xmlAttr *attr;

    for (attr = node->properties; attr != NULL; attr = attr->next) {
	xmlNode *n;
	if (strcasecmp((char*)attr->name, "id") == 0) {
	    n = attr->children;
	    if (n && n->type == XML_TEXT_NODE) {
		char *script =
		    Sprintf("{"
			    "  let form = null;"
			    "  for (let i = 0; i < document.forms.length; i++) {"
			    "    form = w3m_getElementById(document.forms[i], \"%s\");"
			    "    if (form) {"
			    "      break;"
			    "    }"
			    "  }"
			    "  form;"
			    "}", (char*)n->content)->ptr;
		return JS_Eval(ctx, script, strlen(script), "<input>", EVAL_FLAG);
	    }
	} else if (strcasecmp((char*)attr->name, "name") == 0) {
	    n = attr->children;
	    if (n && n->type == XML_TEXT_NODE) {
		char *script =
		    Sprintf("{"
			    "  let ret = new Array();"
			    "  for (let i = 0; i < document.forms.length; i++) {"
			    "    w3m_getElementsByName(document.forms[i], \"%s\", ret);"
			    "    if (ret.length > 0) {"
			    "      break;"
			    "    }"
			    "  }"
			    "  if (ret.length > 0) {"
			    "    ret[0];"
			    "  } else {"
			    "    null;"
			    "  }"
			    "}", (char*)n->content)->ptr;
		return JS_Eval(ctx, script, strlen(script), "<input>", EVAL_FLAG);
	    }
	}
    }
#endif

    return JS_NULL;
}

static void
create_tree(JSContext *ctx, xmlNode *node, JSValue jsparent, int innerhtml)
{
#ifdef DOM_DEBUG
    FILE *fp = fopen("domlog.txt", "a");
    fprintf(fp, "------\n");
#endif

    for (; node; node = node->next) {
	JSValue jsnode;

	if (node->type == XML_ELEMENT_NODE) {
	    xmlAttr *attr;

	    if (!innerhtml &&
		(strcasecmp((char*)node->name, "FORM") == 0 ||
		 strcasecmp((char*)node->name, "SELECT") == 0 ||
		 strcasecmp((char*)node->name, "INPUT") == 0 ||
		 strcasecmp((char*)node->name, "TEXTAREA") == 0 ||
		 strcasecmp((char*)node->name, "BUTTON") == 0)) {
		jsnode = find_form(ctx, node);
		if (!JS_IsNull(jsnode)) {
		    goto child_tree;
		}
	    }

#ifdef DOM_DEBUG
	    fprintf(fp, "element %s ", node->name);
#endif

	    if (strcasecmp((char*)node->name, "SCRIPT") == 0) {
		jsnode = html_script_element_new(ctx, jsparent, 0, NULL);
		push_node_to(ctx, jsnode, "document.scripts;");
	    } else if (strcasecmp((char*)node->name, "IMG") == 0) {
		jsnode = html_image_element_new(ctx, jsparent, 0, NULL);
		push_node_to(ctx, jsnode, "document.images;");
	    } else if (strcasecmp((char*)node->name, "CANVAS") == 0) {
		jsnode = html_canvas_element_new(ctx, jsparent, 0, NULL);
	    } else if (strcasecmp((char*)node->name, "FORM") == 0) {
		jsnode = html_form_element_new(ctx, jsparent, 0, NULL);
	    } else if (strcasecmp((char*)node->name, "SELECT") == 0) {
		jsnode = html_select_element_new(ctx, jsparent, 0, NULL);
	    } else {
		JSValue arg = JS_NewString(ctx, to_upper((char*)node->name));
		jsnode = html_element_new(ctx, jsparent, 1, &arg);
		JS_FreeValue(ctx, arg);

		/* XXX Ignore <area> tag which has href attribute. */
		if (strcasecmp((char*)node->name, "a") == 0) {
		    push_node_to(ctx, jsnode, "document.links;");
		}
	    }

	    for (attr = node->properties; attr != NULL; attr = attr->next) {
		xmlNode *n;
		JSValue value;
		char *name;

		n = attr->children;
		if (n && n->type == XML_TEXT_NODE) {
		    value = JS_NewString(ctx, (char*)n->content);

		    if (strcasecmp((char*)attr->name, "class") == 0) {
			element_class_name_set(ctx, jsnode, value);
			JS_FreeValue(ctx, value);
			continue;
		    }

		    name = to_lower((char*)attr->name);
#ifdef DOM_DEBUG
		    fprintf(fp, "attr %s=%s ", name, n->content);
#endif
		} else {
		    name = (char*)attr->name;
		    value = JS_NULL;
#ifdef DOM_DEBUG
		    fprintf(fp, "attr %s", name);
#endif
		}
		JS_SetPropertyStr(ctx, jsnode, name, value);
	    }

#ifdef DOM_DEBUG
	    fprintf(fp, "\n");
#endif
	} else if (node->type == XML_TEXT_NODE) {
	    JSValue arg = JS_NewString(ctx, "#text");
	    jsnode = html_element_new(ctx, jsparent, 1, &arg);
	    JS_FreeValue(ctx, arg);

#ifdef DOM_DEBUG
	    fprintf(fp, "text %s\n", node->content);
#endif
	    JS_SetPropertyStr(ctx, jsnode, "nodeValue", JS_NewString(ctx, (char*)node->content));
	    JS_SetPropertyStr(ctx, jsnode, "data", JS_NewString(ctx, (char*)node->content));
	    if (innerhtml) {
		JS_SetPropertyStr(ctx, jsnode, "isModified", JS_TRUE);
	    }
	} else if (node->type == XML_COMMENT_NODE) {
	    JSValue arg = JS_NewString(ctx, "#comment");
	    jsnode = html_element_new(ctx, jsparent, 1, &arg);
	    JS_FreeValue(ctx, arg);

#ifdef DOM_DEBUG
	    fprintf(fp, "text %s\n", node->content);
#endif
	    JS_SetPropertyStr(ctx, jsnode, "nodeValue", JS_NewString(ctx, (char*)node->content));
	    JS_SetPropertyStr(ctx, jsnode, "data", JS_NewString(ctx, (char*)node->content));
	} else {
#ifdef DOM_DEBUG
	    fprintf(fp, "element %d\n", node->type);
#endif
	    continue;
	}

	element_append_child(ctx, jsparent, 1, &jsnode);

#ifdef DOM_DEBUG
	fclose(fp);
#endif
    child_tree:
	create_tree(ctx, node->children, jsnode, innerhtml);
#ifdef DOM_DEBUG
	fp = fopen("domlog.txt", "a");
#endif
    }
#ifdef DOM_DEBUG
    fclose(fp);
#endif

    JS_FreeValue(ctx, jsparent);
}

int
js_create_dom_tree(JSContext *ctx, char *filename, const char *charset)
{
    URLFile uf;
    xmlDoc *doc;
    xmlNode *node;

    if (filename == NULL) {
	goto error;
    }

    LIBXML_TEST_VERSION;

    if (strcasecmp(charset, "US-ASCII") == 0) {
	charset = NULL;
    }

    if (examineFile2(filename, &uf)) {
	Str str = Strnew();
	while (1) {
	    Str s = StrISgets(uf.stream);
	    if (s->length == 0) {
		ISclose(uf.stream);
		break;
	    }
	    Strcat(str, s);
	}

	doc = htmlReadMemory(str->ptr, str->length, filename, charset,
			     HTML_PARSE_RECOVER|HTML_PARSE_NOERROR|HTML_PARSE_NOWARNING);
    } else {
	/* parse the file and get the DOM */
	doc = htmlReadFile(filename, charset,
			   HTML_PARSE_RECOVER|HTML_PARSE_NOERROR|HTML_PARSE_NOWARNING);
    }

    if (doc == NULL) {
	goto error;
    }

    for (node = xmlDocGetRootElement(doc)->children; node; node = node->next) {
	JSValue element;
	xmlAttr *attr;

	if (strcmp((char*)node->name, "head") == 0) {
	    element = js_eval2(ctx, "document.head;");
	} else if (strcmp((char*)node->name, "body") == 0) {
	    element = js_eval2(ctx, "document.body;");
	} else {
#ifdef DOM_DEBUG
	    FILE *fp = fopen("domlog.txt", "a");
	    fprintf(fp, "(orphan element %s)\n", node->name);
	    fclose(fp);
#endif
	    continue;
	}

	for (attr = node->properties; attr != NULL; attr = attr->next) {
	    xmlNode *n;

	    n = attr->children;
	    if (n && n->type == XML_TEXT_NODE && strcasecmp((char*)attr->name, "onload") == 0) {
		JS_SetPropertyStr(ctx, element, "onload", JS_NewString(ctx, (char*)n->content));
#ifdef DOM_DEBUG
		FILE *fp = fopen("domlog.txt", "a");
		fprintf(fp, "body onload=%s\n", n->content);
		fclose(fp);
#endif
	    }
	}

	create_tree(ctx, node->children, element, 0);
    }

    xmlFreeDoc(doc);
    xmlCleanupParser();

    js_eval(ctx,
	    "if (document.scripts && document.scripts.length > 0) {"
	    "  document.currentScript = document.scripts[document.scripts.length - 1];"
	    "}");

    return 1;

error:
    {
	FILE *fp;
	if ((fp = fopen("scriptlog.txt", "a")) != NULL) {
	    fprintf(fp, "ERROR: could not parse file %s\n", filename);
	    fclose(fp);
	}
	return 0;
    }
}

void
js_insert_dom_tree(JSContext *ctx, const char *html)
{
    JSValue parent = js_eval2(ctx, "document.head;");
    create_tree_from_html(ctx, parent, html);
    JS_FreeValue(ctx, parent);
}

#else

int
js_create_dom_tree(JSContext *ctx, char *filename, const char *charset) { return 0; }

void
js_insert_dom_tree(JSContext *ctx, const char *html) { ; }

#endif /* LIBXML_TREE_ENABLED */

#endif
