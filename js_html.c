#include "fm.h"
#ifdef USE_JAVASCRIPT
#include "js_html.h"
#include <sys/utsname.h>

#define NUM_TAGNAMES 8
#define TN_FORM 0
#define TN_IMG 1
#define TN_SCRIPT 2
#define TN_SELECT 3
#define TN_CANVAS 4
#define TN_SVG 5
#define TN_IFRAME 6
#define TN_OBJECT 7

typedef struct _CtxState {
    JSValue tagnames[NUM_TAGNAMES];
    JSValue *funcs;
    int nfuncs;
    Buffer *buf;
    JSValue document;
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
static JSClassID HTMLIFrameElementClassID;
static JSClassID HTMLObjectElementClassID;
static JSClassID ElementClassID;
static JSClassID NodeClassID;
static JSClassID SVGElementClassID;
static JSClassID XMLHttpRequestClassID;
static JSClassID CanvasRenderingContext2DClassID;

char *alert_msg;

static JSRuntime *rt;

int term_ppc;
int term_ppl;

#define BACKTRACE(ctx) \
    js_eval(ctx, "try { throw new Error(\"error\"); } catch (e) { console.log(e.stack); }")

#ifdef USE_LIBXML2
#if 0
#define DOM_DEBUG
#endif
#include <libxml/HTMLparser.h>
#include <libxml/HTMLtree.h>
static void create_dtd(JSContext *ctx, xmlDtd *dtd, JSValue doc);
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

static void
dump_error(JSContext *ctx, const char *script)
{
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

#ifdef SCRIPT_DEBUG
static JSValue
backtrace(JSContext *ctx, const char *script, JSValue eval_ret)
{
#if 0
    FILE *fp = fopen(Sprintf("w3mlog%p.txt", ctx)->ptr, "a");
    fprintf(fp, "%s\n", script);
    fclose(fp);
#endif

    if (JS_IsException(eval_ret)) {
	dump_error(ctx, script);
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
	JS_ThrowTypeError(ctx, "addEventListener: Too few arguments.");

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

    event = JS_Eval(ctx, "new Event(null);", 16, "<input>", EVAL_FLAG);
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
	strcmp(str, "DOMContentLoaded") != 0 && strcmp(str, "readystatechange") != 0 &&
	strcmp(str, "visibilitychange") != 0 && strcmp(str, "submit") != 0 &&
	strcmp(str, "click") != 0 && strcmp(str, "keypress") != 0 &&
	strcmp(str, "keydown") != 0 && strcmp(str, "keyup") != 0 &&
	strcmp(str, "input") != 0 && strcmp(str, "focus") != 0 &&
	strcmp(str, "message") != 0 && strcmp(str, "change") != 0)
#endif
    {
	FILE *fp = fopen("scriptlog.txt", "a");
	JSValue tag = JS_GetPropertyStr(ctx, jsThis, "nodeName");
	const char *str2 = JS_ToCString(ctx, tag);
	const char *script;

	fprintf(fp, "<Unknown event (addEventListener)> %s (tag %s) \n", str, str2);
	script = JS_ToCString(ctx, argv[1]);
	if (script) {
	    fprintf(fp, "=> %s\n\n", script);
	    JS_FreeCString(ctx, script);
	}
	JS_FreeCString(ctx, str2);
	JS_FreeValue(ctx, tag);
	fclose(fp);
    }
    JS_FreeCString(ctx, str);
#endif

    return JS_UNDEFINED;
}

static JSValue
attach_event(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue argv2[2];
    const char *type;

    if (argc < 2) {
	JS_ThrowTypeError(ctx, "attachEvent: Too few arguments.");

	return JS_EXCEPTION;
    }

    type = JS_ToCString(ctx, argv[0]);
    if (strncmp(type, "on", 2) == 0) {
	argv2[0] = JS_NewString(ctx, type + 2);
    } else {
	argv2[0] = JS_DupValue(ctx, argv[0]);
    }
    JS_FreeCString(ctx, type);
    argv2[1] = argv[1];

    add_event_listener(ctx, jsThis, 2, argv2);

    JS_FreeValue(ctx, argv2[0]);

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
	JS_ThrowTypeError(ctx, "removeEventListener: Too few arguments.");

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
detach_event(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue argv2[2];
    const char *type;

    if (argc < 2) {
	JS_ThrowTypeError(ctx, "detachEvent: Too few arguments.");

	return JS_EXCEPTION;
    }

    type = JS_ToCString(ctx, argv[0]);
    if (strncmp(type, "on", 2) == 0) {
	argv2[0] = JS_NewString(ctx, type + 2);
    } else {
	argv2[0] = JS_DupValue(ctx, argv[0]);
    }
    JS_FreeCString(ctx, type);
    argv2[1] = argv[1];

    remove_event_listener(ctx, jsThis, 2, argv2);

    JS_FreeValue(ctx, argv2[0]);

    return JS_UNDEFINED;
}

static JSValue
dispatch_event(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue type;
    const char *str;

    if (argc < 1) {
	JS_ThrowTypeError(ctx, "dispatchEvent: Too few arguments.");

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
    WindowState *state;
    OpenWindow *w = malloc(sizeof(OpenWindow));

    if (!JS_IsObject(jsThis)) {
	/* open() (JS_UNDEFINED) */
	jsThis = JS_GetGlobalObject(ctx);
    } else {
	/* window.open() */
	jsThis = JS_DupValue(ctx, jsThis);
    }
    state = JS_GetOpaque(jsThis, WindowClassID);

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
    WindowState *state;

    if (!JS_IsObject(jsThis)) {
	/* close() (JS_UNDEFINED) */
	jsThis = JS_GetGlobalObject(ctx);
    } else {
	/* window.close() */
	jsThis = JS_DupValue(ctx, jsThis);
    }
    state = JS_GetOpaque(jsThis, WindowClassID);

    state->close = TRUE;

    return JS_UNDEFINED;
}

static JSValue
window_alert(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    const char *str;
    size_t len;

    if (argc < 1) {
	JS_ThrowTypeError(ctx, "Window.alert: Too few arguments.");

	return JS_EXCEPTION;
    }

    str = JS_ToCStringLen(ctx, &len, argv[0]);
    if (!str) {
	JS_ThrowTypeError(ctx, "Window.alert: 1st argument is not String.");

	return JS_EXCEPTION;
    }
    alert_msg = allocStr(str, len);
    JS_FreeCString(ctx, str);

    return JS_UNDEFINED;
}

static JSValue
window_prompt(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    const char *p;
    char *prompt;
    char *ans;

    if (argc < 1) {
	JS_ThrowTypeError(ctx, "Window.prompt: Too few arguments.");

	return JS_EXCEPTION;
    }

    p = JS_ToCString(ctx, argv[0]);
    if (!p) {
	JS_ThrowTypeError(ctx, "Window.alert: 1st argument is not String.");

	return JS_EXCEPTION;
    }
    prompt = Sprintf("%s: ", p)->ptr;
    JS_FreeCString(ctx, p);

    ans = inputStr(prompt, "");

    if (ans) {
	return JS_NewString(ctx, ans);
    } else {
	JSValue ret;

	if (argc > 1) {
	    p = JS_ToCString(ctx, argv[1]);
	    ret = JS_NewString(ctx, p);
	    JS_FreeCString(ctx, p);
	} else {
	    ret = JS_NULL;
	}

	return ret;
    }
}

static JSValue
window_confirm(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    const char *str;
    char *ans;

    if (argc < 1) {
	JS_ThrowTypeError(ctx, "Window.confirm: Too few arguments.");

	return JS_EXCEPTION;
    }

    str = JS_ToCString(ctx, argv[0]);
    if (!str) {
	JS_ThrowTypeError(ctx, "Window.alert: 1st argument is not String.");

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
	JS_ThrowTypeError(ctx, "Window.getComputedStyle: Too few arguments.");

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
	JS_ThrowTypeError(ctx, "Window.matchMedia: Too few arguments.");

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
    { "prompt", window_prompt },
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
    { "attachEvent", attach_event }, /* XXX DEPRECATED */
    { "detachEvent", detach_event }, /* XXX DEPRECATED */
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
	    JS_ThrowTypeError(ctx, "Location constructor: 1st argument is not String.");

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
	JS_ThrowTypeError(ctx, "Location.replace: Too few arguments.");

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
	JS_ThrowTypeError(ctx, "Location.href: 1st argument is not String.");

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
	JS_ThrowTypeError(ctx, "Location.protocol: 1st argument is not String.");

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
	JS_ThrowTypeError(ctx, "Location.host: 1st argument is not String.");

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
	JS_ThrowTypeError(ctx, "Location.hostname: 1st argument is not String.");

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
	JS_ThrowTypeError(ctx, "Location.host: 1st argument is neither String nor Number.");

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
	JS_ThrowTypeError(ctx, "Location.pathname: 1st argument is not String.");

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
    return backtrace(ctx, script,
		     JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1, "<input>", EVAL_FLAG));
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
	JS_ThrowTypeError(ctx, "Location.search: 1st argument is not String.");

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
	JS_ThrowTypeError(ctx, "Location.hash: 1st argument is not String.");

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
	JS_ThrowTypeError(ctx, "Location.username: 1st argument is not String.");

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
	JS_ThrowTypeError(ctx, "Location.password: 1st argument is not String.");

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
    JS_CFUNC_DEF("toJSON", 1, location_to_string), /* XXX URL method */
    JS_CGETSET_DEF("href", location_href_get, location_href_set),
    JS_CGETSET_DEF("protocol", location_protocol_get, location_protocol_set),
    JS_CGETSET_DEF("host", location_host_get, location_host_set),
    JS_CGETSET_DEF("hostname", location_hostname_get, location_hostname_set),
    JS_CGETSET_DEF("port", location_port_get, location_port_set),
    JS_CGETSET_DEF("pathname", location_pathname_get, location_pathname_set),
    JS_CGETSET_DEF("search", location_search_get, location_search_set),
    JS_CGETSET_DEF("searchParams", location_search_params_get, NULL), /* XXX URL property */
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
    JS_SetPropertyStr(ctx, obj, "ownerDocument", JS_DupValue(ctx, js_get_document(ctx)));
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
		      JS_GetPropertyStr(ctx, js_get_document(ctx), "querySelector"));
    JS_SetPropertyStr(ctx, obj, "querySelectorAll",
		      JS_GetPropertyStr(ctx, js_get_document(ctx), "querySelectorAll"));

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
    JS_SetPropertyStr(ctx, style, "visibility", JS_NewString(ctx, "visible"));
    JS_SetPropertyStr(ctx, style, "display", JS_NewString(ctx, ""));
    JS_SetPropertyStr(ctx, obj, "style", style);

    JS_SetPropertyStr(ctx, obj, "lang", JS_NewString(ctx, "unknown"));

    JS_SetPropertyStr(ctx, obj, "offsetWidth", JS_NewInt32(ctx, term_ppc));
    JS_SetPropertyStr(ctx, obj, "offsetHeight", JS_NewInt32(ctx, term_ppl));
    JS_SetPropertyStr(ctx, obj, "offsetLeft", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, obj, "offsetTop", JS_NewInt32(ctx, 0));

    JS_SetPropertyStr(ctx, obj, "dataset", JS_NewObject(ctx)); /* XXX DOMStringMap */

    /* XXX HTMLSelectElement */
    JS_SetPropertyStr(ctx, obj, "disabled", JS_FALSE);

    /* XXX HTMLIFrameElement */
    if (strcasecmp(str, "IFRAME") == 0 || strcasecmp(str, "OBJECT") == 0) {
	const char script[] =
	    "{"
	    "  let doc = new Document();"
	    "  w3m_initDocument(doc);"
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
	JS_ThrowTypeError(ctx, "HTMLElement constructor: Too few arguments.");

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

static const JSClassDef HTMLIFrameElementClass = {
    "HTMLIFrameElement", NULL /* finalizer */, NULL /* gc_mark */, NULL /* call */, NULL
};

static JSValue
html_iframe_element_new(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue obj;
    CtxState *ctxstate;

    ctxstate = JS_GetContextOpaque(ctx);
    if (JS_IsUndefined(ctxstate->tagnames[TN_IFRAME])) {
	ctxstate->tagnames[TN_IFRAME] = JS_NewString(ctx, "IFRAME");
    }

    obj = JS_NewObjectClass(ctx, HTMLIFrameElementClassID);
    set_element_property(ctx, obj, ctxstate->tagnames[TN_IFRAME]);

    return obj;
}

static const JSClassDef HTMLObjectElementClass = {
    "HTMLObjectElement", NULL /* finalizer */, NULL /* gc_mark */, NULL /* call */, NULL
};

static JSValue
html_object_element_new(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue obj;
    CtxState *ctxstate;

    ctxstate = JS_GetContextOpaque(ctx);
    if (JS_IsUndefined(ctxstate->tagnames[TN_OBJECT])) {
	ctxstate->tagnames[TN_OBJECT] = JS_NewString(ctx, "OBJECT");
    }

    obj = JS_NewObjectClass(ctx, HTMLObjectElementClassID);
    set_element_property(ctx, obj, ctxstate->tagnames[TN_OBJECT]);

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
    CtxState *ctxstate;

    ctxstate = JS_GetContextOpaque(ctx);
    if (JS_IsUndefined(ctxstate->tagnames[TN_SVG])) {
	ctxstate->tagnames[TN_SVG] = JS_NewString(ctx, "SVG");
    }

    obj = JS_NewObjectClass(ctx, SVGElementClassID);
    set_element_property(ctx, obj, ctxstate->tagnames[TN_SVG]);

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
	JS_ThrowTypeError(ctx, "Element constructor: Too few arguments.");

	return JS_EXCEPTION;
    }

    obj = JS_NewObjectClass(ctx, ElementClassID);
    set_element_property(ctx, obj, argv[0]);

    return obj;
}

static JSValue
node_remove_child(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv);

static void
add_node_to(JSContext *ctx, JSValue node, char *array_name)
{
    JSValue array = JS_GetPropertyStr(ctx, js_get_document(ctx), array_name);
    JSValue prop = JS_GetPropertyStr(ctx, array, "push");
    JS_FreeValue(ctx, JS_Call(ctx, prop, array, 1, &node));
    JS_FreeValue(ctx, prop);
    JS_FreeValue(ctx, array);
}

static void
remove_node_from(JSContext *ctx, JSValue node, char *array_name)
{
    JSValue array = JS_GetPropertyStr(ctx, js_get_document(ctx), array_name);
    JSValue prop = JS_GetPropertyStr(ctx, array, "indexOf");
    JSValue ret = JS_Call(ctx, prop, array, 1, &node);
    int idx;

    JS_FreeValue(ctx, prop);
    JS_ToInt32(ctx, &idx, ret);
    if (idx >= 0) {
	JSValue args[2];

	prop = JS_GetPropertyStr(ctx, array, "splice");
	args[0] = ret;
	args[1] = JS_NewInt32(ctx, 1);
	JS_FreeValue(ctx, JS_Call(ctx, prop, array, 2, args));
	JS_FreeValue(ctx, args[1]);
    }
    JS_FreeValue(ctx, ret);
    JS_FreeValue(ctx, prop);
    JS_FreeValue(ctx, array);
}

static JSValue
dom_exception_new(JSContext *ctx, int code, const char *name, const char *message)
{
    JSValue e = JS_Eval(ctx, "new DOMException();", 19, "<input>", EVAL_FLAG);

    JS_SetPropertyStr(ctx, e, "code", JS_NewInt32(ctx, code));
    JS_SetPropertyStr(ctx, e, "name", JS_NewString(ctx, name));
    JS_SetPropertyStr(ctx, e, "message", JS_NewString(ctx, message));

    return e;
}

static JSValue
node_append_child_or_insert_before(JSContext *ctx, JSValueConst jsThis, int argc,
				   JSValueConst *argv, int append_child)
{
    JSValue val;
    JSValue doc;
    JSValue prop;
    JSValue vret;
    int32_t iret;

#if 0
    FILE *fp = fopen("w3mlog.txt", "a");
    fprintf(fp, "Ref count %d\n", ((JSRefCountHeader*)JS_VALUE_GET_PTR(argv[0]))->ref_count);
    fclose(fp);
#endif

    if (append_child) {
	if (argc < 1) {
	    JS_ThrowTypeError(ctx, "Node.appendChild: Too few arguments.");

	    return JS_EXCEPTION;
	}
    } else {
	if (argc < 2) {
	    JS_ThrowTypeError(ctx, "Node.insertBefore: Too few arguments.");

	    return JS_EXCEPTION;
	} else if (JS_IsNull(argv[1])) {
	    append_child = 1;
	}
    }

    doc = JS_GetPropertyStr(ctx, jsThis, "ownerDocument");
    if (JS_IsObject(doc)) {
	val = JS_GetPropertyStr(ctx, doc, "documentElement");
	if (JS_VALUE_GET_PTR(val) == JS_VALUE_GET_PTR(argv[0])) {
	    JS_FreeValue(ctx, val);
	    JS_Throw(ctx, dom_exception_new(ctx, 3, "HierarchyRequestError",
					    "appendChild: child == documentElement"));

	    return JS_EXCEPTION;
	}
	JS_FreeValue(ctx, val);

	JS_SetPropertyStr(ctx, argv[0], "ownerDocument", doc);
    } else {
	JS_FreeValue(ctx, doc);
    }

    val = JS_GetPropertyStr(ctx, argv[0], "parentNode");
    if (JS_IsObject(val)) {
	JS_FreeValue(ctx, node_remove_child(ctx, val, 1, argv));
    }
    JS_FreeValue(ctx, val);

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
    if (!append_child) {
#if 0
	log_msg("=== BEFORE");
	log_msg(get_cstr(ctx, JS_GetPropertyStr(ctx, argv[0], "tagName")));
	log_msg(get_cstr(ctx, JS_GetPropertyStr(ctx, argv[1], "tagName")));
	JS_EvalThis(ctx, jsThis, "dump_tree(this, \"\");", 20, "<input>", EVAL_FLAG);
#endif
	prop = JS_GetPropertyStr(ctx, val, "indexOf");
	vret = JS_Call(ctx, prop, val, 1, argv + 1);
	JS_FreeValue(ctx, prop);
	if (JS_IsNumber(vret)) {
	    JS_ToInt32(ctx, &iret, vret);
	    if (iret >= 0) {
		JSValue argv2[3];

		argv2[0] = vret;
		argv2[1] = JS_NewInt32(ctx, 0);
		argv2[2] = argv[0];
		prop = JS_GetPropertyStr(ctx, val, "splice");
		JS_FreeValue(ctx, JS_Call(ctx, prop, val, 3, argv2));
		JS_FreeValue(ctx, argv2[1]);
		JS_FreeValue(ctx, prop);
	    } else {
		append_child = 1;
	    }
	}
	JS_FreeValue(ctx, vret);
#if 0

	log_msg("=== AFTER");
	JS_EvalThis(ctx, jsThis, "dump_tree(this, \"\");", 20, "<input>", EVAL_FLAG);
#endif
    }
    if (append_child) {
	prop = JS_GetPropertyStr(ctx, val, "push");
	vret = JS_Call(ctx, prop, val, 1, argv);
	JS_FreeValue(ctx, prop);

	JS_ToInt32(ctx, &iret, vret);
	JS_FreeValue(ctx, vret);

	iret--;
    }
    JS_FreeValue(ctx, val);

#if 0
    fp = fopen("w3mlog.txt", "a");
    fprintf(fp, "Ref count %d after array.push()\n",
	    ((JSRefCountHeader*)JS_VALUE_GET_PTR(argv[0]))->ref_count);
    fclose(fp);
#endif

    if (iret >= 0) {
	char *tag;
	char *script;
	val = JS_GetPropertyStr(ctx, argv[0], "tagName");
	tag = get_cstr(ctx, val);
	JS_FreeValue(ctx, val);

	if (tag != NULL) {
	    if (strcasecmp(tag, "script") == 0) {
		add_node_to(ctx, argv[0], "scripts");
	    } else if (strcasecmp(tag, "img") == 0) {
		add_node_to(ctx, argv[0], "images");
	    } else if (strcasecmp(tag, "a") == 0) {
		/* XXX Ignore <area> tag which has href attribute. */
		add_node_to(ctx, argv[0], "links");
	    } else if (strcasecmp(tag, "form") == 0) {
		JSValue doc1 = js_get_document(ctx);
		JSValue doc2 = JS_GetPropertyStr(ctx, jsThis, "ownerDocument");
		if (JS_VALUE_GET_PTR(doc1) != JS_VALUE_GET_PTR(doc2)) {
		    add_node_to(ctx, argv[0], "forms");
		}
		JS_FreeValue(ctx, doc2);
	    }
#if 0
	    else if (strcasecmp(tag, "select") == 0 ||
		     strcasecmp(tag, "input") == 0 ||
		     strcasecmp(tag, "textarea") == 0 ||
		     strasecmp(tag, "button") == 0) {
		/* XXX should add to document.forms */
	    }
#endif
	}

#if 1
	/* XXX for acid3 test 65 and 69 */
	script = Sprintf("w3m_element_onload(this.children[%d]);", iret)->ptr;
	JS_FreeValue(ctx, backtrace(ctx, script,
				    JS_EvalThis(ctx, jsThis, script, strlen(script),
						"<input>", EVAL_FLAG)));
#endif

	return JS_DupValue(ctx, argv[0]); /* XXX segfault without this by returining argv[0]. */
    } else {
	JS_ThrowTypeError(ctx, "Node.appendChild: Error happens.");

	return JS_EXCEPTION;
    }
}

static JSValue
node_append_child(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    return node_append_child_or_insert_before(ctx, jsThis, argc, argv, 1);
}

static JSValue
node_insert_before(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    return node_append_child_or_insert_before(ctx, jsThis, argc, argv, 0);
}

static JSValue
node_remove_child(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue children, prop, argv2[2];
    int removed = 0;
    char *tag;

    if (argc < 1) {
	JS_ThrowTypeError(ctx, "Node.removeChild: Too few arguments.");

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
	removed = 1;
    }
    JS_FreeValue(ctx, argv2[0]);
    JS_FreeValue(ctx, children);

    tag = get_cstr(ctx, argv[0]);
    if (tag != NULL) {
	if (strcasecmp(tag, "script") == 0) {
	    remove_node_from(ctx, argv[0], "scripts");
	} else if (strcasecmp(tag, "img") == 0) {
	    remove_node_from(ctx, argv[0], "images");
	} else if (strcasecmp(tag, "a") == 0) {
	    /* XXX Ignore <area> tag which has href attribute. */
	    remove_node_from(ctx, argv[0], "links");
	} else  if (strcasecmp(tag, "form") == 0) {
	    JSValue doc1 = js_get_document(ctx);
	    JSValue doc2 = JS_GetPropertyStr(ctx, jsThis, "ownerDocument");
	    if (JS_VALUE_GET_PTR(doc1) != JS_VALUE_GET_PTR(doc2)) {
		remove_node_from(ctx, argv[0], "forms");
	    }
	    JS_FreeValue(ctx, doc2);
	}
#if 0
	else if (strcasecmp(tag, "select") == 0 ||
		 strcasecmp(tag, "input") == 0 ||
		 strcasecmp(tag, "textarea") == 0 ||
		 strasecmp(tag, "button") == 0) {
	    /* XXX should remove from document.forms */
	}
#endif
    }

    if (!removed) {
	JS_Throw(ctx, dom_exception_new(ctx, 8, "NotFoundError",
					"removeChild: child is not found."));

	return JS_EXCEPTION;
    }

    return JS_DupValue(ctx, argv[0]); /* XXX segfault without this by returining argv[0]. */
}

static JSValue
node_replace_child(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue children, prop, idx, ret;
    int i;

    if (argc < 2) {
	JS_ThrowTypeError(ctx, "Node.replaceChild: Too few arguments.");

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
static JSValue element_remove_attribute(JSContext *ctx, JSValueConst jsThis, int argc,
					JSValueConst *argv);
static JSValue element_has_attribute(JSContext *ctx, JSValueConst jsThis, int argc,
				     JSValueConst *argv);

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
node_has_child_nodes(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    const char script[] = "if (this.childNodes.length == 0) { true; } else { false; }";
    return backtrace(ctx, script,
		     JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1, "<input>", EVAL_FLAG));
}

static JSValue
node_first_child_get(JSContext *ctx, JSValueConst jsThis)
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
node_last_child_get(JSContext *ctx, JSValueConst jsThis)
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
element_first_child_get(JSContext *ctx, JSValueConst jsThis)
{
    const char script[] =
	"if (this.children.length > 0) {"
	"  let first = null;"
	"  for (let i = 0; i < this.children.length; i++) {"
	"    if (this.children[i].nodeType == 1 /* ELEMENT_NODE */) {"
	"      first = this.children[i];"
	"      break;"
	"    }"
	"  }"
	"  first;"
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
	"if (this.children.length > 0) {"
	"  let last = null;"
	"  for (let i = this.children.length - 1; i >= 0; i--) {"
	"    if (this.children[i].nodeType == 1 /* ELEMENT_NODE */) {"
	"      last = this.children[i];"
	"      break;"
	"    }"
	"  }"
	"  last;"
	"} else {"
	"  null;"
	"}";

    return backtrace(ctx, script,
		     JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1, "<input>", EVAL_FLAG));
}

static JSValue
node_next_sibling_get(JSContext *ctx, JSValueConst jsThis)
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
node_previous_sibling_get(JSContext *ctx, JSValueConst jsThis)
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
iframe_content_window_get(JSContext *ctx, JSValueConst jsThis)
{
    /* XXX global == window */
    return JS_GetGlobalObject(ctx);
}

static JSValue
element_matches(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    if (argc < 1) {
	JS_ThrowTypeError(ctx, "Element.matches: Too few arguments.");

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
node_compare_document_position(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
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
	JS_ThrowTypeError(ctx, "Element.getElementsByTagName: Too few arguments.");

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
	JS_ThrowTypeError(ctx, "Element.getElementsByClassName: Too few arguments.");

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
node_clone_node(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
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
node_text_content_get(JSContext *ctx, JSValueConst jsThis)
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

static JSValue
element_get_number_of_chars(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    const char script[] = "this.textContent.length;";

    return backtrace(ctx, script,
		     JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1, "<input>", EVAL_FLAG));
}

static JSValue
iframe_get_svg_document(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    return JS_GetPropertyStr(ctx, jsThis, "contentDocument");
}

#ifdef USE_LIBXML2
static void
create_tree_from_html(JSContext *ctx, JSValue parent, const char *html) {
    xmlDoc *doc = htmlReadMemory(html, strlen(html), "", "utf-8",
				 HTML_PARSE_RECOVER | HTML_PARSE_NOERROR |
				 HTML_PARSE_NOWARNING | HTML_PARSE_NODEFDTD
				 /*| HTML_PARSE_NOIMPLIED*/);
    xmlDtd *dtd = xmlGetIntSubset(doc);
    xmlNode *node;

    if (dtd) {
	JSValue val = JS_GetPropertyStr(ctx, parent, "ownerDocument");
	create_dtd(ctx, dtd, val);
	JS_FreeValue(ctx, val);
    }
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
    node = xmlDocGetRootElement(doc);
    if (node) {
	for (node = node->children /* head */; node; node = node->next) {
	    create_tree(ctx, node->children, JS_DupValue(ctx, parent), 1);
	}
    }
    xmlFreeDoc(doc);
    xmlCleanupParser();
}
#endif

static JSValue
node_text_content_set(JSContext *ctx, JSValueConst jsThis, JSValueConst val)
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

	JS_FreeValue(ctx, node_append_child(ctx, jsThis, 1, &element));

	JS_FreeValue(ctx, nodename);
	JS_FreeValue(ctx, element);
    }
#ifdef USE_LIBXML2
    JS_FreeCString(ctx, str);
#endif

    return JS_UNDEFINED;
}

static const JSClassDef NodeClass = {
    "Node", NULL /* finalizer */, NULL /* gc_mark */, NULL /* call */, NULL
};

static JSValue
node_new(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue obj;

    if (argc < 1 || !JS_IsString(argv[0])) {
	JS_ThrowTypeError(ctx, "Node constructor: Too few arguments.");

	return JS_EXCEPTION;
    }

    obj = JS_NewObjectClass(ctx, NodeClassID);
    set_element_property(ctx, obj, argv[0]);

    return obj;
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
node_contains(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    if (argc < 1) {
	JS_ThrowTypeError(ctx, "Node.contains: Too few arguments.");

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
	JS_ThrowTypeError(ctx, "Element.insertAdjacentHtml: Too few arguments.");

	return JS_EXCEPTION;
    }
    if (strcmp(pos, "beforebegin") == 0 || strcmp(pos, "afterend") == 0) {
	parent = JS_GetPropertyStr(ctx, jsThis, "parentElement"); /* XXX */
    } else if (strcmp(pos, "afterbegin") == 0 || strcmp(pos, "beforeend") == 0) {
	parent = JS_DupValue(ctx, jsThis); /* XXX */
    } else {
	JS_ThrowTypeError(ctx, "Element.insertAdjacentHtml: Illegal argument.");

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
	JS_ThrowTypeError(ctx, "Element.insertAdjacentHtml: Too few arguments.");

	return JS_EXCEPTION;
    } else {
	JSValue nodename = JS_NewStringLen(ctx, "#text", 5);
	JSValue element = element_new(ctx, jsThis, 1, &nodename);

	JS_SetPropertyStr(ctx, element, "nodeValue", JS_DupValue(ctx, argv[1]));
	JS_FreeValue(ctx, node_append_child(ctx, jsThis, 1, &element));

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
    const char script[] = "if (this.classList.length > 0) { this.classList.value; } else { undefined; }";
    return backtrace(ctx, script,
		     JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1, "<input>", EVAL_FLAG));
}

static JSValue
element_class_name_set(JSContext *ctx, JSValueConst jsThis, JSValueConst val)
{
    const char *name = JS_ToCString(ctx, val);
    char *script = Sprintf("this.classList.value = \"%s\";",
			   escape_value(Strnew_charp(name))->ptr)->ptr;
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
canvas_element_get_context(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    return canvas_rendering_context2d_new(ctx, jsThis, 0, NULL);
}

static JSValue
canvas_element_to_data_url(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
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
	JS_ThrowTypeError(ctx, "Element.href: The value is not String.");

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

static JSValue
element_child_element_count_get(JSContext *ctx, JSValueConst jsThis)
{
    return JS_EvalThis(ctx, jsThis, "this.children.length;", 21, "<input>", EVAL_FLAG);
}

static const JSCFunctionListEntry NodeFuncs[] = {
    /* EventTarget */
    JS_CFUNC_DEF("addEventListener", 1, add_event_listener),
    JS_CFUNC_DEF("removeEventListener", 1, remove_event_listener),
    JS_CFUNC_DEF("dispatchEvent", 1, dispatch_event),
    JS_CFUNC_DEF("attachEvent", 1, attach_event), /* XXX DEPRECATED */
    JS_CFUNC_DEF("detachEvent", 1, detach_event), /* XXX DEPRECATED */

    /* Node */
    JS_CFUNC_DEF("appendChild", 1, node_append_child),
    JS_CFUNC_DEF("insertBefore", 1, node_insert_before),
    JS_CFUNC_DEF("removeChild", 1, node_remove_child),
    JS_CFUNC_DEF("replaceChild", 1, node_replace_child),
    JS_CFUNC_DEF("hasChildNodes", 1, node_has_child_nodes),
    JS_CGETSET_DEF("firstChild", node_first_child_get, NULL),
    JS_CGETSET_DEF("lastChild", node_last_child_get, NULL),
    JS_CGETSET_DEF("nextSibling", node_next_sibling_get, NULL),
    JS_CGETSET_DEF("previousSibling", node_previous_sibling_get, NULL),
    JS_CFUNC_DEF("compareDocumentPosition", 1, node_compare_document_position),
    JS_CFUNC_DEF("cloneNode", 1, node_clone_node),
    JS_CGETSET_DEF("textContent", node_text_content_get, node_text_content_set),
    JS_CFUNC_DEF("contains", 1, node_contains),
    JS_PROP_INT32_DEF("ELEMENT_NODE", 1, 0),
    JS_PROP_INT32_DEF("ATTRIBUTE_NODE", 2, 0),
    JS_PROP_INT32_DEF("TEXT_NODE", 3, 0),
    JS_PROP_INT32_DEF("CDATA_SECTION_NODE", 4, 0),
    JS_PROP_INT32_DEF("PROCESSING_INSTRUCTION_NODE", 7, 0),
    JS_PROP_INT32_DEF("COMMENT_NODE", 8, 0),
    JS_PROP_INT32_DEF("DOCUMENT_NODE", 9, 0),
    JS_PROP_INT32_DEF("DOCUMENT_TYPE_NODE", 10, 0),
    JS_PROP_INT32_DEF("DOCUMENT_FRAGMENT_NODE", 11, 0),

    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Node", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry ElementFuncs[] = {
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
    JS_CGETSET_DEF("innerHTML", node_text_content_get, node_text_content_set),
    JS_CFUNC_DEF("insertAdjacentHTML", 1, element_insert_adjacent_html),
    JS_CFUNC_DEF("getAttributeNode", 1, element_get_attribute_node),
    JS_CGETSET_DEF("childElementCount", element_child_count_get, NULL),
    JS_CGETSET_DEF("className", element_class_name_get, element_class_name_set),
    JS_CFUNC_DEF("remove", 1, element_remove),
    JS_CGETSET_DEF("childElementCount", element_child_element_count_get, NULL),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "HTMLElement", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry HTMLElementFuncs[] = {
    /* HTMLElement */
    JS_CFUNC_DEF("doScroll", 1, element_do_scroll), /* XXX Obsolete API */
    JS_CGETSET_DEF("offsetParent", element_offset_parent_get, NULL),
    JS_CGETSET_DEF("innerText", node_text_content_get, node_text_content_set),
    JS_CFUNC_DEF("click", 1, element_click),
    JS_CFUNC_DEF("focus", 1, element_focus_or_blur),
    JS_CFUNC_DEF("blur", 1, element_focus_or_blur),

    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "HTMLElement", JS_PROP_CONFIGURABLE),

    /* XXX HTMLAnchorElement */
    JS_CGETSET_DEF("href", element_href_get, element_href_set),

    /* XXX HTMLInputElement */
    JS_CFUNC_DEF("select", 1, element_select),

    /* XXX SVGTextContentElement */
    JS_CFUNC_DEF("getNumberOfChars", 1, element_get_number_of_chars),
};

static const JSCFunctionListEntry HTMLCanvasElementFuncs[] = {
    JS_CFUNC_DEF("getContext", 1, canvas_element_get_context),
    JS_CFUNC_DEF("toDataURL", 1, canvas_element_to_data_url),

    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "HTMLCanvasElement", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry HTMLFormElementFuncs[] = {
    JS_CFUNC_DEF("submit", 1, html_form_element_submit),
    JS_CFUNC_DEF("reset", 1, html_form_element_reset),

    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "HTMLFormElement", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry HTMLSelectElementFuncs[] = {
    JS_CFUNC_DEF("add", 1, node_append_child),

    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "HTMLSelectElement", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry HTMLIFrameElementFuncs[] = {
    JS_CGETSET_DEF("contentWindow", iframe_content_window_get, NULL),
    JS_CFUNC_DEF("getSVGDocument", 1, iframe_get_svg_document),

    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "HTMLIFrameElement", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry HTMLObjectElementFuncs[] = {
    JS_CGETSET_DEF("contentWindow", iframe_content_window_get, NULL),
    JS_CFUNC_DEF("getSVGDocument", 1, iframe_get_svg_document),

    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "HTMLObjectElement", JS_PROP_CONFIGURABLE),
};

static int
call_setter(JSContext *ctx, JSValueConst jsThis, JSValueConst arg, const char *key,
	    const JSCFunctionListEntry *funcs, int num)
{
    size_t i;

    for (i = 0; i < num; i++) {
	if (strcmp(key, funcs[i].name) == 0) {
	    if (funcs[i].def_type == JS_DEF_CGETSET) {
		(*funcs[i].u.getset.set.setter)(ctx, jsThis, JS_DupValue(ctx, arg));
		return 1;
	    }
	}
    }

    return 0;
}

static JSValue
call_getter(JSContext *ctx, JSValueConst jsThis, const char *key,
	    const JSCFunctionListEntry *funcs, int num)
{
    size_t i;

    for (i = 0; i < num; i++) {
	if (strcmp(key, funcs[i].name) == 0) {
	    if (funcs[i].def_type == JS_DEF_CGETSET) {
		return (*funcs[i].u.getset.get.getter)(ctx, jsThis);
	    }
	}
    }

    return JS_NULL;
}

static int
has_getter(JSContext *ctx, JSValueConst jsThis, const char *key,
	    const JSCFunctionListEntry *funcs, int num)
{
    size_t i;

    for (i = 0; i < num; i++) {
	if (strcmp(key, funcs[i].name) == 0) {
	    if (funcs[i].def_type == JS_DEF_CGETSET) {
		JSValue val = (*funcs[i].u.getset.get.getter)(ctx, jsThis);
		if (JS_IsUndefined(val)) {
		    break;
		}
		JS_FreeValue(ctx, val);

		return 1;
	    }
	}
    }

    return 0;
}

static JSValue
element_set_attribute(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    if (argc < 2) {
	JS_ThrowTypeError(ctx, "Element.setAttribute: Too few arguments.");

	return JS_EXCEPTION;
    }

    if (JS_IsString(argv[0])) {
	const char *key = JS_ToCString(ctx, argv[0]);
	int free_cstring_done = 1;

	/* See also w3m_elementAttributes */
	if (strcasecmp(key, "className") == 0 || strcasecmp(key, "htmlFor") == 0 ||
	    strcasecmp(key, "httpEquiv") == 0) {
	    JS_FreeCString(ctx, key);
	    return JS_UNDEFINED;
	} else if (strcasecmp(key, "class") == 0) {
	    JS_FreeCString(ctx, key);
	    key = "className";
	} else if (strcasecmp(key, "for") == 0) {
	    JS_FreeCString(ctx, key);
	    key = "htmlFor";
	} else if (strcasecmp(key, "http-equiv") == 0) {
	    JS_FreeCString(ctx, key);
	    key = "httpEquiv";
	} else {
	    free_cstring_done = 0;

	    if (strncmp(key, "data-", 5) == 0) {
		JSValue dataset = JS_GetPropertyStr(ctx, jsThis, "dataset");
		JS_SetPropertyStr(ctx, dataset, key + 5, JS_DupValue(ctx, argv[1]));
		JS_FreeValue(ctx, dataset);

		goto end;
	    }
	}

	if (!call_setter(ctx, jsThis, argv[1], key, NodeFuncs,
			 sizeof(NodeFuncs) / sizeof(NodeFuncs[0])) &&
	    !call_setter(ctx, jsThis, argv[1], key, ElementFuncs,
			 sizeof(ElementFuncs) / sizeof(ElementFuncs[0]))) {
	    JS_SetPropertyStr(ctx, jsThis, key, JS_DupValue(ctx, argv[1]));
	}

    end:
	if (!free_cstring_done) {
	    JS_FreeCString(ctx, key);
	}
    }

    return JS_UNDEFINED;
}

static JSValue
element_get_attribute(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    if (argc < 1) {
	JS_ThrowTypeError(ctx, "Element.getAttribute: Too few arguments.");

	return JS_EXCEPTION;
    }

    if (JS_IsString(argv[0])) {
	const char *key = JS_ToCString(ctx, argv[0]);
	int free_cstring_done = 1;
	JSValue prop;

	/* See also w3m_elementAttributes */
	if (strcasecmp(key, "className") == 0 || strcasecmp(key, "htmlFor") == 0 ||
	    strcasecmp(key, "httpEquiv") == 0) {
	    JS_FreeCString(ctx, key);
	    return JS_NULL;
	} else if (strcasecmp(key, "class") == 0) {
	    JS_FreeCString(ctx, key);
	    key = "className";
	} else if (strcasecmp(key, "for") == 0) {
	    JS_FreeCString(ctx, key);
	    key = "htmlFor";
	} else if (strcasecmp(key, "http-equiv") == 0) {
	    JS_FreeCString(ctx, key);
	    key = "httpEquiv";
	} else {
	    free_cstring_done = 0;

	    if (strncmp(key, "data-", 5) == 0) {
		JSValue dataset = JS_GetPropertyStr(ctx, jsThis, "dataset");
		prop = JS_GetPropertyStr(ctx, dataset, key + 5);
		JS_FreeValue(ctx, dataset);

		goto end;
	    }
	}

	prop = call_getter(ctx, jsThis, key, NodeFuncs,
			   sizeof(NodeFuncs) / sizeof(NodeFuncs[0]));
	if (JS_IsNull(prop)) {
	    prop = call_getter(ctx, jsThis, key, ElementFuncs,
			       sizeof(ElementFuncs) / sizeof(ElementFuncs[0]));

	    if (JS_IsNull(prop)) {
		prop = JS_GetPropertyStr(ctx, jsThis, key);
	    }
	}

    end:
	if (!free_cstring_done) {
	    JS_FreeCString(ctx, key);
	}

	if (!JS_IsUndefined(prop)) {
	    return prop;
	} else {
	    JS_FreeValue(ctx, prop);
	}
    }

    return JS_NULL;
}

static JSValue
element_remove_attribute(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    log_msg("XXX: Element.removeAttribute");

    if (argc < 1) {
	JS_ThrowTypeError(ctx, "Node.removeAttribute: Too few arguments.");

	return JS_EXCEPTION;
    }

    return JS_UNDEFINED;
}

static JSValue
element_has_attribute(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    if (argc < 1) {
	JS_ThrowTypeError(ctx, "Element.hasAttribute: Too few arguments.");

	return JS_EXCEPTION;
    }

    if (JS_IsString(argv[0])) {
	const char *key = JS_ToCString(ctx, argv[0]);
	int free_cstring_done = 1;
	int has;
	JSValue ret = JS_TRUE;

	/* See also w3m_elementAttributes */
	if (strcasecmp(key, "className") == 0 || strcasecmp(key, "htmlFor") == 0 ||
	    strcasecmp(key, "httpEquiv") == 0) {
	    JS_FreeCString(ctx, key);
	    return JS_FALSE;
	} else if (strcasecmp(key, "class") == 0) {
	    JS_FreeCString(ctx, key);
	    key = "className";
	} else if (strcasecmp(key, "for") == 0) {
	    JS_FreeCString(ctx, key);
	    key = "htmlFor";
	} else if (strcasecmp(key, "http-equiv") == 0) {
	    JS_FreeCString(ctx, key);
	    key = "httpEquiv";
	} else {
	    free_cstring_done = 0;

	    if (strncmp(key, "data-", 5) == 0) {
		JSValue dataset = JS_GetPropertyStr(ctx, jsThis, "dataset");
		JSValue prop = JS_GetPropertyStr(ctx, dataset, key + 5);
		JS_FreeValue(ctx, dataset);
		if (JS_IsUndefined(prop)) {
		    ret = JS_FALSE;
		}
		JS_FreeValue(ctx, prop);

		goto end;
	    }
	}

	has = has_getter(ctx, jsThis, key, NodeFuncs,
			   sizeof(NodeFuncs) / sizeof(NodeFuncs[0]));
	if (!has) {
	    has = has_getter(ctx, jsThis, key, ElementFuncs,
			     sizeof(ElementFuncs) / sizeof(ElementFuncs[0]));

	    if (!has) {
		JSValue prop = JS_GetPropertyStr(ctx, jsThis, key);
		if (JS_IsUndefined(prop)) {
		    ret = JS_FALSE;
		}
		JS_FreeValue(ctx, prop);
	    }
	}

    end:
	if (!free_cstring_done) {
	    JS_FreeCString(ctx, key);
	}

	return ret;
    }

    return JS_FALSE;
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
	JS_ThrowTypeError(ctx, "History.go: Too few arguments.");

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

    return JS_DupValue(ctx, jsThis);
}

static JSValue
document_close(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    log_msg("XXX: Document.close");
    return JS_UNDEFINED;
}

static void
insert_dom_tree(JSContext *ctx, const char *html)
{
    JSValue parent = JS_Eval(ctx, "document.head;", 14, "<input>", EVAL_FLAG);
    create_tree_from_html(ctx, parent, html);
    JS_FreeValue(ctx, parent);
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

    insert_dom_tree(ctx, state->write->ptr);

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
    return location_href_set(ctx, document_location_get(ctx, jsThis), val);
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
	    "  w3m_initDocument(doc);"
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
	    "  w3m_initDocument(doc);"
	    "  w3m_initDocumentTree(doc);"
	    "  doc;"
	    "}";
	return backtrace(ctx, script,
			 JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1,
				     "<input>", EVAL_FLAG));
    }
}

static JSValue
document_current_script_get(JSContext *ctx, JSValueConst jsThis)
{
    const char script[] = "if (this.scripts.length > 0) { this.scripts[0]; } else { null; }";

    return backtrace(ctx, script,
		     JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1, "<input>", EVAL_FLAG));
}

static const JSCFunctionListEntry DocumentFuncs[] = {
    /* override Node */
    JS_CFUNC_DEF("cloneNode", 1, document_clone_node),

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
    JS_CGETSET_DEF("currentScript", document_current_script_get, NULL),

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
	    fwrite(str, strlen(str), 1, fp);
	    JS_FreeCString(ctx, str);
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
    JS_SetPropertyStr(ctx, obj, "timeout", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, obj, "withCredentials", JS_FALSE);

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
	JS_ThrowTypeError(ctx, "XMLHttpRequest.open: Too few arguments.");

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
    JS_FreeCString(ctx, str);
    JS_ThrowTypeError(ctx, "XMLHttpRequest.open: Unknown method.");

    return JS_EXCEPTION;
}

static JSValue
xml_http_request_add_event_listener(JSContext *ctx, JSValueConst jsThis, int argc,
				    JSValueConst *argv)
{
    const char *str;

    if (argc < 2) {
	JS_ThrowTypeError(ctx, "XMLHttpRequest.addEventListener: Too few arguments.");

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
	JS_ThrowTypeError(ctx, "XMLHttpRequest.setRequestHeader: Too few arguments.");

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
    const char *end_events[] = { "progress", "load", "onload", "loadend", "onloadend", "readystatechange", "onreadystatechange" };
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
	    JS_ThrowTypeError(ctx, "XMLHttpRequest.send: Error");

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

    if (state->response_headers->first) {
	JS_SetPropertyStr(ctx, jsThis, "statusText",
			  JS_NewString(ctx, state->response_headers->first->ptr));
    }

    response = JS_NewStringLen(ctx, str->ptr, str->length); /* XXX */
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
	JS_ThrowTypeError(ctx, "XMLHttpRequest.getResponseHeader: Too few arguments.");

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
    int is_running;
} *interval_callbacks;
static u_int num_interval_callbacks;

static int
check_same_callback(JSContext *ctx, JSValueConst func, int *idx)
{
    int i;
    const char *str1 = JS_ToCString(ctx, func);

    *idx = -1;
    for (i = 0; i < num_interval_callbacks; i++) {
	if (interval_callbacks[i].is_running) {
	    /* skip */
	} else 	if (interval_callbacks[i].ctx == NULL) {
	    if (*idx < 0) {
		*idx = i;
	    }
	} else {
	    const char *str2 = JS_ToCString(ctx, interval_callbacks[i].func);
	    if (strcmp(str1, str2) == 0) {
		log_msg(Sprintf("Ignore duplicated interval/timeout callback: %s", str2)->ptr);
		JS_FreeCString(ctx, str2);
		JS_FreeCString(ctx, str1);
		*idx = i;

		return 1;
	    }
	    JS_FreeCString(ctx, str2);
	}
    }
    JS_FreeCString(ctx, str1);

    return 0;
}

static JSValue
set_interval_intern(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv,
		    int is_timeout)
{
    int i;
    int idx;
    void *p;

    if (argc < 1) {
	JS_ThrowTypeError(ctx, is_timeout ? "setTimeout: Too few arguments." :
			                    "setInterval: Too few arguments.");

	return JS_EXCEPTION;
    }

    if (!JS_IsString(argv[0]) && !JS_IsFunction(ctx, argv[0])) {
	log_msg("setInterval: not a function");
	return JS_UNDEFINED;
    }

    if (check_same_callback(ctx, argv[0], &idx)) {
	return JS_NewInt32(ctx, idx + 1);
    } else if (idx >= 0) {
	goto set_callback;
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

#if 0
    {
	const char *str = JS_ToCString(ctx, argv[0]);
	if (is_timeout) {
	    log_msg(Sprintf("Timeout id %d delay %d",
			    idx, interval_callbacks[idx].cur_delay)->ptr);
	} else {
	    log_msg(Sprintf("Interval id %d delay %d",
			    idx, interval_callbacks[idx].cur_delay)->ptr);
	}
	log_msg(str);
	JS_FreeCString(ctx, str);
    }
#endif

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
	JS_ThrowTypeError(ctx, "clearInterval: Too few arguments.");

	return JS_EXCEPTION;
    }

    JS_ToInt32(ctx, &idx, argv[0]);
    idx --;

    if (0 <= idx && idx < num_interval_callbacks && interval_callbacks[idx].ctx != NULL) {
	destroy_interval_callback(interval_callbacks + idx);
    }

    return JS_UNDEFINED;
}

struct interval_callback_sort {
    int cur_delay;
    int idx;
};

int
compare_interval_callback(const void *n1, const void *n2)
{
    if (((struct interval_callback_sort*)n1)->cur_delay >
	((struct interval_callback_sort*)n2)->cur_delay) {
	return 1;
    }
    if (((struct interval_callback_sort*)n1)->cur_delay <
	((struct interval_callback_sort*)n2)->cur_delay) {
	return -1;
    }
    return 0;
}

int
js_trigger_interval(Buffer *buf, int msec, void (*update_forms)(Buffer*, void*))
{
    int i, j;
    int num = num_interval_callbacks;
    struct interval_callback_sort *sort_data = malloc(num * sizeof(*sort_data));
    int executed = 0;
    JSContext *ctx;

    /* 'Promise' works by this. */
    if (JS_ExecutePendingJob(rt, &ctx) < 0) {
	dump_error(ctx, "JS_ExecutePendingJob");
    }

    for (i = 0; i < num; i++) {
	sort_data[i].cur_delay = interval_callbacks[i].cur_delay;
	sort_data[i].idx = i;
    }
    qsort(sort_data, num, sizeof(struct interval_callback_sort), compare_interval_callback);

    for (j = 0; j < num; j++) {
	i = sort_data[j].idx;
	if (interval_callbacks[i].ctx != NULL) {
	    if (interval_callbacks[i].cur_delay > msec) {
		interval_callbacks[i].cur_delay -= msec;
	    } else {
		CtxState *ctxstate;
		JSValue val;

		/* interval_callbacks[i] can be destroyed in JS_Call() below. */
		ctx = interval_callbacks[i].ctx;
		ctxstate = JS_GetContextOpaque(ctx);

		interval_callbacks[i].is_running = 1;
		if (JS_IsString(interval_callbacks[i].func)) {
		    const char *script = JS_ToCString(ctx, interval_callbacks[i].func);
		    val = backtrace(ctx, script,
				    JS_Eval(ctx, script,
					    strlen(script), "<input>", EVAL_FLAG));
		    JS_FreeCString(ctx, script);
		} else {
#ifdef SCRIPT_DEBUG
		    const char *script = JS_ToCString(ctx, interval_callbacks[i].func);
#endif
		    val = backtrace(ctx, script ? script : "interval/timeout func",
				    JS_Call(ctx,
					    interval_callbacks[i].func,
					    JS_NULL, interval_callbacks[i].argc,
					    interval_callbacks[i].argv));
#ifdef SCRIPT_DEBUG
		    JS_FreeCString(ctx, script);
#endif
		}
		interval_callbacks[i].is_running = 0;
		JS_FreeValue(ctx, val);
		if (ctxstate->buf == buf) {
		    if (executed == 0) {
			executed = 1;
			if (update_forms) {
			    update_forms(buf, ctx);
			}
		    }
		}

		/* interval_callbacks[i] can be destroyed in JS_Call() above. */
		if (interval_callbacks[i].ctx != NULL) {
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

    return executed;
}

void
js_reset_interval(JSContext *ctx)
{
    int i;

    for (i = 0; i < num_interval_callbacks; i++) {
	if (interval_callbacks[i].ctx == ctx) {
	    destroy_interval_callback(interval_callbacks + i);
	}
    }
}

static void
create_class(JSContext *ctx, JSValue gl, JSClassID *id, char *name,
	     const JSCFunctionListEntry *funcs, int num_funcs, const JSClassDef *class,
	     JSValue (*ctor_func)(JSContext *, JSValueConst, int, JSValueConst *),
	     JSClassID parent_class)
{
    JSValue proto;
    JSValue ctor;

    if (*id == 0) {
	JS_NewClassID(id);
	JS_NewClass(rt, *id, class);
    }

    if (parent_class != 0) {
	JSValue parent_proto = JS_GetClassProto(ctx, parent_class);
	proto = JS_NewObjectProto(ctx, parent_proto);
	JS_FreeValue(ctx, parent_proto);
    } else {
	proto = JS_NewObject(ctx);
    }

    if (funcs) {
	JS_SetPropertyFunctionList(ctx, proto, funcs, num_funcs);
    }

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
	"globalThis.DOMStringList = class DOMStringList extends DOMTokenList {};"
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
	"    this.code = 0; /* dummy */"
	"    this.INDEX_SIZE_ERR = 1;"
	"    this.HIERARCHY_REQUEST_ERR = 3;"
	"    this.WRONG_DOCUMENT_ERR = 4;"
	"    this.INVALID_CHARACTER_ERR = 5;"
	"    this.NO_MODIFICATION_ALLOWED_ERR = 7;"
	"    this.NOT_FOUND_ERR = 8;"
	"    this.NOT_SUPPORTED_ERR = 9;"
	"    this.INUSE_ATTRIBUTE_ERR = 10;"
	"    this.INVALID_STATE_ERR = 11;"
	"    this.SYNTAX_ERR = 12;"
	"    this.INVALID_MODIFICATION_ERR = 13;"
	"    this.NAMESPACE_ERR = 14;"
	"    this.INVALID_ACCESS_ERR = 15;"
	"    this.SECURITY_ERR = 18;"
	"    this.NETWORK_ERR = 19;"
	"    this.ABORT_ERR = 20;"
	"    this.URL_MISMATCH_ERR = 21;"
	"    this.QUOTA_EXCEEDED_ERR = 22;"
	"    this.TIMEOUT_ERR = 23;"
	"    this.INVALID_NODE_TYPE_ERR = 24;"
	"    this.DATA_CLONE_ERR = 25;"
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
	"globalThis.Event = class Event {"
	"  constructor(type, ...args) {"
	"    this.type = type;"
	"    if (args.length > 0) {"
	"      this.bubbles = args[0].bubbles;"
	"      this.cancelable = args[0].cancelable;"
	"      this.composed = args[0].composed;"
	"    } else {"
	"      this.bubbles = false;"
	"      this.cancelable = false;"
	"      this.composed = false;"
	"    }"
	"  }"
	"  /* DEPRECATED */"
	"  initEvent(type, bubbles, cancelable) {"
	"    this.type = type;"
	"    this.bubbles = bubbles;"
	"    this.cancelable = cancelable;"
	"  }"
	"  preventDefault() {"
	"    console.log(\"XXX: Event.preventDefault\");"
	"  }"
	"};"
	""
	"globalThis.CustomEvent = class CustomEvent extends Event {"
	"  constructor(type, ...args) {"
	"    if (args.length > 0) {"
	"      super(type, args[0]);"
	"      this.detail = args[0].detail;"
	"    } else {"
	"      super(type);"
	"      this.detail = null;"
	"    }"
	"  }"
	"  /* DEPRECATED */"
	"  initCustomEvent(type, bubbles, cancelable, detail) {"
	"    this.type = type;"
	"    this.bubbles = bubbles;"
	"    this.cancelable = cancelable;"
	"    this.detail = detail;"
	"  }"
	"};"
	""
	"globalThis.UIEvent = class UIEvent extends Event {"
	"  constructor(type, ...args) {"
	"    super(type);"
	"    if (args.length > 0) {"
	"      this.detail = args[0].detail;"
	"      this.view = args[0].view;"
	"      this.sourceCapabilities = args[0].sourceCapabilities;"
	"    } else {"
	"      this.detail = 0;"
	"      this.view = null;"
	"      this.sourceCapabilities = null;"
	"    }"
	"  }"
	"  /* DEPRECATED */"
	"  initUIEvent(type, canBubble, cancelable, view, detail) {"
	"    this.type = type;"
	"    this.bubbles = canBubble;"
	"    this.cancelable = cancelable;"
	"    this.view = view;"
	"    this.detail = detail;"
	"  }"
	"};"
	""
	"/* Element.attributes */"
	"function w3m_elementAttributes(obj) {"
	"  let attribute_keys = Object.keys(obj);"
	"  let attrs = new Array(); /* XXX NamedNodeMap */"
	"  for (let i = 0; i < attribute_keys.length; i++) {"
	"    let key = attribute_keys[i];"
	"    let value;"
	"    /* XXX */"
	"    if (key.substring(0, 5) === \"data-\") {"
	"      value = obj.dataset[key.substring(5)];"
	"    } else {"
	"      value = obj[key];"
	"    }"
	"    if (typeof value === \"string\") {"
	"      if (key === \"tagName\" || key === \"innerText\" ||"
	"          key === \"innerHTML\") {"
	"        continue;"
	"      }"
	"      /* see element_{get|set|has}_attribute() */"
	"      else if (key === \"className\") {"
	"        key = \"class\";"
	"      } else if (key === \"htmlFor\") {"
	"        key = \"for\";"
	"      } else if (key === \"httpEquiv\") {"
	"        key = \"http-equiv\";"
	"      }"
	"    } else if (typeof value === \"function\") {"
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
	"  attrs.style = new Object(); /* for MutationObserver.js:218 */"
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
	"    if (typeof init === \"string\") {"
	"      init = init.slice(1);"
	"      let pairs = init.split(\"&\");"
	"      for (let i = 0; i < pairs.length; i++) {"
	"        let array = pairs[i].split(\"=\");"
	"        this.param_keys.push(array[0]);"
	"        if (array.length == 1) {"
	"          this.param_values.push(\"\");"
	"        } else {"
	"          this.param_values.push(array[1]);"
	"        }"
	"      }"
	"    } else if (init.param_keys) {"
	"      /* init == URLSearchParams */"
	"      this.param_keys = Array.from(init.param_keys);"
	"      this.param_values = Array.from(init.param_values);"
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
	"  set(name, value) {"
	"    let i = this.param_keys.indexOf(name);"
	"    if (i >= 0) {"
	"      this.param_values[i] = value;"
	"    } else {"
	"      this.append(name, value);"
	"    }"
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
	"  forEach(func) {"
	"    for (let i = 0; i < this.param_keys.length; i++) {"
	"      func(this.param_values[i], this.param_keys[i]);"
	"    }"
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
	"}"
	""
	"globalThis.PromiseRejectionEvent = class PromiseRejectionEvent extends Event {"
	"  constructor(type, param) {"
	"    super(type);"
	"    this.promise = param.promise;"
	"    this.reason = param.reason;"
	"  }"
	"}";
    const char script2[] =
#if 1
	"function dump_tree(e, head) {"
	"  for (let i = 0; i < e.children.length; i++) {"
	"    console.log(head + e.children[i].tagName + \"(id: \" + e.children[i].id + \" name: \" + e.children[i].name + \" value: \" + e.children[i].nodeValue + \" \" + e.children[i].nodeType + \" \" + e.children[i].onload + \")\");"
	"    if (e.children[i] === e.parentNode) {"
	"      console.log(\"HIERARCHY_ERR\");"
	"    } else {"
	"      dump_tree(e.children[i], head + \" \");"
	"    }"
	"  }"
	"}"
#endif
	"function w3m_initDocumentTree(doc) {"
	"  doc.ownerDocument = doc;"
	"  doc.children = new HTMLCollection();"
	"  doc.childNodes = doc.children; /* XXX NodeList */"
	"  let html = doc.createElement(\"HTML\");"
	"  doc.appendChild(html);"
	"  doc.firstElementChild = doc.lastElementChild = doc.scrollingElement ="
	"    doc.documentElement = html;"
	"  doc.childElementCount = 1;"
	"  doc.documentElement.appendChild(doc.createElement(\"HEAD\"));"
	"  doc.documentElement.appendChild(doc.createElement(\"BODY\"));"
	"  doc.activeElement = doc.body;"
	"  doc.forms = new HTMLCollection();"
	"  doc.scripts = new HTMLCollection();"
	"  doc.images = new HTMLCollection();"
	"  doc.links = new HTMLCollection();"
	"}"
	""
	"function w3m_getElementById(element, id) {"
	"  for (let i = 0; i < element.children.length; i++) {"
	"    if (element.children[i].id === id) {"
	"      return element.children[i];"
	"    }"
	"    let hit = w3m_getElementById(element.children[i], id);"
	"    if (hit != null) {"
	"      return hit;"
	"    }"
	"  }"
	"  return null;"
	"}"
	""
	"function w3m_getElementsByTagName(element, name, elements) {"
	"  for (let i = 0; i < element.children.length; i++) {"
	"    if (name === \"*\" ? element.children[i].nodeType == 1 :"
	"                         element.children[i].tagName === name.toUpperCase()) {"
	"      elements.push(element.children[i]);"
	"    }"
	"    w3m_getElementsByTagName(element.children[i], name, elements);"
	"  }"
	"}"
	""
	"function w3m_getElementsByName(element, name, elements) {"
	"  for (let i = 0; i < element.children.length; i++) {"
	"    if (element.children[i].name === name) {"
	"      elements.push(element.children[i]);"
	"    }"
	"    w3m_getElementsByName(element.children[i], name, elements);"
	"  }"
	"}"
	""
	"function w3m_matchClassName(class1, class2) {"
	"  let class1_array = class1.split(\" \");"
	"  for (let c of class2.split(\" \")) {"
	"    let i;"
	"    for (i = 0; i < class1_array.length; i++) {"
	"      if (c === class1_array[i]) {"
	"        break;"
	"      }"
	"    }"
	"    if (i == class1_array.length) {"
	"      return false;"
	"    }"
	"  }"
	"  return true;"
	"}"
	""
	"function w3m_getElementsByClassName(element, name, elements) {"
	"  for (let i = 0; i < element.children.length; i++) {"
	"    if (element.children[i].className) {"
	"      if (w3m_matchClassName(element.children[i].className, name)) {"
	"        elements.push(element.children[i]);"
	"      }"
	"    }"
	"    w3m_getElementsByClassName(element.children[i], name, elements);"
	"  }"
	"}"
	""
	"function w3m_parseSelector(selector) {"
	"  let array = selector.split(/[ >+]+/);"
	"  for (let i = 0; i < array.length; i++) {"
	"    if (array[i].match((/^[>~+|]+$/)) != null) {"
	"      console.log(\"Maybe unrecognized CSS selector: \" + array[i]);"
	"    }"
	"    let sel = new Object();"
	"    sel.tag = array[i].match(/^[^.#\\[]+/); /* tag */"
	"    if (sel.tag != null) {"
	"      sel.tag = sel.tag[0].toUpperCase();"
	"    }"
	"    sel.class = array[i].match(/\\.[^.#\\[]+/); /* class */"
	"    if (sel.class != null) {"
	"      sel.class = sel.class[0].substring(1);"
	"    }"
	"    sel.id = array[i].match(/#[^.#\\[]+/); /* id */"
	"    if (sel.id != null) {"
	"      sel.id = sel.id[0].substring(1);"
	"    }"
	"    sel.attr_key = array[i].match(/\\[.*\\]/);"
	"    if (sel.attr_key != null) {"
	"      let attr = sel.attr_key[0].substring(1, sel.attr_key[0].length - 1);"
	"      let attr_pair = attr.split(/[~|^$*]*=/);"
	"      sel.attr_key = attr_pair[0];"
	"      if (attr_pair.length > 1) {"
	"        sel.attr_value = attr_pair[1].match(/[^\"]+/)[0];"
	"      } else {"
	"        sel.attr_value = null;"
	"      }"
	"    } else {"
	"      sel.attr_value = null;"
	"    }"
	"    array[i] = sel;"
	"  }"
	"  return array;"
	"}"
	""
	"function w3m_querySelectorAllIntern(element, sel, elements) {"
	"  if (sel.tag) {"
	"    w3m_getElementsByTagName(element, sel.tag, elements);"
	"  } else if (sel.class) {"
	"    w3m_getElementsByClassName(element, sel.class, elements);"
	"  } else if (sel.id) {"
	"    let e = w3m_getElementById(element, sel.id);"
	"    if (e) {"
	"      elements.push(e);"
	"    }"
	"  }"
	"}"
	""
	"function w3m_matchAttr(element, attr_key, attr_value) {"
	"  if (element[attr_key]) {"
	"    if (attr_value == null || attr_value == element[attr_key]) {"
	"      return true;"
	"    }"
	"  }"
	"  return false;"
	"}"
	""
	"function w3m_querySelectorAll(element, sels, elements) {"
	"  if (sels[0]) {"
	"    for (let i = 0; i < element.children.length; i++) {"
	"      if ((sels[0].tag == null || sels[0].tag === element.children[i].tagName) &&"
	"          (sels[0].class == null ||"
	"           (element.children[i].className &&"
	"            w3m_matchClassName(element.children[i].className, sels[0].class))) &&"
	"          (sels[0].id == null || sels[0].id === element.children[i].id) &&"
	"          (sels[0].attr_key == null ||"
	"           w3m_matchAttr(element.children[i], sels[0].attr_key, sels[0].attr_value))) {"
	"        if (sels[1]) {"
	"          w3m_querySelectorAllIntern(element.children[i], sels[1], elements);"
	"        } else {"
	"          elements.push(element.children[i]);"
	"        }"
	"      }"
	"      w3m_querySelectorAll(element.children[i], sels, elements);"
	"    }"
	"  }"
	"}"
	""
	"function w3m_initDocument(doc) {"
	"  doc.nodeName = \"#document\";"
	"  doc.compatMode = \"CSS1Compat\";"
	"  doc.nodeType = 9; /* DOCUMENT_NODE */"
	"  doc.referrer = \"\";"
	"  doc.readyState = \"complete\";"
	"  doc.visibilityState = \"visible\";" /* XXX */
	"  doc.activeElement = null;"
	"  doc.defaultView = window;"
	"  doc.designMode = \"off\";"
	"  doc.dir = \"ltr\";"
	"  doc.hidden = false;"
	"  doc.fullscreenEnabled = false;"
	""
	"  doc.createElement = function(tagname) {"
	"    tagname = tagname.toUpperCase();"
	"    let element;"
	"    if (tagname === \"FORM\") {"
	"      element = new HTMLFormElement();"
	"    } else if (tagname === \"IMG\") {"
	"      element = new HTMLImageElement();"
	"    } else if (tagname === \"SELECT\") {"
	"      element = new HTMLSelectElement();"
	"    } else if (tagname === \"SCRIPT\") {"
	"      element = new HTMLScriptElement();"
	"    } else if (tagname === \"CANVAS\") {"
	"      element = new HTMLCanvasElement();"
	"    } else if (tagname === \"SVG\") {"
	"      element = new SVGElement();"
	"    } else if (tagname === \"IFRAME\") {"
	"      element = new HTMLIFrameElement();"
	"    } else if (tagname === \"OBJECT\") {"
	"      element = new HTMLObjectElement();"
	"    } else {"
	"      element = new HTMLElement(tagname);"
	"    }"
	"    element.ownerDocument = this;"
	"    return element;"
	"  };"
	""
	"  Object.defineProperty(doc, 'body', {"
	"    get: function() {"
	"      for (let i = 0; i < this.documentElement.children.length; i++) {"
	"        if (this.documentElement.children[i].tagName === \"BODY\") {"
	"          return this.documentElement.children[i];"
	"        }"
	"      }"
	"      return null;"
	"    },"
	"    set: function(value) {"
	"      for (let i = 0; i < this.documentElement.children.length; i++) {"
	"        if (this.documentElement.children[i].tagName === \"BODY\") {"
	"          this.removeChild(this.documentElement.children[i]);"
	"          this.appendChild(value);"
	"        }"
	"      }"
	"    }"
	"  });"
	"  Object.defineProperty(doc, 'head', {"
	"    get: function() {"
	"      for (let i = 0; i < this.documentElement.children.length; i++) {"
	"        if (this.documentElement.children[i].tagName === \"HEAD\") {"
	"          return this.documentElement.children[i];"
	"        }"
	"      }"
	"      return null;"
	"    },"
	"    set: function(value) {"
	"      for (let i = 0; i < this.documentElement.children.length; i++) {"
	"        if (this.documentElement.children[i].tagName === \"HEAD\") {"
	"          this.removeChild(this.documentElement.children[i]);"
	"          this.appendChild(value);"
	"        }"
	"      }"
	"    }"
	"  });"
	""
	"  doc.createDocumentFragment = function() {"
	"    let doc = new Document();"
	"    w3m_initDocument(doc);"
	"    w3m_initDocumentTree(doc);"
	"    doc.nodeName = \"#document-fragment\";"
	"    doc.nodeType = 11; /* DOCUMENT_FRAGMENT_NODE */"
	"    return doc;"
	"  };"
	""
	"  doc.implementation = new Object();"
	""
	"  /* XXX */"
	"  doc.implementation.createDocument ="
	"  doc.implementation.createHTMLDocument = function(...args) {"
	"    let doc = new Document();"
	"    w3m_initDocument(doc);"
	"    w3m_initDocumentTree(doc);"
	"    doc.title = \"\";"
	"    if (args.length > 2 && args[2]) {"
	"      args[2].ownerDocument = doc;"
	"    }"
	"    return doc;"
	"  };"
	""
	"  doc.implementation.createDocumentType ="
	"    function(qualifiedNamedStr, publicId, systemId) {"
	"      let type = new Node(\"\");"
	"      type.publicId = publicId;"
	"      type.systemId = systemId;"
	"      type.internalSubset = null;"
	"      type.name = \"html\";"
	"      type.nodeType = 10; /* DOCUMENT_TYPE_NODE */"
	"      /* XXX type.notations */"
	"      return type;"
	"    };"
	""
	"  doc.createElementNS = function(namespaceURI, tagname) {"
	"    let element = this.createElement(tagname);"
	"    element.namespaceURI = namespaceURI;"
	"    return element;"
	"  };"
	""
	"  /* see node_text_content_set() in js_html.c */"
	"  doc.createTextNode = function(text) {"
	"    let element = new HTMLElement(\"#text\");"
	"    element.nodeValue = text;"
	"    element.data = text; /* CharacterData.data */"
	"    element.ownerDocument = this;"
	"    return element;"
	"  };"
	""
	"  doc.createComment = function(data) {"
	"    let element = new HTMLElement(\"#comment\");"
	"    element.nodeValue = data;"
	"    element.data = data; /* CharacterData.data */"
	"    element.ownerDocument = this;"
	"    return element;"
	"  };"
	""
	"  doc.createAttribute = function(name) {"
	"    let attr = new Object();"
	"    attr.name = attr.localName = name;"
	"    attr.specified = true;"
	"    return attr;"
	"  };"
	""
	"  doc.getElementById = function(id) {"
	"    let hit;"
	"    /*"
	"     * Search document.forms first of all because document.children"
	"     * may not have all form elements."
	"     * (see create_tree() in js_html.c)"
	"     */"
	"    for (let i = 0; i < this.forms.length; i++) {"
	"      hit = w3m_getElementById(this.forms[i], id);"
	"      if (hit) {"
	"        return hit;"
	"      }"
	"    }"
	"    hit = w3m_getElementById(this, id);"
#ifdef USE_LIBXML2
	"    return hit;"
#else
	"    if (hit != null) {"
	"      return hit;"
	"    }"
	"    let element = this.createElement(\"SPAN\");"
	"    element.id = id;"
	"    element.value = \"\";"
	"    return this.body.appendChild(element);"
#endif
	"  };"
	""
	"  doc.getElementsByTagName = function(name) {"
	"    name = name.toUpperCase();"
	"    if (name === \"SCRIPT\") {"
	"      return this.scripts;"
	"    } else if (name === \"IMG\") {"
	"      return this.images;"
	"    }"
	"    /* this.links can contain not only a tag but also area tag with href attribute. */"
#if 0
	"    else if (name === \"A\") {"
	"      return this.links;"
	"    }"
#endif
	"    else if (name === \"FORM\") {"
	"      return this.forms;"
	"    } else {"
	"      let elements = new HTMLCollection();"
	"      if (name === \"HTML\") {"
	"        elements.push(this.documentElement);"
	"      } else {"
	"        w3m_getElementsByTagName(this, name, elements);"
#ifndef USE_LIBXML2
	"        if (elements.length == 0) {"
	"          if (name === \"*\") {"
	"            name = \"SPAN\";"
	"          }"
	"          let element = this.createElement(name);"
	"          element.value = \"\";"
	"          this.body.appendChild(element);"
	"          elements.push(element);"
	"        }"
#endif
	"      }"
	"      return elements;"
	"    }"
	"  };"
	""
	"  doc.getElementsByName = function(name) {"
	"    let elements = new HTMLCollection();"
	"    /*"
	"     * document.forms is not searched because some of them are added"
	"     * to document.children (see create_tree() in js_html.c) and it"
	"     * is difficult to identify them by name attribute."
	"     */"
	"    w3m_getElementsByName(document, name, elements);"
#ifndef USE_LIBXML2
	"    if (elements.length == 0) {"
	"      let element = this.createElement(\"SPAN\");"
	"      element.name = name;"
	"      element.value = \"\";"
	"      this.body.appendChild(element);"
	"      elements.push(element);"
	"    }"
#endif
	"    return elements;"
	"  };"
	""
	"  doc.getElementsByClassName = function(name) {"
	"    let elements = new HTMLCollection();"
	"    w3m_getElementsByClassName(this, name, elements);"
#ifndef USE_LIBXML2
	"    if (elements.length == 0) {"
	"      let element = this.createElement(\"SPAN\");"
	"      element.className = name;"
	"      element.value = \"\";"
	"      this.body.appendChild(element);"
	"      elements.push(element);"
	"    }"
#endif
	"    return elements;"
	"  };"
	""
	"  doc.querySelectorAll = function(sel) {"
	"    let elements = new NodeList();"
	"    for (let s of sel.split(/ *,/)) {"
	"      /* XXX */"
	"      if (s.toUpperCase() === \"HTML\") {"
	"        elements.push(this.documentElement);"
	"      } else {"
	"        w3m_querySelectorAll(this, w3m_parseSelector(s), elements);"
	"      }"
	"    }"
#ifndef USE_LIBXML2
	"    if (elements.length == 0) {"
	"        let element = this.createElement(\"SPAN\");"
	"        element.value = \"\";"
	"        elements.push(this.body.appendChild(element));"
	"    }"
#endif
	"    return elements;"
	"  };"
	""
	"  doc.querySelector = function(sel) {"
	"    let elements = this.querySelectorAll(sel);"
	"    if (elements.length == 0) {"
	"      return null;"
	"    } else {"
	"      return elements[0];"
	"    }"
	"  };"
	""
	"  /* DEPRECATED */"
	"  doc.createEvent = function(type) {"
	"    if (type === \"UIEvent\" || type === \"UIEvents\") {"
	"      return new UIEvent(type);"
	"    } else if (type === \"CustomEvent\" || type === \"CustomEvents\") {"
	"      return new CustomEvent(type);"
	"    } else {"
	"      return new Event(type);"
	"    }"
	"  };"
	""
	"  doc.createRange = function() {"
	"    let r = new Range();"
	"    r.setStart(this, 0);"
	"    r.setEnd(this, 0);"
	"    return r;"
	"  };"
	""
	"  doc.createNodeIterator = function(root, ...args) {"
	"    return new NodeIterator(root, args[0], args[1]);"
	"  };"
	""
	"  doc.execCommand = function(cmdName, showDefaultUI, valueArgument) {"
	"    console.log(\"XXX Document.execCommand (DEPRECATED)\");"
	"    return false;"
	"  };"
	""
	"  doc.elementFromPoint = function(x, y) {"
	"    return this.body;"
	"  };"
	"}"
	""
	"function w3m_textNodesToStr(node) {"
	"  let str = \"\";"
	"  for (let i = 0; i < node.childNodes.length; i++) {"
	"    if (node.childNodes[i].nodeName === \"#text\" &&"
	"        node.childNodes[i].nodeValue != null &&"
	"        node.childNodes[i].isModified == true) {"
	"      if (node.name != undefined) {"
	"        str += node.name;"
	"        str += \"=\";"
	"      } else if (node.id != undefined) {"
	"        str += node.id;"
	"        str += \"=\";"
	"      }"
	"      str += node.childNodes[i].nodeValue;"
	"      str += \" \";"
	"      node.childNodes[i].isModified = false;"
	"    }"
	"    str += w3m_textNodesToStr(node.childNodes[i]);"
	"  }"
	"  return str;"
	"}"
	""
	"const w3m_base64ConvTable ="
	"  \"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/\";"
	"function w3m_btoaChar(i) {"
	"  if (i >= 0 && i < 64) {"
	"    return w3m_base64ConvTable[i];"
	"  }"
	"  return undefined;"
	"}"
	"function w3m_atobIndex(chr) {"
	"  let i = w3m_base64ConvTable.indexOf(chr);"
	"  return i < 0 ? undefined : i;"
	"}"
	"function atob(data) {"
	"  data = `${data}`;"
	"  data = data.replace(/[ \\t\\n\\f\\r]/g, \"\");"
	"  if (data.length % 4 === 0) {"
	"    data = data.replace(/==?$/, "");"
	"  }"
	"  if (data.length % 4 === 1 || /[^+/0-9A-Za-z]/.test(data)) {"
	"    return null;"
	"  }"
	"  let out = \"\";"
	"  let buf = 0;"
	"  let nbits = 0;"
	"  for (let i = 0; i < data.length; i++) {"
	"    buf <<= 6;"
	"    buf |= w3m_atobIndex(data[i]);"
	"    nbits += 6;"
	"    if (nbits === 24) {"
	"      out += String.fromCharCode((buf & 0xff0000) >> 16);"
	"      out += String.fromCharCode((buf & 0xff00) >> 8);"
	"      out += String.fromCharCode(buf & 0xff);"
	"      buf = nbits = 0;"
	"    }"
	"  }"
	"  if (nbits === 12) {"
	"    buf >>= 4;"
	"    out += String.fromCharCode(buf);"
	"  } else if (nbits === 18) {"
	"    buf >>= 2;"
	"    out += String.fromCharCode((buf & 0xff00) >> 8);"
	"    out += String.fromCharCode(buf & 0xff);"
	"  }"
	"  return out;"
	"}"
	"function btoa(data) {"
	"  data = `${data}`;"
	"  for (let i = 0; i < data.length; i++) {"
	"    if (data.charCodeAt(i) > 255) {"
	"      return null;"
	"    }"
	"  }"
	"  let out = \"\";"
	"  for (let i = 0; i < data.length; i += 3) {"
	"    let set = [undefined, undefined, undefined, undefined];"
	"    set[0] = data.charCodeAt(i) >> 2;"
	"    set[1] = (data.charCodeAt(i) & 0x03) << 4;"
	"    if (data.length > i + 1) {"
	"      set[1] |= data.charCodeAt(i + 1) >> 4;"
	"      set[2] = (data.charCodeAt(i + 1) & 0x0f) << 2;"
	"    }"
	"    if (data.length > i + 2) {"
	"      set[2] |= data.charCodeAt(i + 2) >> 6;"
	"      set[3] = data.charCodeAt(i + 2) & 0x3f;"
	"    }"
	"    for (let j = 0; j < set.length; j++) {"
	"      if (typeof set[j] === \"undefined\") {"
	"        out += \"=\";"
	"      } else {"
	"        out += w3m_btoaChar(set[j]);"
	"      }"
	"    }"
	"  }"
	"  return out;"
	"}"
	""
	"function w3m_onload(obj) {"
	"  if (obj.w3m_events) {"
	"    /*"
	"     * 'i = 0; ...;i++' falls infinite loop if a listener calls addEventListener()."
	"     */"
	"    for (let i = obj.w3m_events.length - 1; i >= 0; i--) {"
	"      let event = obj.w3m_events[i];"
	"      obj.w3m_events.splice(i, 1);"
	"      if (event.type === \"loadstart\" ||"
	"          event.type === \"load\" ||"
	"          event.type === \"loadend\" ||"
	"          event.type === \"DOMContentLoaded\" ||"
	"          event.type === \"readystatechange\" ||"
	"          event.type === \"visibilitychange\") {"
	"        if (typeof event.listener === \"function\") {"
	"          event.listener(event);"
	"        } else if (event.listener.handleEvent &&"
	"                   typeof event.listener.handleEvent === \"function\") {"
	"          event.listener.handleEvent(event);"
	"        }"
	"      }"
	"      /* In case a listener calls removeEventListener(). */"
	"      if (i > obj.w3m_events.length) {"
	"        i = obj.w3m_events.length;"
	"      }"
	"    }"
	"  }"
	"  if (obj.onloadstart) {"
	"    if (typeof obj.onloadstart === \"string\") {"
	"      try {"
	"        eval(obj.onloadstart);"
	"      } catch (e) {"
	"        console.log(\"Error in onloadstart: \" + e.message);"
	"      }"
	"    } else if (typeof obj.onloadstart === \"function\") {"
	"      obj.onloadstart();"
	"    }"
	"    obj.onloadstart = undefined;"
	"  }"
	"  if (obj.onload) {"
	"    if (typeof obj.onload === \"string\") {"
	"      try {"
	"        eval(obj.onload);"
	"      } catch (e) {"
	"        console.log(\"Error in onload: \" + e.message);"
	"        console.log(obj.onload);"
	"      }"
	"    } else if (typeof obj.onload === \"function\") {"
	"      obj.onload();"
	"    }"
	"    obj.onload = undefined;"
	"  }"
	"  if (obj.onloadend) {"
	"    if (typeof obj.onloadend === \"string\") {"
	"      try {"
	"        eval(obj.onloadend);"
	"      } catch (e) {"
	"        console.log(\"Error in onloadend: \" + e.message);"
	"      }"
	"    } else if (typeof obj.onloadend === \"function\") {"
	"      obj.onloadend();"
	"    }"
	"    obj.onloadend = undefined;"
	"  }"
	"  if (obj.onreadystatechange) {"
	"    if (typeof obj.onreadystatechange === \"string\") {"
	"      try {"
	"        eval(obj.onreadystatechange);"
	"      } catch (e) {"
	"        console.log(\"Error in onreadystatechange: \" + e.message);"
	"      }"
	"    } else if (typeof obj.onreadystatechange === \"function\") {"
	"      obj.onreadystatechange();"
	"    }"
	"    obj.onreadystatechange = undefined;"
	"  }"
	"}"
	"function w3m_element_onload(element) {"
	"  w3m_onload(element);"
	"  for (let i = 0; i < element.children.length; i++) {"
	"    w3m_element_onload(element.children[i]);"
	"  }"
	"}"
	"function w3m_reset_onload(obj) {"
	"  if (obj.w3m_events) {"
	"    obj.w3m_events = undefined;"
	"  }"
	"  if (obj.onloadstart) {"
	"    obj.w3m_onloadstart = undefined;"
	"  }"
	"  if (obj.onload) {"
	"    obj.w3m_onload = undefined;"
	"  }"
	"  if (obj.onloadend) {"
	"    obj.w3m_onloadend = undefined;"
	"  }"
	"  if (obj.onreadystatechange) {"
	"    obj.onreadystatechange = undefined;"
	"  }"
	"}"
	""
	"function w3m_invoke_event(obj, evtype) {"
	"  if (obj.w3m_events) {"
	"    for (let i = obj.w3m_events.length - 1; i >= 0; i--) {"
	"      let event = obj.w3m_events[i];"
	"      if (event.type === evtype) {"
	"        if (typeof event.listener === \"function\") {"
	"          event.listener(event);"
	"        } else if (event.listener.handleEvent &&"
	"                   typeof event.listener.handleEvent === \"function\") {"
	"          event.listener.handleEvent(event);"
	"        }"
	"      }"
	"      /* In case a listener calls removeEventListener(). */"
	"      if (i > obj.w3m_events.length) {"
	"        i = obj.w3m_events.length;"
	"      }"
	"    }"
	"  }"
	"  evtype = \"on\" + evtype;"
	"  if (obj[evtype]) {"
	"    if (typeof obj[evtype] === \"string\") {"
	"      try {"
	"        eval(obj[evtype]);"
	"      } catch (e) {"
	"        console.log(\"Error in \" + evtype + \": \" + e.message);"
	"      }"
	"    } else if (typeof obj[evtype] === \"function\") {"
	"      obj[evtype]();"
	"    }"
	"  }"
	"}"
	"function w3m_element_invoke_event(element, evtype) {"
	"  w3m_invoke_event(element, evtype);"
	"  for (let i = 0; i < element.children.length; i++) {"
	"    w3m_element_invoke_event(element.children[i], evtype);"
	"  }"
	"}"
	""
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
    const char script3[] =
	"globalThis.IDBRequest = class IDBRequest {"
	"  constructor(result, source) {"
	"    this.result = result;"
	"    this.readyState = \"done\";"
	"    this.source = source;"
	"  }"
	"  get transaction() {"
	"    console.log(\"XXX: IDBRequest.transaction\");"
	"    return null;"
	"  }"
	"};"
	"globalThis.IDBOpenDBRequest = class IDBOpenDBRequest extends IDBRequest {};"
	"var w3m_requests = new Array();"
	"function w3m_idbrequest(result, source) {"
	"  let req = IDBRequest(result, source);"
	"  w3m_requests.push(req);"
	"  return req;"
	"}"
	"function w3m_invoke_idbrequests() {"
	"  if (w3m_requests.length > 0) {"
	"    for (let i = 0; i < w3m_requests.length; i++) {"
	"      if (w3m_requests[i].onsuccess) {"
	"        w3m_requests[i].onsuccess();"
	"      }"
	"    }"
	"    w3m_requests = new Array();"
	"  }"
	"}"
	"globalThis.IDBIndex = class IDBIndex {"
	"  constructor(name, keyPath, objectStore) {"
	"    this.name = name;"
	"    this.keyPath = keyPath;"
	"    this.objectStore = objectStore;"
	"  }"
	"  get(...args) {"
	"    if (args.length > 0) {"
	"      /* XXX */"
	"      return this.objectStore.get(key);"
	"    } else {"
	"      return w3m_idbrequest(null, this);"
	"    }"
	"  }"
	"};"
	"globalThis.IDBObjectStore = class IDBObjectStore {"
	"  constructor(name, ...args) {"
	"    if (args.length > 0) {"
	"      this.autoIncrement = args[0].autoIncrement;"
	"      this.keyPath = args[0].keyPath;"
	"      this.keys = null;"
	"    } else {"
	"      this.autoIncrement = false;"
	"      this.keyPath = null;"
	"      this.keys = new Array();"
	"    }"
	"    this.values = new Array();"
	"    this.incrementKey = 0;"
	"    this.name = name;"
	"  }"
	"  put(item, ...args) {"
	"    let keyval;"
	"    if (args.length == 0) {"
	"      keyval = this.incrementKey + 1;"
	"    } else {"
	"      keyval = args[0];"
	"    }"
	"    if (this.keys) {"
	"      let i;"
	"      if (args.length > 0 && (i = this.keys.indexOf(keyval)) >= 0) {"
	"        this.keys[i] = keyval;"
	"      } else {"
	"        this.keys.push(keyval);"
	"      }"
	"    } else {"
	"      item[this.keyPath] = keyval;"
	"    }"
	"    this.values.push(item);"
	"    return w3m_idbrequest(null, this);"
	"  }"
	"  add(value, ...args) {"
	"    let keyval;"
	"    if (args.length == 0) {"
	"      keyval = this.incrementKey + 1;"
	"    } else {"
	"      keyval = args[0];"
	"    }"
	"    if (this.keys) {"
	"      let i;"
	"      if (args.length > 0 && (i = this.keys.indexOf(keyval)) >= 0) {"
	"        throw \"ConstraintError\";"
	"      } else {"
	"        this.keys.push(keyval);"
	"      }"
	"    } else {"
	"      if (item[this.keyPath] == undefined) {"
	"        throw \"ConstraintError\";"
	"      }"
	"      item[this.keyPath] = keyval;"
	"    }"
	"    this.values.push(item);"
	"    return w3m_idbrequest(null, this);"
	"  }"
	"  get(key) {"
	"    let i;"
	"    if (this.key) {"
	"      for (i = 0; i < this.keys.length; i++) {"
	"        if (this.keys[i] === key) {"
	"          return w3m_idbrequest(this.values[i], this);"
	"        }"
	"      }"
	"    } else {"
	"      for (i = 0; i < this.values.length; i++) {"
	"        if (this.values[i][this.keyPath] === key) {"
	"          return w3m_idbrequest(this.values[i], this);"
	"        }"
	"      }"
	"    }"
	"  }"
	"  getAll() {"
	"    return w3m_idbrequest(this.values, this);"
	"  }"
	"  index(name) {"
	"    return new IDBIndex(name, this.keyPath, this);"
	"  }"
	"};"
	"globalThis.IDBTransaction = class IDBTransaction {"
	"  constructor(stores) {"
	"    this.stores = stores;"
	"  }"
	"  objectStore(name) {"
	"    for (let i = 0; i < this.stores.length; i++) {"
	"      if (this.stores[i].name === name) {"
	"        return this.stores[i];"
	"      }"
	"    }"
	"    throw \"Not found\";"
	"  }"
	"};"
	"globalThis.IDBDatabase = class IDBDatabase {"
	"  constructor() {"
	"    this.objectStoreNames = new DOMStringList();"
	"    this.objectStores = new Array();"
	"  }"
	"  createObjectStore(name, ...args) {"
	"    let store;"
	"    if (args.length > 0) {"
	"      store = new IDBObjectStore(name, args[0]);"
	"    } else {"
	"      store = new IDBObjectStore(name);"
	"    }"
	"    this.objectStores.push(name);"
	"    this.objectStoreNames.push(store);"
	"    return store;"
	"  }"
	"  deleteObjectStore(name) {"
	"    for (let i = 0; i < this.objectStoreNames.length; i++) {"
	"      if (this.objectStoreNames[i] === name) {"
	"        this.objectStoreNames.splice(i, 1);"
	"        this.objectStores(i, 1);"
	"        break;"
	"      }"
	"    }"
	"  }"
	"  transaction(storeNames, ...args) {"
	"    let array = new Array();"
	"    if (!Array.isArray(storeNames)) {"
	"      for (let i = 0; i < this.objectStores.length; i++) {"
	"        if (this.objectStores[i].name === storeNames) {"
	"          array.push(this.objectStores[i]);"
	"        }"
	"      }"
	"    } else {"
	"      for (let i = 0; i < this.objectStores.length; i++) {"
	"        for (let j = 0; j < storeNames.length; j++) {"
	"          if (this.objectStores[i].name === storeNames[j]) {"
	"            array.push(this.objectStores[i]);"
	"            break;"
	"          }"
	"        }"
	"      }"
	"    }"
	"    return new IDBTransaction(array);"
	"  }"
	"};"
	"globalThis.IDBFactory = class IDBFactory {"
	"  static open(name, version) {"
	"    return new IDBOpenDBRequest(new IBDatabase(), this);"
	"  }"
	"  static deleteDatabase(name, args) {"
	"    return new IDBOpenDBRequest(null, this);"
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
#if 0
	JS_SetCanBlock(rt, TRUE);
#endif
    }

    ctx = JS_NewContext(rt);
    gl = JS_GetGlobalObject(ctx);

    JS_FreeValue(ctx, backtrace(ctx, script,
				JS_Eval(ctx, script, sizeof(script) - 1, "<input>", EVAL_FLAG)));

    create_class(ctx, gl, &LocationClassID, "Location", LocationFuncs,
		 sizeof(LocationFuncs) / sizeof(LocationFuncs[0]), &LocationClass,
		 location_new, 0);

    create_class(ctx, gl, &NodeClassID, "Node", NodeFuncs,
		 sizeof(NodeFuncs) / sizeof(NodeFuncs[0]), &NodeClass,
		 node_new, 0);

    create_class(ctx, gl, &ElementClassID, "Element", ElementFuncs,
		 sizeof(ElementFuncs) / sizeof(ElementFuncs[0]), &ElementClass,
		 element_new, NodeClassID);

    create_class(ctx, gl, &HTMLElementClassID, "HTMLElement", HTMLElementFuncs,
		 sizeof(HTMLElementFuncs) / sizeof(HTMLElementFuncs[0]), &HTMLElementClass,
		 html_element_new, ElementClassID);

    create_class(ctx, gl, &HTMLFormElementClassID, "HTMLFormElement", HTMLFormElementFuncs,
		 sizeof(HTMLFormElementFuncs) / sizeof(HTMLFormElementFuncs[0]),
		 &HTMLFormElementClass, html_form_element_new, HTMLElementClassID);

    create_class(ctx, gl, &HTMLImageElementClassID, "HTMLImageElement", NULL, 0,
		 &HTMLImageElementClass, html_image_element_new, HTMLElementClassID);

    create_class(ctx, gl, &HTMLSelectElementClassID, "HTMLSelectElement", HTMLSelectElementFuncs,
		 sizeof(HTMLSelectElementFuncs) / sizeof(HTMLSelectElementFuncs[0]),
		 &HTMLSelectElementClass, html_select_element_new, HTMLElementClassID);

    create_class(ctx, gl, &HTMLScriptElementClassID, "HTMLScriptElement", NULL, 0,
		 &HTMLScriptElementClass, html_script_element_new, HTMLElementClassID);

    create_class(ctx, gl, &HTMLCanvasElementClassID, "HTMLCanvasElement", HTMLCanvasElementFuncs,
		 sizeof(HTMLCanvasElementFuncs) / sizeof(HTMLCanvasElementFuncs[0]),
		 &HTMLCanvasElementClass, html_canvas_element_new, HTMLElementClassID);

    create_class(ctx, gl, &SVGElementClassID, "SVGElement", NULL, 0,
		 &SVGElementClass, svg_element_new, HTMLElementClassID);

    create_class(ctx, gl, &HTMLIFrameElementClassID, "HTMLIFrameElement", HTMLIFrameElementFuncs,
		 sizeof(HTMLIFrameElementFuncs) / sizeof(HTMLIFrameElementFuncs[0]),
		 &HTMLIFrameElementClass, html_iframe_element_new, HTMLElementClassID);

    create_class(ctx, gl, &HTMLObjectElementClassID, "HTMLObjectElement", HTMLObjectElementFuncs,
		 sizeof(HTMLObjectElementFuncs) / sizeof(HTMLObjectElementFuncs[0]),
		 &HTMLObjectElementClass, html_object_element_new, HTMLElementClassID);

    create_class(ctx, gl, &DocumentClassID, "Document", DocumentFuncs,
		 sizeof(DocumentFuncs) / sizeof(DocumentFuncs[0]), &DocumentClass,
		 document_new, NodeClassID);

    create_class(ctx, gl, &HistoryClassID, "History", HistoryFuncs,
		 sizeof(HistoryFuncs) / sizeof(HistoryFuncs[0]), &HistoryClass,
		 history_new, 0);

    create_class(ctx, gl, &NavigatorClassID, "Navigator", NavigatorFuncs,
		 sizeof(NavigatorFuncs) / sizeof(NavigatorFuncs[0]), &NavigatorClass,
		 navigator_new, 0);

    create_class(ctx, gl, &XMLHttpRequestClassID, "XMLHttpRequest", XMLHttpRequestFuncs,
		 sizeof(XMLHttpRequestFuncs) / sizeof(XMLHttpRequestFuncs[0]), &XMLHttpRequestClass,
		 xml_http_request_new, 0);

    create_class(ctx, gl, &CanvasRenderingContext2DClassID, "CanvasRenderingContext2D",
		 CanvasRenderingContext2DFuncs,
		 sizeof(CanvasRenderingContext2DFuncs) / sizeof(CanvasRenderingContext2DFuncs[0]),
		 &CanvasRenderingContext2DClass, canvas_rendering_context2d_new, 0);

    JS_FreeValue(ctx, backtrace(ctx, script2,
				JS_Eval(ctx, script2, sizeof(script2) - 1, "<input>", EVAL_FLAG)));
    JS_FreeValue(ctx, backtrace(ctx, script3,
				JS_Eval(ctx, script3, sizeof(script3) - 1, "<input>", EVAL_FLAG)));

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
    JS_SetPropertyStr(ctx, gl, "requestAnimationFrame",
		      JS_NewCFunction(ctx, set_timeout, "requestAnimationFrame", 1));
    JS_SetPropertyStr(ctx, gl, "clearInterval",
		      JS_NewCFunction(ctx, clear_interval, "clearInterval", 1));
    JS_SetPropertyStr(ctx, gl, "clearTimeout",
		      JS_NewCFunction(ctx, clear_interval, "clearTimeout", 1));
    JS_SetPropertyStr(ctx, gl, "cancelAnimationFrame",
		      JS_NewCFunction(ctx, clear_interval, "cancelAnimationFrame", 1));

    JS_FreeValue(ctx, gl);

    ctxstate = (CtxState*)GC_MALLOC_UNCOLLECTABLE(sizeof(*ctxstate));
    for (i = 0; i < NUM_TAGNAMES; i++) {
	ctxstate->tagnames[i] = JS_UNDEFINED;
    }
    ctxstate->funcs = NULL;
    ctxstate->nfuncs = 0;
    ctxstate->buf = buf;
    ctxstate->document = JS_UNDEFINED;
    JS_SetContextOpaque(ctx, ctxstate);

    js_eval(ctx,
	    "Object.setPrototypeOf(globalThis, setInterval);"
	    "Object.setPrototypeOf(globalThis, setTimeout);"
	    "Object.setPrototypeOf(globalThis, requestAnimationFrame);"
	    "Object.setPrototypeOf(globalThis, clearInterval);"
	    "Object.setPrototypeOf(globalThis, clearTimeout);"
	    "Object.setPrototypeOf(globalThis, cancelAnimationFrame);");

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
    JS_FreeValue(ctx, ctxstate->document);
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
    const char seq[] = ".toRealArray(arguments).slice(1).forEach(function(t){";
    Str str = Strnew();
    while ((p = strstr(beg, seq))) {
	Strcat_charp_n(str, beg, p - beg + sizeof(seq) - 1);
	Strcat_charp(str, "var zzz=console.log(\"HELO\" + t.screenName)),");
        beg = p + sizeof(seq) - 1;
    }
    Strcat_charp(str, beg);
    script = str->ptr;
#elif 0
    char *beg = script;
    char *p;
    /*const char seq[] = "if(4===u.readyState){var r=m(e,u);0===u.status?n(r):t(r)";*/
    const char seq[] = "var t=e.headers,n=t&&t[\"content-type\"];";
    Str str = Strnew();
    while ((p = strstr(beg, seq))) {
	Strcat_charp_n(str, beg, p - beg + sizeof(seq) - 1);
	Strcat_charp(str, "console.log(\"content-type:\" + n);if (typeof n === \"string\") console.log(JSON.parse(e.body));");
        beg = p + sizeof(seq) - 1;
    }
    Strcat_charp(str, beg);
    script = str->ptr;

    beg = script;
    const char seq2[] = "|void 0===I?void 0:I.ok;";
    str = Strnew();
    while ((p = strstr(beg, seq2))) {
	Strcat_charp_n(str, beg, p - beg + sizeof(seq2) - 1);
	Strcat_charp(str, "console.log(\"TEST0\" + ge);");
        beg = p + sizeof(seq2) - 1;
    }
    Strcat_charp(str, beg);
    script = str->ptr;

    beg = script;
    const char seq3[] = "re=e.withheld_text;";
    str = Strnew();
    while ((p = strstr(beg, seq3))) {
	Strcat_charp_n(str, beg, p - beg + sizeof(seq3) - 1);
	Strcat_charp(str, "console.log(\"TEST1 \" + re + X);");
        beg = p + sizeof(seq3) - 1;
    }
    Strcat_charp(str, beg);
    script = str->ptr;

    beg = script;
    const char seq4[] = "(ue.enrichments={interactive_text_enrichment:{interactive_texts:xe}})}";
    str = Strnew();
    while ((p = strstr(beg, seq4))) {
	Strcat_charp_n(str, beg, p - beg + sizeof(seq4) - 1);
	Strcat_charp(str, "console.log((0,l.Z)(ue,t,n));console.log(\"TEST2\");");
        beg = p + sizeof(seq4) - 1;
    }
    Strcat_charp(str, beg);
    script = str->ptr;

    beg = script;
    const char seq6[] = "l=this._processStrategy(e,t,r);";
    str = Strnew();
    while ((p = strstr(beg, seq6))) {
	Strcat_charp_n(str, beg, p - beg + sizeof(seq6) - 1);
	Strcat_charp(str, "console.log(\"TEST2.5\" + this.schema);");
        beg = p + sizeof(seq6) - 1;
    }
    Strcat_charp(str, beg);
    script = str->ptr;

    beg = script;
    const char seq5[] = "we.collaborator_users);";
    str = Strnew();
    while ((p = strstr(beg, seq5))) {
	Strcat_charp_n(str, beg, p - beg + sizeof(seq5) - 1);
	Strcat_charp(str, "console.log(\"TEST3 \" + K);try { throw new Error(\"error\"); } catch (e) { console.log(e.stack); }");
        beg = p + sizeof(seq5) - 1;
    }
    Strcat_charp(str, beg);
    script = str->ptr;

    beg = script;
    const char seq7[] = "var l=n.entries.filter(Boolean);";
    str = Strnew();
    while ((p = strstr(beg, seq7))) {
	Strcat_charp_n(str, beg, p - beg + sizeof(seq7) - 1);
	Strcat_charp(str, "console.log(\"TEST4\" + n.type);");
        beg = p + sizeof(seq7) - 1;
    }
    Strcat_charp(str, beg);
    script = str->ptr;

    beg = script;
    const char seq8[] = "void 0};const Fe=function(e){";
    str = Strnew();
    while ((p = strstr(beg, seq8))) {
	Strcat_charp_n(str, beg, p - beg + sizeof(seq8) - 1);
	Strcat_charp(str, "console.log(\"TEST5\");");
        beg = p + sizeof(seq8) - 1;
    }
    Strcat_charp(str, beg);
    script = str->ptr;

    beg = script;
    const char seq9[] = "t.renderDOM=function(e,t,r){";
    str = Strnew();
    while ((p = strstr(beg, seq9))) {
	Strcat_charp_n(str, beg, p - beg + sizeof(seq9) - 1);
	Strcat_charp(str, "console.log(\"TEST6\");");
        beg = p + sizeof(seq9) - 1;
    }
    Strcat_charp(str, beg);
    script = str->ptr;
#elif 0
    /* frikaetter.com (0xde0b6b3a7640080.toFixed(0) => invalid number literal. */
    char *beg = script;
    char *p;
    const char seq[] = "0xde0b6b3a7640080";
    Str str = Strnew();
    while ((p = strstr(beg, seq))) {
	Strcat_charp_n(str, beg, p - beg + sizeof(seq) - 1);
	Strcat_charp(str, "==0xde0b6b3a7640080||1000000000000000128.");
        beg = p + sizeof(seq) - 1;
    }
    Strcat_charp(str, beg);
    script = str->ptr;
#elif 0
    /* www.pref.hiroshima.lg.jp */
    char *beg = script;
    char *p;
    const char seq[] = "decodeURIComponent(t[1]).replace(/\\+/g,\" \"));";
    Str str = Strnew();
    while ((p = strstr(beg, seq))) {
	Strcat_charp_n(str, beg, p - beg + sizeof(seq) - 1);
	Strcat_charp(str, "if (!r.pids) { r.pids = \"\"; }");
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
    /* for acid3 tests */
    char *beg = script;
    char *p;
    const char seq[] = "setTimeout(update, delay);";
    Str str = Strnew();
    while ((p = strstr(beg, seq))) {
	Strcat_charp_n(str, beg, p - beg + sizeof(seq) - 1);
	Strcat_charp(str, "console.log(log);/*dump_element(document, \"\");*/");
        beg = p + sizeof(seq) - 1;
    }
    Strcat_charp(str, beg);
    script = str->ptr;

#if 0
    beg = script;
    const char seq2[] = "doc.close();";
    str = Strnew();
    while ((p = strstr(beg, seq2))) {
	Strcat_charp_n(str, beg, p - beg + sizeof(seq2) - 1);
	Strcat_charp(str, "dump_tree(doc, \"\");");
        beg = p + sizeof(seq2) - 1;
    }
    Strcat_charp(str, beg);
    script = str->ptr;
#endif
#endif

    if (strncmp(script, "func_id:", 8) == 0) {
	int idx = atoi(script + 8);
	CtxState *ctxstate = JS_GetContextOpaque(ctx);
	if (ctxstate->nfuncs >= idx + 1) {
#ifdef SCRIPT_DEBUG
	    const char *str = JS_ToCString(ctx, ctxstate->funcs[idx]);
	    script = Sprintf("%s: %s", script, str)->ptr;
	    JS_FreeCString(ctx, str);
#endif
	    return backtrace(ctx, script, JS_Call(ctx, ctxstate->funcs[idx],
							 JS_UNDEFINED, 0, NULL));
	}
    }

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

#ifdef SCRIPT_DEBUG
	    {
		const char *str = JS_ToCString(ctx, ctxstate->funcs[idx]);
		script = Sprintf("%s: %s", script, str)->ptr;
		JS_FreeCString(ctx, str);
	    }
#endif

	    ret = backtrace(ctx, script, JS_Call(ctx, ctxstate->funcs[idx], jsThis, 0, NULL));
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

/* Don't JS_Free() the return value. */
JSValue
js_get_document(JSContext *ctx)
{
    CtxState *ctxstate = JS_GetContextOpaque(ctx);

    if (JS_IsUndefined(ctxstate->document)) {
	JSValue document = JS_Eval(ctx, "document;", 9, "<input>", EVAL_FLAG);

	if (JS_IsException(document)) {
	    return JS_UNDEFINED;
	}
	ctxstate->document = document;
    }

    return ctxstate->document;
}
#endif /* USE_JAVASCRIPT */

#ifdef USE_LIBXML2

#ifdef LIBXML_TREE_ENABLED
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
		return backtrace(ctx, script,
				 JS_Eval(ctx, script, strlen(script), "<input>", EVAL_FLAG));
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
		return backtrace(ctx, script,
				 JS_Eval(ctx, script, strlen(script), "<input>", EVAL_FLAG));
	    }
	}
    }
#endif

    return JS_NULL;
}

static void
create_dtd(JSContext *ctx, xmlDtd *dtd, JSValue doc)
{
    JSValue args[2];
    char *script = Sprintf("this.implementation.createDocumentType(%s, %s, %s);",
			   dtd->name ? Sprintf("\"%s\"", dtd->name)->ptr : "null",
			   dtd->ExternalID ? Sprintf("\"%s\"", dtd->ExternalID)->ptr : "null",
			   dtd->SystemID ? Sprintf("\"%s\"", dtd->SystemID)->ptr : "null")->ptr;
    args[0] = backtrace(ctx, script, JS_EvalThis(ctx, doc, script, strlen(script),
						 "<input>", EVAL_FLAG));
    args[1] = JS_EvalThis(ctx, doc, "this.firstChild;", 16, "<input>", EVAL_FLAG);
    JS_FreeValue(ctx, node_insert_before(ctx, doc, 2, args));
}

static void
set_attribute(JSContext *ctx, JSValue jsnode, xmlNode *node)
{
    xmlAttr *attr;

    for (attr = node->properties; attr != NULL; attr = attr->next) {
	xmlNode *n = attr->children;
	JSValue args[2];
	char *name;

	if (n && n->type == XML_TEXT_NODE) {
	    args[1] = JS_NewString(ctx, (char*)n->content);
	    name = to_lower((char*)attr->name);
#ifdef DOM_DEBUG
	    fprintf(fp, "attr %s=%s ", name, n->content);
#endif
	} else {
	    name = (char*)attr->name;
	    args[1] = JS_NULL;
#ifdef DOM_DEBUG
	    fprintf(fp, "attr %s", name);
#endif
	}
	args[0] = JS_NewString(ctx, name);
	element_set_attribute(ctx, jsnode, 2, args);
	JS_FreeValue(ctx, args[0]);
	JS_FreeValue(ctx, args[1]);
    }
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
	    } else if (strcasecmp((char*)node->name, "IMG") == 0) {
		jsnode = html_image_element_new(ctx, jsparent, 0, NULL);
	    } else if (strcasecmp((char*)node->name, "CANVAS") == 0) {
		jsnode = html_canvas_element_new(ctx, jsparent, 0, NULL);
	    } else if (strcasecmp((char*)node->name, "FORM") == 0) {
		jsnode = html_form_element_new(ctx, jsparent, 0, NULL);
	    } else if (strcasecmp((char*)node->name, "SELECT") == 0) {
		jsnode = html_select_element_new(ctx, jsparent, 0, NULL);
	    } else if (strcasecmp((char*)node->name, "OBJECT") == 0) {
		jsnode = html_object_element_new(ctx, jsparent, 0, NULL);
	    } else if (strcasecmp((char*)node->name, "IFRAME") == 0) {
		jsnode = html_iframe_element_new(ctx, jsparent, 0, NULL);
	    } else if (strcasecmp((char*)node->name, "SVG") == 0) {
		jsnode = svg_element_new(ctx, jsparent, 0, NULL);
	    } else {
		JSValue arg = JS_NewString(ctx, to_upper((char*)node->name));
		jsnode = html_element_new(ctx, jsparent, 1, &arg);
		JS_FreeValue(ctx, arg);
	    }

	    set_attribute(ctx, jsnode, node);

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
	    fprintf(fp, "skip element %d\n", node->type);
#endif
	    continue;
	}

	JS_FreeValue(ctx, node_append_child(ctx, jsparent, 1, &jsnode));

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
    xmlDtd *dtd;

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
			     HTML_PARSE_RECOVER | HTML_PARSE_NOERROR |
			     HTML_PARSE_NOWARNING | HTML_PARSE_NODEFDTD);
    } else {
	/* parse the file and get the DOM */
	doc = htmlReadFile(filename, charset,
			   HTML_PARSE_RECOVER | HTML_PARSE_NOERROR |
			   HTML_PARSE_NOWARNING | HTML_PARSE_NODEFDTD);
    }

    if (doc == NULL) {
	goto error;
    }

    dtd = xmlGetIntSubset(doc);
    if (dtd) {
	create_dtd(ctx, dtd, js_get_document(ctx));
    }

    node = xmlDocGetRootElement(doc);
    if (node) {
	JSValue jsnode = JS_GetPropertyStr(ctx, js_get_document(ctx), "documentElement");

	set_attribute(ctx, jsnode, node);
	JS_FreeValue(ctx, jsnode);

	for (node = node->children; node; node = node->next) {
	    xmlNode *next_tmp = NULL;

	    if (strcasecmp((char*)node->name, "head") == 0) {
		jsnode = JS_GetPropertyStr(ctx, js_get_document(ctx), "head");
	    } else if (strcasecmp((char*)node->name, "body") == 0) {
		jsnode = JS_GetPropertyStr(ctx, js_get_document(ctx), "body");
	    } else {
#ifdef DOM_DEBUG
		FILE *fp = fopen("domlog.txt", "a");
		fprintf(fp, "(orphan jsnode %s)\n", node->name);
		fclose(fp);
#endif
		jsnode = JS_GetPropertyStr(ctx, js_get_document(ctx), "documentJsnode");
		next_tmp = node->next;
		node->next = NULL;
	    }

	    set_attribute(ctx, jsnode, node);

	    if (next_tmp) {
		create_tree(ctx, node, jsnode, 0);
		node->next = next_tmp;
	    } else {
		create_tree(ctx, node->children, jsnode, 0);
	    }
	}
    }

    xmlFreeDoc(doc);
    xmlCleanupParser();

#if 0
    js_eval(ctx, "dump_tree(document, \"\");");
#endif

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

#else

int
js_create_dom_tree(JSContext *ctx, char *filename, const char *charset) { return 0; }

#endif /* LIBXML_TREE_ENABLED */

#endif

Str
escape_value(Str src)
{
    char *p1 = src->ptr;
    Str dst;
    char *p2;

    while (*p1 != '\r' && *p1 != '\n' && *p1 != '\"' && *p1 != '\'') {
	if (*p1 == '\0') {
	    return src;
	}
	p1++;
    }

    dst = Strnew();
    p2 = src->ptr;

    while (1) {
	char *str;

	if (*p1 == '\0') {
	    break;
	} else if (*p1 == '\r') {
	    str = "\\r";
	} else if (*p1 == '\n') {
	    str = "\\n";
	} else if (*p1 == '\"') {
	    str = "\\\x22";
	} else if (*p1 == '\'') {
	    str = "\\'";
	} else {
	    p1++;
	    continue;
	}

	Strcat_charp_n(dst, p2, p1 - p2);
	Strcat_charp(dst, str);
	p2 = ++p1;
    }

    Strcat_charp(dst, p2);

    return dst;
}
