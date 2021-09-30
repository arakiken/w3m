#include "fm.h"
#ifdef USE_JAVASCRIPT
#include "js_html.h"
#include <sys/utsname.h>

#define NUM_TAGNAMES 2
#define TN_FORM 0
#define TN_IMG 1

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
static JSClassID ElementClassID;
static JSClassID SVGElementClassID;

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

#ifdef SCRIPT_DEBUG
static JSValue
backtrace(JSContext *ctx, char *script, JSValue eval_ret)
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
    const char *type;

    if (argc < 2) {
	return JS_EXCEPTION;
    }

#if 0
    FILE *fp = fopen("w3mlog.txt", "a");
    fprintf(fp, "%s\n", JS_ToCString(ctx, argv[1]));
    fclose(fp);
#endif

    type = JS_ToCString(ctx, argv[0]);
    if (strcmp(type, "load") == 0 || strcmp(type, "DOMContentLoaded") == 0) {
	JSValue gl = JS_GetGlobalObject(ctx);
	JSValue listeners = JS_GetPropertyStr(ctx, gl, "w3m_eventListeners");
	JSValue prop = JS_GetPropertyStr(ctx, listeners, "push");

	JSValue event = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, event, "type", JS_DupValue(ctx, argv[0]));
	JS_SetPropertyStr(ctx, event, "currentTarget", JS_DupValue(ctx, jsThis));
	JS_SetPropertyStr(ctx, event, "callback", JS_DupValue(ctx, argv[1]));

	JS_FreeValue(ctx, JS_Call(ctx, prop, listeners, 1, &event));

	JS_FreeValue(ctx, event);
	JS_FreeValue(ctx, prop);
	JS_FreeValue(ctx, listeners);
	JS_FreeValue(ctx, gl);
    } else if (strcmp(type, "click") == 0) {
	JS_SetPropertyStr(ctx, jsThis, "onclick", JS_DupValue(ctx, argv[1]));
    }

    JS_FreeCString(ctx, type);

    return JS_UNDEFINED;
}

static JSValue
remove_event_listener(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    return JS_UNDEFINED;
}

static JSValue
dispatch_event(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    return JS_UNDEFINED;
}

static JSValue
window_open(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    WindowState *state = JS_GetOpaque(jsThis, WindowClassID);

    if (argc >= 1) {
	const char *str[2];
	size_t len[2];

	str[0] = JS_ToCStringLen(ctx, &len[0], argv[0]);

	if (argc >= 2) {
	    str[1] = JS_ToCStringLen(ctx, &len[1], argv[1]);
	} else {
	    str[1] = NULL;
	}

	if (str[0] != NULL) {
	    OpenWindow *w = (OpenWindow *)GC_MALLOC(sizeof(OpenWindow));
	    w->url = allocStr(str[0], len[0]);
	    if (str[1] != NULL) {
		w->name = allocStr(str[1], len[1]);
		JS_FreeCString(ctx, str[1]);
	    } else {
		w->name = "";
	    }
	    pushValue(state->win, (void *)w);
	    JS_FreeCString(ctx, str[0]);
	}

	return JS_DupValue(ctx, jsThis); /* XXX segfault without this by returining jsThis. */
    }

    return JS_EXCEPTION;
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
    JSValue style;

    if (argc < 1) {
	return JS_EXCEPTION;
    }

    style = JS_GetPropertyStr(ctx, argv[0], "style");
    if (!JS_IsUndefined(style)) {
	return JS_DupValue(ctx, style);
    } else {
	return JS_UNDEFINED;
    }
}

static JSValue
window_scroll(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    return JS_UNDEFINED;
}

static JSValue
window_get_selection(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    /* XXX getSelection() should return Selection object. */
    return JS_NewString(ctx, "");
}

