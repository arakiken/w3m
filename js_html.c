#include "fm.h"
#ifdef USE_JAVASCRIPT
#include "js_html.h"
#include <sys/utsname.h>

JSClassID WindowClassID;
JSClassID LocationClassID;
JSClassID DocumentClassID;
JSClassID NavigatorClassID;
JSClassID HistoryClassID;
JSClassID AnchorClassID;
JSClassID FormItemClassID;
JSClassID FormClassID;
JSClassID ImageClassID;
JSClassID CookieClassID;

static void
window_final(JSRuntime *rt, JSValue val) {
    GC_FREE(JS_GetOpaque(val, WindowClassID));
}

static const JSClassDef WindowClass = {
    "Window", window_final, NULL /* gc_mark */, NULL /* call */, NULL
};

static JSValue
window_new(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue obj;
    WindowState *state;

    if (argc != 0) {
	return JS_EXCEPTION;
    }

    obj = JS_NewObjectClass(ctx, WindowClassID);

    state = (WindowState *)GC_MALLOC_UNCOLLECTABLE(sizeof(WindowState));
    state->win = newGeneralList();
    state->close = FALSE;

    JS_SetOpaque(obj, state);

    return obj;
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
	    jse_windowopen_t *w = (jse_windowopen_t *)GC_MALLOC(sizeof(jse_windowopen_t));
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

	return jsThis; /* XXX */
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

static const JSCFunctionListEntry WindowFuncs[] = {
    JS_CFUNC_DEF("open", 1, window_open),
    JS_CFUNC_DEF("close", 1, window_close),
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
	if ((state->url = js_get_str(ctx, argv[0])) == NULL) {
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

    if (argc != 1 || (str = js_get_str(ctx, argv[0])) == NULL) {
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

    if (argc != 0)
	return JS_EXCEPTION;

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

    if ((str = js_get_str(ctx, val)) == NULL) {
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

    if ((s = js_get_str(ctx, val)) == NULL) {
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

static const JSClassDef CookieClass = {
    "Cookie", NULL /* finalizer */, NULL /* gc_mark */, NULL /* call */, NULL
};

static JSValue
cookie_new(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    if (argc != 0) {
	return JS_EXCEPTION;
    }

    return JS_NewObjectClass(ctx, CookieClassID);
}

static const JSClassDef ImageClass = {
    "Image", NULL /* finalizer */, NULL /* gc_mark */, NULL /* call */, NULL
};

static JSValue
image_new(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    if (argc != 0) {
	return JS_EXCEPTION;
    }

    return JS_NewObjectClass(ctx, ImageClassID);
}

static const JSClassDef FormItemClass = {
    "FormItem", NULL /* finalizer */, NULL /* gc_mark */, NULL /* call */, NULL
};

static JSValue
formitem_new(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    if (argc != 0) {
	return JS_EXCEPTION;
    }

    return JS_NewObjectClass(ctx, FormItemClassID);
}

static void
form_final(JSRuntime *rt, JSValue val) {
    GC_FREE(JS_GetOpaque(val, FormClassID));
}

static const JSClassDef FormClass = {
    "Form", form_final, NULL /* gc_mark */, NULL /* call */, NULL
};

static JSValue
form_new(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    JSValue obj;
    FormState *state;

    if (argc != 0) {
	return JS_EXCEPTION;
    }

    obj = JS_NewObjectClass(ctx, FormClassID);

    state = (FormState *)GC_MALLOC_UNCOLLECTABLE(sizeof(FormState));
    memset(state, 0, sizeof(*state));

    JS_SetOpaque(obj, state);

    return obj;
}

static JSValue
form_submit(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    FormState *state = JS_GetOpaque(jsThis, FormClassID);

    state->submit = 1;

    return JS_UNDEFINED;
}

static JSValue
form_reset(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    FormState *state = JS_GetOpaque(jsThis, FormClassID);

    /* reset() isn't implemented (see script.c) */
    state->reset = 1;

    return JS_UNDEFINED;
}

static const JSCFunctionListEntry FormFuncs[] = {
    JS_CFUNC_DEF("submit", 1, form_submit),
    JS_CFUNC_DEF("reset", 1, form_reset),
};

static const JSClassDef AnchorClass = {
    "Anchor", NULL /* finalizer */, NULL /* gc_mark */, NULL /* call */, NULL
};

static JSValue
anchor_new(JSContext *ctx, JSValueConst jsThis, int argc, JSValueConst *argv)
{
    if (argc != 0) {
	return JS_EXCEPTION;
    }

    return JS_NewObjectClass(ctx, AnchorClassID);
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

    if (argc != 0) {
	return JS_EXCEPTION;
    }

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

    if (argc != 1 || !JS_IsNumber(argv[0])) {
	return JS_EXCEPTION;
    }

    JS_ToInt32(ctx, &i, argv[0]);
    state->pos = i;

    return JS_UNDEFINED;
}

static const JSCFunctionListEntry HistoryFuncs[] = {
    JS_CFUNC_DEF("back", 1, history_back),
    JS_CFUNC_DEF("forward", 1, history_forward),
    JS_CFUNC_DEF("go", 1, history_go),
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

    if (argc != 0) {
	return JS_EXCEPTION;
    }

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

static const JSCFunctionListEntry NavigatorFuncs[] = {
    JS_CGETSET_DEF("appCodeName", navigator_appcodename_get, NULL),
    JS_CGETSET_DEF("appName", navigator_appname_get, NULL),
    JS_CGETSET_DEF("appVersion", navigator_appversion_get, NULL),
    JS_CGETSET_DEF("userAgent", navigator_useragent_get, NULL),
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

    if (argc != 0) {
	return JS_EXCEPTION;
    }

    obj = JS_NewObjectClass(ctx, DocumentClassID);

    state = (DocumentState *)GC_MALLOC_UNCOLLECTABLE(sizeof(DocumentState));
    state->write = NULL;

    JS_SetOpaque(obj, state);

    return obj;
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

static const JSCFunctionListEntry DocumentFuncs[] = {
    JS_CFUNC_DEF("write", 1, document_write),
    JS_CFUNC_DEF("writeln", 1, document_writeln),
};

#if 0
static int
io_stdout (void *context, unsigned char *buffer, unsigned int amount)
{
    FILE *fp;
    int	 i = 0;

    if ((fp = fopen("w3mjs.log", "a")) != NULL) {
	fprintf(fp, "STDOUT: ");
	i = fwrite (buffer, 1, amount, fp);
	fclose(fp);
    }
    return i;
}

static int
io_stderr (void *context, unsigned char *buffer, unsigned int amount)
{
    FILE *fp;
    int	 i = 0;

    if ((fp = fopen("w3mjs.log", "a")) != NULL) {
	fprintf(fp, "STDERR: ");
	i = fwrite (buffer, 1, amount, fp);
	fclose(fp);
	return i;
    }
    return i;
}
#endif

JSContext *
js_html_init(void)
{
    static JSRuntime *rt;
    JSContext *ctx;
    JSValue gl;
    JSValue proto;

    if (rt == NULL) {
	rt = JS_NewRuntime();
    }

    ctx = JS_NewContext(rt);
    gl = JS_GetGlobalObject(ctx);

    JS_NewClassID(&WindowClassID);
    JS_NewClass(rt, WindowClassID, &WindowClass);
    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, WindowFuncs,
			       sizeof(WindowFuncs) / sizeof(WindowFuncs[0]));
    JS_SetClassProto(ctx, WindowClassID, proto);
    JS_SetPropertyStr(ctx, gl, "Window",
		      JS_NewCFunction2(ctx, window_new, "Window", 1, JS_CFUNC_constructor, 0));

    JS_NewClassID(&LocationClassID);
    JS_NewClass(rt, LocationClassID, &LocationClass);
    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, LocationFuncs,
			       sizeof(LocationFuncs) / sizeof(LocationFuncs[0]));
    JS_SetClassProto(ctx, LocationClassID, proto);
    JS_SetPropertyStr(ctx, gl, "Location",
		      JS_NewCFunction2(ctx, location_new, "Location", 1, JS_CFUNC_constructor, 0));
    JS_SetPropertyStr(ctx, gl, "location", location_new(ctx, JS_NULL, 0, NULL));

    JS_NewClassID(&CookieClassID);
    JS_NewClass(rt, CookieClassID, &CookieClass);
    JS_SetPropertyStr(ctx, gl, "Cookie",
		      JS_NewCFunction2(ctx, cookie_new, "Cookie", 1, JS_CFUNC_constructor, 0));

    JS_NewClassID(&ImageClassID);
    JS_NewClass(rt, ImageClassID, &ImageClass);
    JS_SetPropertyStr(ctx, gl, "Image",
		      JS_NewCFunction2(ctx, image_new, "Image", 1, JS_CFUNC_constructor, 0));

    JS_NewClassID(&FormItemClassID);
    JS_NewClass(rt, FormItemClassID, &FormItemClass);
    JS_SetPropertyStr(ctx, gl, "FormItem",
		      JS_NewCFunction2(ctx, formitem_new, "FormItem", 1, JS_CFUNC_constructor, 0));

    JS_NewClassID(&FormClassID);
    JS_NewClass(rt, FormClassID, &FormClass);
    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, FormFuncs,
			       sizeof(FormFuncs) / sizeof(FormFuncs[0]));
    JS_SetClassProto(ctx, FormClassID, proto);
    JS_SetPropertyStr(ctx, gl, "Form",
		      JS_NewCFunction2(ctx, form_new, "Form", 1, JS_CFUNC_constructor, 0));

    JS_NewClassID(&AnchorClassID);
    JS_NewClass(rt, AnchorClassID, &AnchorClass);
    JS_SetPropertyStr(ctx, gl, "Anchor",
		      JS_NewCFunction2(ctx, anchor_new, "Anchor", 1, JS_CFUNC_constructor, 0));

    JS_NewClassID(&HistoryClassID);
    JS_NewClass(rt, HistoryClassID, &HistoryClass);
    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, HistoryFuncs,
			       sizeof(HistoryFuncs) / sizeof(HistoryFuncs[0]));
    JS_SetClassProto(ctx, HistoryClassID, proto);
    JS_SetPropertyStr(ctx, gl, "History",
		      JS_NewCFunction2(ctx, history_new, "History", 1, JS_CFUNC_constructor, 0));

    JS_NewClassID(&NavigatorClassID);
    JS_NewClass(rt, NavigatorClassID, &NavigatorClass);
    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, NavigatorFuncs,
			       sizeof(NavigatorFuncs) / sizeof(NavigatorFuncs[0]));
    JS_SetClassProto(ctx, NavigatorClassID, proto);
    JS_SetPropertyStr(ctx, gl, "Navigator",
		      JS_NewCFunction2(ctx, navigator_new, "Navigator", 1,
				       JS_CFUNC_constructor, 0));

    JS_NewClassID(&DocumentClassID);
    JS_NewClass(rt, DocumentClassID, &DocumentClass);
    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, DocumentFuncs,
			       sizeof(DocumentFuncs) / sizeof(DocumentFuncs[0]));
    JS_SetClassProto(ctx, DocumentClassID, proto);
    JS_SetPropertyStr(ctx, gl, "Document",
		      JS_NewCFunction2(ctx, document_new, "Document", 1, JS_CFUNC_constructor, 0));

    JS_FreeValue(ctx, gl);

    return ctx;
}

JSValue
js_eval(JSContext *ctx, char *str) {
    JSValue ret = JS_Eval(ctx, str, strlen(str), "<input>", 0 /* JS_EVAL_FLAG_STRICT */);

    if (JS_IsException(ret)) {
	JSValue err = JS_GetException(ctx);
#ifdef SCRIPT_DEBUG
	const char *err_str = JS_ToCString(ctx, err);
	FILE *fp = fopen("scriptlog.txt", "a");
	fprintf(fp, "<%s>\n%s\n", err_str, str);
	JS_FreeCString(ctx, err_str);
	fclose(fp);
#endif
	JS_FreeValue(ctx, err);
    }

    return ret;
}

char *
js_get_cstr(JSContext *ctx, JSValue value)
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

Str
js_get_str(JSContext *ctx, JSValue value)
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
#endif