static JSValue
window_match_media(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue result = JS_NewObject(ctx); /* XXX MediaQueryList */

    JS_SetPropertyStr(ctx, result, "matches", JS_FALSE);

    return result;
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
location_protocol_get(JSContext *ctx, JSValueConst jsThis)
{
    LocationState *state = JS_GetOpaque(jsThis, LocationClassID);
    Str s;

    s = Strnew();
    switch (state->pu.scheme) {
    case SCM_HTTP:
	Strcat_charp(s, "http:");
	break;
    case SCM_GOPHER:
	Strcat_charp(s, "gopher:");
	break;
    case SCM_FTP:
	Strcat_charp(s, "ftp:");
	break;
    case SCM_LOCAL:
	Strcat_charp(s, "file:");
	break;
    case SCM_NNTP:
	Strcat_charp(s, "nntp:");
	break;
    case SCM_NEWS:
	Strcat_charp(s, "news:");
	break;
#ifndef USE_W3MMAILER
    case SCM_MAILTO:
	Strcat_charp(s, "mailto:");
	break;
#endif
#ifdef USE_SSL
    case SCM_HTTPS:
	Strcat_charp(s, "https:");
	break;
#endif
    case SCM_JAVASCRIPT:
	Strcat_charp(s, "javascript:");
	break;
    default:
	break;
    }

    return JS_NewStringLen(ctx, s->ptr, s->length);
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

    if ((p = js_get_cstr(ctx, val)) == NULL) {
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

    if ((str = js_get_cstr(ctx, val)) == NULL) {
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

    if ((str = js_get_cstr(ctx, val)) == NULL) {
	return JS_EXCEPTION;
    }

    state->pu.file = str;
    state->url = parsedURL2Str(&state->pu);
    state->refresh |= JS_LOC_REFRESH;

    return JS_UNDEFINED;
}

static JSValue
location_search_get(JSContext *ctx, JSValueConst jsThis)
{
    LocationState *state = JS_GetOpaque(jsThis, LocationClassID);
    Str s;

    s = Strnew();
    if (state->pu.query) {
	Strcat_charp(s, "?");
	Strcat_charp_n(s, state->pu.query, strlen(state->pu.query));
    }
    return JS_NewStringLen(ctx, s->ptr, s->length);
}

static JSValue
location_search_set(JSContext *ctx, JSValueConst jsThis, JSValueConst val)
{
    LocationState *state = JS_GetOpaque(jsThis, LocationClassID);
    char *str;

    if ((str = js_get_cstr(ctx, val)) == NULL) {
	return JS_EXCEPTION;
    }

    state->pu.query = str;
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

    if ((str = js_get_cstr(ctx, val)) == NULL) {
	return JS_EXCEPTION;
    }

    state->pu.label = str;
    state->url = parsedURL2Str(&state->pu);
    state->refresh |= JS_LOC_HASH;

    return JS_UNDEFINED;
}

static const JSCFunctionListEntry LocationFuncs[] = {
    JS_CFUNC_DEF("replace", 1, location_replace),
    JS_CFUNC_DEF("reload", 1, location_reload),
    JS_CGETSET_DEF("href", location_href_get, location_href_set),
    JS_CGETSET_DEF("protocol", location_protocol_get, location_protocol_set),
    JS_CGETSET_DEF("host", location_host_get, location_host_set),
    JS_CGETSET_DEF("hostname", location_hostname_get, location_hostname_set),
    JS_CGETSET_DEF("port", location_port_get, location_port_set),
    JS_CGETSET_DEF("pathname", location_pathname_get, location_pathname_set),
    JS_CGETSET_DEF("search", location_search_get, location_search_set),
    JS_CGETSET_DEF("hash", location_hash_get, location_hash_set),
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


    if (strcasecmp(tagname, "form") == 0) {
	JS_SetPropertyStr(ctx, obj, "elements", JS_DupValue(ctx, children));
    } else if (strcasecmp(tagname, "select") == 0) {
	JS_SetPropertyStr(ctx, obj, "options", JS_DupValue(ctx, children));
    }
}

static void
set_element_property(JSContext *ctx, JSValue obj, JSValue tagname)
{
    const char *str = JS_ToCString(ctx, tagname);
    JSValue style;

    init_children(ctx, obj, str);

    /* Node */
    JS_SetPropertyStr(ctx, obj, "parentNode", JS_NULL);
    JS_SetPropertyStr(ctx, obj, "nextSibling", JS_NULL);
    JS_SetPropertyStr(ctx, obj, "previousSibling", JS_NULL);
    if (strcasecmp(str, "#text") == 0) {
	JS_SetPropertyStr(ctx, obj, "nodeType", JS_NewInt32(ctx, 3)); /* TEXT_NODE */
    } else if (strcasecmp(str, "#comment") == 0) {
	JS_SetPropertyStr(ctx, obj, "nodeType", JS_NewInt32(ctx, 8)); /* COMMENT_NODE */
    } else {
	JS_SetPropertyStr(ctx, obj, "nodeType", JS_NewInt32(ctx, 1)); /* ELEMENT_NODE */
    }
    JS_SetPropertyStr(ctx, obj, "nodeValue", JS_NULL);
    JS_SetPropertyStr(ctx, obj, "nodeName", JS_DupValue(ctx, tagname));

    /* Element */
    JS_SetPropertyStr(ctx, obj, "tagName", JS_DupValue(ctx, tagname));
    JS_SetPropertyStr(ctx, obj, "parentElement", JS_NULL);

    /* HTMLElement */
    style = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, style, "fontSize",
		      JS_NewString(ctx, Sprintf("%dpx", term_ppl)->ptr));
    JS_SetPropertyStr(ctx, style, "zoom", JS_NewString(ctx, "normal"));
    JS_SetPropertyStr(ctx, obj, "style", style);

    JS_SetPropertyStr(ctx, obj, "offsetWidth", JS_NewInt32(ctx, term_ppc));
    JS_SetPropertyStr(ctx, obj, "offsetHeight", JS_NewInt32(ctx, term_ppl));
    JS_SetPropertyStr(ctx, obj, "offsetLeft", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, obj, "offsetTop", JS_NewInt32(ctx, 0));

    /* XXX HTMLSelectElement */
    JS_SetPropertyStr(ctx, obj, "disabled", JS_FALSE);

    JS_FreeCString(ctx, str);
}

static JSValue
html_form_element_new(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue obj;
    JSValue *tagnames;
    HTMLFormElementState *state;

    tagnames = JS_GetContextOpaque(ctx);
    if (JS_IsUndefined(tagnames[TN_FORM])) {
	tagnames[TN_FORM] = JS_NewString(ctx, "form");
    }

    obj = JS_NewObjectClass(ctx, HTMLFormElementClassID);
    set_element_property(ctx, obj, tagnames[TN_FORM]);

    state = (HTMLFormElementState *)GC_MALLOC_UNCOLLECTABLE(sizeof(HTMLFormElementState));
    memset(state, 0, sizeof(*state));

    JS_SetOpaque(obj, state);

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
    JSValue *tagnames;

    tagnames = JS_GetContextOpaque(ctx);
    if (JS_IsUndefined(tagnames[TN_IMG])) {
	tagnames[TN_FORM] = JS_NewString(ctx, "img");
    }

    obj = JS_NewObjectClass(ctx, HTMLImageElementClassID);
    set_element_property(ctx, obj, tagnames[TN_IMG]);

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

    children = JS_GetPropertyStr(ctx, jsThis, "children");
    prop = JS_GetPropertyStr(ctx, children, "indexOf");
    argv2[0] = JS_Call(ctx, prop, children, 1, argv);
    JS_FreeValue(ctx, prop);
    argv2[1] = JS_NewInt32(ctx, 1);

    prop = JS_GetPropertyStr(ctx, children, "splice");
    JS_FreeValue(ctx, JS_Call(ctx, prop, children, 2, argv2));
    JS_FreeValue(ctx, prop);
    JS_FreeValue(ctx, argv2[0]);
    JS_FreeValue(ctx, argv2[1]);

    JS_FreeValue(ctx, children);

    return JS_DupValue(ctx, argv[0]); /* XXX segfault without this by returining argv[0]. */
}

static JSValue
element_replace_child(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    if (argc < 2) {
	return JS_EXCEPTION;
    }

    /* XXX Not implemented. */

    return JS_DupValue(ctx, argv[1]); /* XXX segfault without this by returining argv[1]. */
}

static JSValue
element_set_attribute(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    if (argc < 2) {
	return JS_EXCEPTION;
    }

    if (JS_IsString(argv[0])) {
	const char *key = JS_ToCString(ctx, argv[0]);
	JS_SetPropertyStr(ctx, jsThis, key, JS_DupValue(ctx, argv[1]));
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
	JSValue prop = JS_GetPropertyStr(ctx, jsThis, key);
	JS_FreeCString(ctx, key);

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
    if (argc < 2) {
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
element_has_child_nodes(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    char script[] = "if (this.childNodes.length == 0) { true; } else { false; }";
    return backtrace(ctx, script,
		     JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1, "<input>", EVAL_FLAG));
}

static JSValue
element_first_child_get(JSContext *ctx, JSValueConst jsThis)
{
    char script[] =
	"if (this.children.length > 0) {"
	"  this.children[0];"
	"} else {"
	"  null;"
	"}";

    return backtrace(ctx, script,
		     JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1, "<input>", EVAL_FLAG));
}

static JSValue
element_last_child_get(JSContext *ctx, JSValueConst jsThis)
{
    char script[] =
	"if (this.children.length > 0) {"
	"  this.children[this.children.length - 1];"
	"} else {"
	"  null;"
	"}";

    return backtrace(ctx, script,
		     JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1, "<input>", EVAL_FLAG));
}

static JSValue
element_owner_document_get(JSContext *ctx, JSValueConst jsThis)
{
    return JS_Eval(ctx, "document;", 9, "<input>", EVAL_FLAG);
}

static JSValue
element_content_window_get(JSContext *ctx, JSValueConst jsThis)
{
    return JS_Eval(ctx, "window;", 7, "<input>", EVAL_FLAG);
}

static JSValue
element_matches(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    if (argc < 1) {
	return JS_EXCEPTION;
    }

    /* XXX argv[0] is CSS selector. */

    return JS_FALSE;
}

static JSValue
element_attributes_get(JSContext *ctx, JSValueConst jsThis)
{
    char script[] = "w3m_elementAttributes(this);";
    return backtrace(ctx, script,
		     JS_EvalThis(ctx, jsThis, script, sizeof(script) - 1, "<input>", EVAL_FLAG));
}

static JSValue
element_compare_document_position(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    return JS_NewInt32(ctx, 4); /* DOCUMENT_POSITION_FOLLOWING */
}

static JSValue
element_closest(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    /* XXX */
    return JS_GetPropertyStr(ctx, jsThis, "parentElement");
}

static JSValue
element_offset_parent_get(JSContext *ctx, JSValueConst jsThis)
{
    /* XXX */
    return JS_GetPropertyStr(ctx, jsThis, "parentElement");
}

static JSValue
element_do_scroll(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
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
    return JS_DupValue(ctx, jsThis);
}

static JSValue
element_text_content_get(JSContext *ctx, JSValueConst jsThis)
{
    char script[] =
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
	xmlDoc *doc = htmlReadMemory(str, strlen(str), "utf-8", "",
				     HTML_PARSE_RECOVER | HTML_PARSE_NOERROR |
				     HTML_PARSE_NOWARNING | HTML_PARSE_NOIMPLIED);
	create_tree(ctx, xmlDocGetRootElement(doc), JS_DupValue(ctx, jsThis), 1);
	xmlFreeDoc(doc);
	xmlCleanupParser();
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
element_insert_adjacent_html(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    if (argc < 2) {
	return JS_EXCEPTION;
    }

    JSValue nodename = JS_NewStringLen(ctx, "#text", 5);
    JSValue element = element_new(ctx, jsThis, 1, &nodename);

    JS_SetPropertyStr(ctx, element, "nodeValue", JS_DupValue(ctx, argv[1]));
    JS_FreeValue(ctx, element_append_child(ctx, jsThis, 1, &element));

    JS_FreeValue(ctx, nodename);
    JS_FreeValue(ctx, element);

    return JS_UNDEFINED;
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
    JS_CFUNC_DEF("compareDocumentPosition", 1, element_compare_document_position),
    JS_CFUNC_DEF("cloneNode", 1, element_clone_node),
    JS_CGETSET_DEF("textContent", element_text_content_get, element_text_content_set),

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
    JS_CFUNC_DEF("getElementsByTagName", 1, element_get_elements_by_tag_name),
    JS_CFUNC_DEF("getElementsByTagNameNS", 1, element_get_elements_by_tag_name),
    JS_CFUNC_DEF("getElementsByClassName", 1, element_get_elements_by_class_name),
    JS_CGETSET_DEF("innerHTML", element_text_content_get, element_text_content_set),
    JS_CFUNC_DEF("insertAdjacentHTML", 1, element_insert_adjacent_html),

    /* HTMLElement */
    JS_CFUNC_DEF("doScroll", 1, element_do_scroll), /* XXX Obsolete API */
    JS_CGETSET_DEF("offsetParent", element_offset_parent_get, NULL),
    JS_CGETSET_DEF("innerText", element_text_content_get, element_text_content_set),

    /* XXX HTMLFrameElement only */
    JS_CGETSET_DEF("contentDocument", element_owner_document_get, NULL),
    JS_CGETSET_DEF("contentWindow", element_content_window_get, NULL),


    /* HTMLFormElement */
    JS_CFUNC_DEF("submit", 1, html_form_element_submit),
    JS_CFUNC_DEF("reset", 1, html_form_element_reset),
};

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
    return JS_UNDEFINED;
}

static JSValue
history_replace_state(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry HistoryFuncs[] = {
    JS_CFUNC_DEF("back", 1, history_back),
    JS_CFUNC_DEF("forward", 1, history_forward),
    JS_CFUNC_DEF("go", 1, history_go),
    JS_CFUNC_DEF("pushState", 1, history_push_state),
    JS_CFUNC_DEF("replaceState", 1, history_replace_state),
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
    JSValue obj;
    NavigatorState *state;

    obj = JS_NewObjectClass(ctx, NavigatorClassID);

    state = (NavigatorState *)GC_MALLOC_UNCOLLECTABLE(sizeof(NavigatorState));
    state->appcodename = NULL;
    state->appname = NULL;
    state->appversion = NULL;
    state->useragent = NULL;

    JS_SetOpaque(obj, state);

    return obj;
}

static JSValue
navigator_appcodename_get(JSContext *ctx, JSValueConst jsThis)
{
    NavigatorState *state = JS_GetOpaque(jsThis, NavigatorClassID);

    if (! state->appcodename)
	state->appcodename = Strnew_charp("w3m");

    return JS_NewStringLen(ctx, state->appcodename->ptr, state->appcodename->length);
}

static JSValue
navigator_appname_get(JSContext *ctx, JSValueConst jsThis)
{
    NavigatorState *state = JS_GetOpaque(jsThis, NavigatorClassID);

    if (! state->appname)
	state->appname = Strnew_charp("w3m");

    return JS_NewStringLen(ctx, state->appname->ptr, state->appname->length);
}

static JSValue
navigator_appversion_get(JSContext *ctx, JSValueConst jsThis)
{
    NavigatorState *state = JS_GetOpaque(jsThis, NavigatorClassID);
#if LANG == JA
    const char *lang = "ja-JP";
#else
    const char *lang = "en-US";
#endif
    char *platform;
    struct utsname unamebuf;
    char *p;
    int n;

    if (! state->appversion) {
	if (uname(&unamebuf) == -1) {
	    platform = "Unknown";
	} else {
	    platform = unamebuf.sysname;
	}
	p = strchr(w3m_version, '/');
	if (p != NULL) {
	    p++;
	    n = strspn(p, "0123456789.");
	    if (n > 0) {
		Str rnum = Strnew();
		Strcopy_charp_n(rnum, p, n);
		state->appversion = Sprintf("%s (%s; %s)", rnum->ptr, platform, lang);
		Strfree(rnum);
	    }
	}
    }

    return JS_NewStringLen(ctx, state->appversion->ptr, state->appversion->length);
}

static JSValue
navigator_useragent_get(JSContext *ctx, JSValueConst jsThis)
{
    NavigatorState *state = JS_GetOpaque(jsThis, NavigatorClassID);

    if (! state->useragent) {
	if (UserAgent == NULL || *UserAgent == '\0')
	    state->useragent = Strnew_charp(w3m_version);
	else
	    state->useragent = Strnew_charp(UserAgent);
    }

    return JS_NewStringLen(ctx, state->useragent->ptr, state->useragent->length);
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

static const JSCFunctionListEntry NavigatorFuncs[] = {
    JS_CGETSET_DEF("appCodeName", navigator_appcodename_get, NULL),
    JS_CGETSET_DEF("appName", navigator_appname_get, NULL),
    JS_CGETSET_DEF("appVersion", navigator_appversion_get, NULL),
    JS_CGETSET_DEF("userAgent", navigator_useragent_get, NULL),
    JS_CGETSET_DEF("cookieEnabled", navigator_cookieenabled_get, NULL),
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
    JS_CFUNC_DEF("compareDocumentPosition", 1, element_compare_document_position),
    JS_CFUNC_DEF("cloneNode", 1, element_clone_node),

    /* EventTarget */
    JS_CFUNC_DEF("addEventListener", 1, add_event_listener),
    JS_CFUNC_DEF("removeEventListener", 1, remove_event_listener),
    JS_CFUNC_DEF("dispatchEvent", 1, dispatch_event),

    /* Document */
    JS_CFUNC_DEF("open", 1, document_open),
    JS_CFUNC_DEF("close", 1, document_close),
    JS_CFUNC_DEF("write", 1, document_write),
    JS_CFUNC_DEF("writeln", 1, document_writeln),
    JS_CGETSET_DEF("location", document_location_get, document_location_set),
    JS_CGETSET_DEF("cookie", document_cookie_get, document_cookie_set),
    JS_CGETSET_DEF("referrer", document_referrer_get, NULL)
};

static void
log_to_file(JSContext *ctx, const char *head, int argc, JSValueConst *argv)
{
    FILE *fp;

    if ((fp = fopen("w3mlog.txt", "a")) != NULL) {
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

static struct {
    char *name;
    JSCFunction *func;
} ConsoleFuncs[] = {
    { "error", console_error } ,
    { "log", console_log },
    { "warn", console_warn },
    { "info", console_info },
};

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
js_html_init(void)
{
    JSContext *ctx;
    JSValue gl;
    JSValue *tagnames;
    int i;
    WindowState *state;
    JSValue console;
    char script[] =
	"class NodeList extends Array {};"
	"class HTMLCollection extends Array {};"
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
	"}";

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

    JS_SetPropertyStr(ctx, gl, "w3m_eventListeners", JS_NewArray(ctx));

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

    JS_FreeValue(ctx, gl);

    tagnames = (JSValue*)GC_MALLOC_UNCOLLECTABLE(sizeof(JSValue) * NUM_TAGNAMES);
    for (i = 0; i < NUM_TAGNAMES; i++) {
	tagnames[i] = JS_UNDEFINED;
    }
    JS_SetContextOpaque(ctx, tagnames);

    return ctx;
}

void
js_html_final(JSContext *ctx) {
    JSValue gl = JS_GetGlobalObject(ctx);
    GC_FREE(JS_GetOpaque(gl, WindowClassID));
    JS_FreeValue(ctx, gl);

    GC_FREE(JS_GetContextOpaque(ctx));
    JS_FreeContext(ctx);
}

void
js_eval(JSContext *ctx, char *script) {
    JSValue ret = js_eval2(ctx, script);
    JS_FreeValue(ctx, ret);
}

JSValue
js_eval2(JSContext *ctx, char *script) {
    return backtrace(ctx, script,
		     JS_Eval(ctx, script, strlen(script), "<input>", EVAL_FLAG));
}

char *
js_get_cstr(JSContext *ctx, JSValue value)
{
    const char *str;
    size_t len;
    char *new_str;

    if (!JS_IsString(value) || (str = JS_ToCStringLen(ctx, &len, value)) == NULL) {
	JS_FreeValue(ctx, value);

	return NULL;
    }

    new_str = allocStr(str, len);

    JS_FreeCString(ctx, str);
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

Str
js_get_function(JSContext *ctx, char *script) {
    JSValue value = js_eval2(ctx, script);
    const char *cstr;
    const char *p;
    size_t len;
    Str str;

    if (!JS_IsFunction(ctx, value) || (cstr = JS_ToCStringLen(ctx, &len, value)) == NULL) {
	JS_FreeValue(ctx, value);

	return NULL;
    }

    p = cstr;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "function", 8) == 0) {
	p += 8;
	while (*p == ' ' || *p == '\t') p++;
	if (strncmp(p, "()", 2) == 0) {
	    str = Strnew_charp(p + 2);
	    goto end;
	}
    }

    str = Strnew_charp_n(cstr, len);

 end:
    JS_FreeCString(ctx, cstr);
    JS_FreeValue(ctx, value);

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
#endif

#ifdef USE_LIBXML2

#ifdef LIBXML_TREE_ENABLED
static void
create_tree(JSContext *ctx, xmlNode *node, JSValue jsparent, int innerhtml)
{
#ifdef DOM_DEBUG
    FILE *fp = fopen("domlog.txt", "a");
    fprintf(fp, "------\n");
#endif

    for (; node; node = node->next) {
	JSValue jsnode;

	if (node->type == XML_ELEMENT_NODE &&
	    (innerhtml ||
	     (strcasecmp((char*)node->name, "form") != 0 &&
	      strcasecmp((char*)node->name, "select") != 0 &&
	      strcasecmp((char*)node->name, "input") != 0 &&
	      strcasecmp((char*)node->name, "textarea") != 0))) {
	    JSValue arg = JS_NewString(ctx, (char*)node->name);
	    xmlAttr *attr;

#ifdef DOM_DEBUG
	    fprintf(fp, "element %s ", node->name);
#endif

	    jsnode = html_element_new(ctx, jsparent, 1, &arg);
	    JS_FreeValue(ctx, arg);

	    for (attr = node->properties; attr != NULL; attr = attr->next) {
		xmlNode *n;
		JSValue value;

		n = attr->children;
		if (n && n->type == XML_TEXT_NODE) {
		    value = JS_NewString(ctx, (char*)n->content);
#ifdef DOM_DEBUG
		    fprintf(fp, "attr %s=%s ", attr->name, n->content);
#endif
		} else {
		    value = JS_NULL;
#ifdef DOM_DEBUG
		    fprintf(fp, "attr %s", attr->name);
#endif
		}
		JS_SetPropertyStr(ctx, jsnode, (char*)attr->name, value);
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
	    if (innerhtml) {
		JS_SetPropertyStr(ctx, jsnode, "isModified", JS_TRUE);
	    }
	} else {
#ifdef DOM_DEBUG
	    fprintf(fp, "element %d\n", node->type);
#endif
	    continue;
	}

	element_append_child(ctx, jsparent, 1, &jsnode);
	JS_FreeValue(ctx, jsnode);

#ifdef DOM_DEBUG
	fclose(fp);
#endif
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
create_dom_tree(JSContext *ctx, char *filename)
{
    URLFile uf;
    xmlDoc *doc;
    xmlNode *node;

    if (filename == NULL) {
	return 0;
    }

    LIBXML_TEST_VERSION;

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

	doc = htmlReadMemory(str->ptr, str->length, "utf-8", filename,
			     HTML_PARSE_RECOVER|HTML_PARSE_NOERROR|HTML_PARSE_NOWARNING);
    } else {
	/* parse the file and get the DOM */
	doc = htmlReadFile(filename, "utf-8",
			   HTML_PARSE_RECOVER|HTML_PARSE_NOERROR|HTML_PARSE_NOWARNING);
    }

    if (doc == NULL) {
        fprintf(stderr, "error: could not parse file %s\n", filename);
	return 0;
    }

    for (node = xmlDocGetRootElement(doc)->children; node; node = node->next) {
	if (strcmp((char*)node->name, "body") == 0) {
	    create_tree(ctx, node->children, js_eval2(ctx, "document.body;"), 0);
	    break;
	}
    }

    xmlFreeDoc(doc);
    xmlCleanupParser();

    return 0;
}
#endif /* LIBXML_TREE_ENABLED */

#else

int
create_dom_tree(JSContext *ctx, char *filename) { return 0; }

#endif
