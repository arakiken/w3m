/* -*- tab-width:8; c-basic-offset:4 -*- */
#include "fm.h"
#ifdef USE_JAVASCRIPT
#include "js_html.h"
#include <sys/utsname.h>

static JSMethodResult
window_new(JSClassPtr cls, JSInterpPtr interp, int argc, JSType *argv,
	void **ictx_return, JSFreeProc *ictx_destructor_return,
	char *error_return)
{
    WindowCtx *ictx;

    ictx = (WindowCtx *)GC_MALLOC(sizeof(WindowCtx));
    ictx->win = newGeneralList();
    ictx->close = FALSE;
    *ictx_return = ictx;
    *ictx_destructor_return = NULL;
    return JS_OK;
}

static JSMethodResult
window_open(JSClassPtr cls, void *instance_context, JSInterpPtr interp,
	int argc, JSType *argv, JSType *result_return, char *error_return)
{
    WindowCtx *ictx = (WindowCtx *)instance_context;
    jse_windowopen_t *w;

    if (! ictx && !(ictx = js_class_context(cls)))
	return JS_ERROR;

    if (argc <= 1)
	return JS_ERROR;

    if (argv[0].type != JS_TYPE_STRING || argv[1].type != JS_TYPE_STRING)
	return JS_ERROR;

    w = (jse_windowopen_t *)GC_MALLOC(sizeof(jse_windowopen_t));
    w->url  = allocStr(argv[0].u.s->data, argv[0].u.s->len);
    w->name = allocStr(argv[1].u.s->data, argv[1].u.s->len);
    pushValue(ictx->win, (void *)w);

    return JS_OK;
}

static JSMethodResult
window_close(JSClassPtr cls, void *instance_context, JSInterpPtr interp,
	int argc, JSType *argv, JSType *result_return, char *error_return)
{
    WindowCtx *ictx = (WindowCtx *)instance_context;

    if (! ictx && !(ictx = js_class_context(cls)))
	return JS_ERROR;

    ictx->close = TRUE;

    return JS_OK;
}

static JSMethodResult
location_new(JSClassPtr cls, JSInterpPtr interp, int argc, JSType *argv,
	void **ictx_return, JSFreeProc *ictx_destructor_return,
	char *error_return)
{
    LocationCtx *ictx;

    ictx = (LocationCtx *)GC_MALLOC(sizeof(LocationCtx));
    bzero(ictx, sizeof(LocationCtx));
    if (argc > 0 && argv[0].type == JS_TYPE_STRING) {
	ictx->url = Strnew_charp_n(argv[0].u.s->data, argv[0].u.s->len);
	parseURL(ictx->url->ptr, &ictx->pu, NULL);
    } else {
	ictx->url = Strnew();
    }
    ictx->refresh = 0;
    *ictx_return = ictx;
    *ictx_destructor_return = NULL;
    return JS_OK;
}

static JSMethodResult
location_replace(JSClassPtr cls, void *instance_context, JSInterpPtr interp,
	int argc, JSType *argv, JSType *result_return, char *error_return)
{
    LocationCtx *ictx = (LocationCtx *)instance_context;

    if (! ictx && !(ictx = js_class_context(cls)))
	return JS_ERROR;

    if (argc != 1)
	return JS_ERROR;

    if (argv[0].type != JS_TYPE_STRING)
	return JS_ERROR;

    ictx->url = Strnew_charp_n(argv[0].u.s->data, argv[0].u.s->len);
    parseURL(ictx->url->ptr, &ictx->pu, NULL);
    ictx->refresh |= JS_LOC_REFRESH;

    return JS_OK;
}

static JSMethodResult
location_reload(JSClassPtr cls, void *instance_context, JSInterpPtr interp,
	int argc, JSType *argv, JSType *result_return, char *error_return)
{
    LocationCtx *ictx = (LocationCtx *)instance_context;

    if (! ictx && !(ictx = js_class_context(cls)))
	return JS_ERROR;

    if (argc != 0)
	return JS_ERROR;

    ictx->refresh |= JS_LOC_REFRESH;
    return JS_OK;
}

static JSMethodResult
location_href(JSClassPtr cls, void *instance_context,
	JSInterpPtr interp, int setp, JSType *value, char *error_return)
{
    LocationCtx *ictx = (LocationCtx *)instance_context;
    if (! ictx && !(ictx = js_class_context(cls)))
	return JS_ERROR;

    if (setp) {
	if (value->type != JS_TYPE_STRING)
	    return JS_ERROR;
	ictx->url = Strnew_charp_n(value->u.s->data, value->u.s->len);
	parseURL(ictx->url->ptr, &ictx->pu, NULL);
	ictx->refresh |= JS_LOC_REFRESH;
    } else {
	js_type_make_string(interp, value, ictx->url->ptr, ictx->url->length);
    }

    return JS_OK;
}

static JSMethodResult
location_protocol(JSClassPtr cls, void *instance_context,
	JSInterpPtr interp, int setp, JSType *value, char *error_return)
{
    LocationCtx *ictx = (LocationCtx *)instance_context;
    Str s;
    char *p;

    if (! ictx && !(ictx = js_class_context(cls)))
	return JS_ERROR;

    if (setp) {
	if (value->type != JS_TYPE_STRING)
	    return JS_ERROR;
	s = Strnew_charp_n(value->u.s->data, value->u.s->len);
	Strcat_charp(s, ":");
	p = allocStr(s->ptr, s->length);
	ictx->pu.scheme = getURLScheme(&p);
	ictx->url = parsedURL2Str(&ictx->pu);
fprintf(stderr, "*****URL:%s\n", ictx->url->ptr);
	ictx->refresh |= JS_LOC_REFRESH;
    } else {
	s = Strnew();
	switch (ictx->pu.scheme) {
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
	js_type_make_string(interp, value, s->ptr, s->length);
    }

    return JS_OK;
}

static JSMethodResult
location_host(JSClassPtr cls, void *instance_context,
	JSInterpPtr interp, int setp, JSType *value, char *error_return)
{
    LocationCtx *ictx = (LocationCtx *)instance_context;
    Str s;
    char *p, *q;

    if (! ictx && !(ictx = js_class_context(cls)))
	return JS_ERROR;

    if (setp) {
	if (value->type != JS_TYPE_STRING)
	    return JS_ERROR;
	p = allocStr(value->u.s->data, value->u.s->len);
	if ((q = strchr(p, ':')) != NULL) {
	    *q++ = '\0';
	    ictx->pu.port = atoi(q);
	    ictx->pu.has_port = 1;
	}
	ictx->pu.host = Strnew_charp(p)->ptr;
	ictx->url = parsedURL2Str(&ictx->pu);
	ictx->refresh |= JS_LOC_REFRESH;
    } else {
	s = Strnew();
	if (ictx->pu.host) {
	    Strcat_charp_n(s, ictx->pu.host, strlen(ictx->pu.host));
	    if (ictx->pu.has_port)
		Strcat(s, Sprintf(":%d", ictx->pu.port));
	}
	js_type_make_string(interp, value, s->ptr, s->length);
    }

    return JS_OK;
}

static JSMethodResult
location_hostname(JSClassPtr cls, void *instance_context,
	JSInterpPtr interp, int setp, JSType *value, char *error_return)
{
    LocationCtx *ictx = (LocationCtx *)instance_context;
    Str s;

    if (! ictx && !(ictx = js_class_context(cls)))
	return JS_ERROR;

    if (setp) {
	if (value->type != JS_TYPE_STRING)
	    return JS_ERROR;
	ictx->pu.host = allocStr(value->u.s->data, value->u.s->len);
	ictx->url = parsedURL2Str(&ictx->pu);
	ictx->refresh |= JS_LOC_REFRESH;
    } else {
	s = Strnew();
	if (ictx->pu.host) {
	    Strcat_charp_n(s, ictx->pu.host, strlen(ictx->pu.host));
	}
	js_type_make_string(interp, value, s->ptr, s->length);
    }

    return JS_OK;
}

static JSMethodResult
location_port(JSClassPtr cls, void *instance_context,
	JSInterpPtr interp, int setp, JSType *value, char *error_return)
{
    LocationCtx *ictx = (LocationCtx *)instance_context;
    Str s;

    if (! ictx && !(ictx = js_class_context(cls)))
	return JS_ERROR;

    if (setp) {
	switch (value->type) {
	case JS_TYPE_INTEGER:
	    ictx->pu.port = value->u.i;
	    break;
	case JS_TYPE_DOUBLE:
	    ictx->pu.port = (int)value->u.d;
	    break;
	case JS_TYPE_STRING:
	    ictx->pu.port = atoi(value->u.s->data);
	    break;
	default:
	    return JS_ERROR;
	}
	ictx->url = parsedURL2Str(&ictx->pu);
	ictx->refresh |= JS_LOC_REFRESH;
    } else {
	if (ictx->pu.has_port)
	    s = Sprintf("%d", ictx->pu.port);
	else
	    s = Strnew();
	js_type_make_string(interp, value, s->ptr, s->length);
    }

    return JS_OK;
}

static JSMethodResult
location_pathname(JSClassPtr cls, void *instance_context,
	JSInterpPtr interp, int setp, JSType *value, char *error_return)
{
    LocationCtx *ictx = (LocationCtx *)instance_context;
    Str s;

    if (! ictx && !(ictx = js_class_context(cls)))
	return JS_ERROR;

    if (setp) {
	if (value->type != JS_TYPE_STRING)
	    return JS_ERROR;
	ictx->pu.file = allocStr(value->u.s->data, value->u.s->len);
	ictx->url = parsedURL2Str(&ictx->pu);
	ictx->refresh |= JS_LOC_REFRESH;
    } else {
	s = Strnew();
	if (ictx->pu.file) {
	    Strcat_charp_n(s, ictx->pu.file, strlen(ictx->pu.file));
	}
	js_type_make_string(interp, value, s->ptr, s->length);
    }

    return JS_OK;
}

static JSMethodResult
location_search(JSClassPtr cls, void *instance_context,
	JSInterpPtr interp, int setp, JSType *value, char *error_return)
{
    LocationCtx *ictx = (LocationCtx *)instance_context;
    Str s;

    if (! ictx && !(ictx = js_class_context(cls)))
	return JS_ERROR;

    if (setp) {
	if (value->type != JS_TYPE_STRING)
	    return JS_ERROR;
	ictx->pu.query = allocStr(value->u.s->data, value->u.s->len);
	ictx->url = parsedURL2Str(&ictx->pu);
	ictx->refresh |= JS_LOC_REFRESH;
    } else {
	s = Strnew();
	if (ictx->pu.query) {
	    Strcat_charp(s, "?");
	    Strcat_charp_n(s, ictx->pu.query, strlen(ictx->pu.query));
	}
	js_type_make_string(interp, value, s->ptr, s->length);
    }

    return JS_OK;
}

static JSMethodResult
location_hash(JSClassPtr cls, void *instance_context,
	JSInterpPtr interp, int setp, JSType *value, char *error_return)
{
    LocationCtx *ictx = (LocationCtx *)instance_context;
    Str s;

    if (! ictx && !(ictx = js_class_context(cls)))
	return JS_ERROR;

    if (setp) {
	if (value->type != JS_TYPE_STRING)
	    return JS_ERROR;
	ictx->pu.label = allocStr(value->u.s->data, value->u.s->len);
	ictx->url = parsedURL2Str(&ictx->pu);
	ictx->refresh |= JS_LOC_HASH;
    } else {
	s = Strnew();
	if (ictx->pu.label) {
	    Strcat_charp(s, "#");
	    Strcat_charp_n(s, ictx->pu.label, strlen(ictx->pu.label));
	}
	js_type_make_string(interp, value, s->ptr, s->length);
    }

    return JS_OK;
}

static JSMethodResult
cookie_new(JSClassPtr cls, JSInterpPtr interp, int argc, JSType *argv,
	void **ictx_return, JSFreeProc *ictx_destructor_return,
	char *error_return)
{
    *ictx_destructor_return = NULL;
    return JS_OK;
}

static JSMethodResult
image_new(JSClassPtr cls, JSInterpPtr interp, int argc, JSType *argv,
	void **ictx_return, JSFreeProc *ictx_destructor_return,
	char *error_return)
{
    *ictx_destructor_return = NULL;
    return JS_OK;
}

static JSMethodResult
formitem_new(JSClassPtr cls, JSInterpPtr interp, int argc, JSType *argv,
	void **ictx_return, JSFreeProc *ictx_destructor_return,
	char *error_return)
{
    *ictx_destructor_return = NULL;
    return JS_OK;
}

static JSMethodResult
form_new(JSClassPtr cls, JSInterpPtr interp, int argc, JSType *argv,
	void **ictx_return, JSFreeProc *ictx_destructor_return,
	char *error_return)
{
    *ictx_destructor_return = NULL;
    return JS_OK;
}

static JSMethodResult
form_submit(JSClassPtr cls, void *instance_context, JSInterpPtr interp,
	int argc, JSType *argv, JSType *result_return, char *error_return)
{
    /* TODO: form submit flag on */
    return JS_OK;
}

static JSMethodResult
form_reset(JSClassPtr cls, void *instance_context, JSInterpPtr interp,
	int argc, JSType *argv, JSType *result_return, char *error_return)
{
    /* TODO: form reset flag on */
    return JS_OK;
}

static JSMethodResult
anchor_new(JSClassPtr cls, JSInterpPtr interp, int argc, JSType *argv,
	void **ictx_return, JSFreeProc *ictx_destructor_return,
	char *error_return)
{
    *ictx_destructor_return = NULL;
    return JS_OK;
}

static JSMethodResult
history_new(JSClassPtr cls, JSInterpPtr interp, int argc, JSType *argv,
	void **ictx_return, JSFreeProc *ictx_destructor_return,
	char *error_return)
{
    HistoryCtx *ictx;

    ictx = (HistoryCtx *)GC_MALLOC(sizeof(HistoryCtx));
    ictx->pos = 0;
    *ictx_return = ictx;
    *ictx_destructor_return = NULL; /* TODO: GC_free() ? */
    return JS_OK;
}

static JSMethodResult
history_back(JSClassPtr cls, void *instance_context, JSInterpPtr interp,
	int argc, JSType *argv, JSType *result_return, char *error_return)
{
    HistoryCtx *ictx = (HistoryCtx *)instance_context;

    if (! ictx && !(ictx = js_class_context(cls)))
	return JS_ERROR;

    ictx->pos = -1;

    return JS_OK;
}

static JSMethodResult
history_forward(JSClassPtr cls, void *instance_context, JSInterpPtr interp,
	int argc, JSType *argv, JSType *result_return, char *error_return)
{
    HistoryCtx *ictx = (HistoryCtx *)instance_context;

    if (! ictx && !(ictx = js_class_context(cls)))
	return JS_ERROR;

    ictx->pos = 1;

    return JS_OK;
}

static JSMethodResult
history_go(JSClassPtr cls, void *instance_context, JSInterpPtr interp,
	int argc, JSType *argv, JSType *result_return, char *error_return)
{
    HistoryCtx *ictx = (HistoryCtx *)instance_context;
    int i;

    if (! ictx && !(ictx = js_class_context(cls)))
	return JS_ERROR;

    if (argc != 1)
	return JS_OK;

    switch (argv[0].type) {
    case JS_TYPE_INTEGER:
	ictx->pos = argv[0].u.i;
	break;
    case JS_TYPE_DOUBLE:
	ictx->pos = (int)argv[0].u.d;
	break;
    }

    return JS_OK;
}

static JSMethodResult
navigator_new(JSClassPtr cls, JSInterpPtr interp, int argc, JSType *argv,
	void **ictx_return, JSFreeProc *ictx_destructor_return,
	char *error_return)
{
    NavigatorCtx *ictx;

    ictx = (NavigatorCtx *)GC_MALLOC(sizeof(NavigatorCtx));
    ictx->appcodename = NULL;
    ictx->appname = NULL;
    ictx->appversion = NULL;
    ictx->useragent = NULL;
    *ictx_return = ictx;
    *ictx_destructor_return = NULL; /* TODO: GC_free() ? */
    return JS_OK;
}

static JSMethodResult
navigator_appcodename(JSClassPtr cls, void *instance_context,
	JSInterpPtr interp, int setp, JSType *value, char *error_return)
{
    NavigatorCtx *ictx = (NavigatorCtx *)instance_context;
    if (! ictx && !(ictx = js_class_context(cls)))
	return JS_ERROR;

    if (setp)
	return JS_ERROR; /* Forbidden */

    if (! ictx->appcodename)
	ictx->appcodename = Strnew_charp("w3m");

    js_type_make_string(interp, value,
	ictx->appcodename->ptr, ictx->appcodename->length);

    return JS_OK;
}

static JSMethodResult
navigator_appname(JSClassPtr cls, void *instance_context,
	JSInterpPtr interp, int setp, JSType *value, char *error_return)
{
    NavigatorCtx *ictx = (NavigatorCtx *)instance_context;
    if (! ictx && !(ictx = js_class_context(cls)))
	return JS_ERROR;

    if (setp)
	return JS_ERROR; /* Forbidden */

    if (! ictx->appname)
	ictx->appname = Strnew_charp("w3m");

    js_type_make_string(interp, value,
	ictx->appname->ptr, ictx->appname->length);

    return JS_OK;
}

static JSMethodResult
navigator_appversion(JSClassPtr cls, void *instance_context,
	JSInterpPtr interp, int setp, JSType *value, char *error_return)
{
    NavigatorCtx *ictx = (NavigatorCtx *)instance_context;
#if LANG == JA
    const char *lang = "ja-JP";
#else
    const char *lang = "en-US";
#endif
    char *platform;
    struct utsname unamebuf;
    char *p;
    int n;

    if (! ictx && !(ictx = js_class_context(cls)))
	return JS_ERROR;

    if (setp)
	return JS_ERROR; /* Forbidden */

    if (! ictx->appversion) {
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
		ictx->appversion = Sprintf("%s (%s; %s)", rnum->ptr, platform, lang);
		Strfree(rnum);
	    }
	}
    }

    js_type_make_string(interp, value,
	ictx->appversion->ptr, ictx->appversion->length);

    return JS_OK;
}

static JSMethodResult
navigator_useragent(JSClassPtr cls, void *instance_context,
	JSInterpPtr interp, int setp, JSType *value, char *error_return)
{
    NavigatorCtx *ictx = (NavigatorCtx *)instance_context;
    if (! ictx && !(ictx = js_class_context(cls)))
	return JS_ERROR;

    if (setp)
	return JS_ERROR; /* Forbidden */

    if (! ictx->useragent) {
	if (UserAgent == NULL || *UserAgent == '\0')
	    ictx->useragent = Strnew_charp(w3m_version);
	else
	    ictx->useragent = Strnew_charp(UserAgent);
    }

    js_type_make_string(interp, value,
	ictx->useragent->ptr, ictx->useragent->length);

    return JS_OK;
}

static JSMethodResult
document_new(JSClassPtr cls, JSInterpPtr interp, int argc, JSType *argv,
	void **ictx_return, JSFreeProc *ictx_destructor_return,
	char *error_return)
{
    DocumentCtx *ictx;

    ictx = (DocumentCtx *)GC_MALLOC(sizeof(DocumentCtx));
    ictx->write = NULL;
    *ictx_return = ictx;
    *ictx_destructor_return = NULL;
    return JS_OK;
}

static JSMethodResult
document_write(JSClassPtr cls, void *instance_context, JSInterpPtr interp,
	int argc, JSType *argv, JSType *result_return, char *error_return)
{
    DocumentCtx *ictx = (DocumentCtx *)instance_context;
    JSType *a;
    int i;

    if (! ictx)
	return JS_ERROR;

    if (! ictx->write)
	ictx->write = Strnew();
    for (i = 0; i < argc; i++) {
	a = &argv[i];
	switch (a->type) {
	case JS_TYPE_BOOLEAN:
	    Strcat_charp(ictx->write, a->u.d ? "true" : "false");
	    break;
	case JS_TYPE_INTEGER:
	    Strcat(ictx->write, Sprintf("%d", a->u.i));
	    break;
	case JS_TYPE_DOUBLE:
	    Strcat(ictx->write, Sprintf("%.21g", a->u.d));
	    break;
	case JS_TYPE_STRING:
	    Strcat_charp_n(ictx->write, a->u.s->data, a->u.s->len);
	    break;
	default:
	    break;
	}
    }
    return JS_OK;
}

static JSMethodResult
document_writeln(JSClassPtr cls, void *instance_context, JSInterpPtr interp,
	int argc, JSType *argv, JSType *result_return, char *error_return)
{
    DocumentCtx *ictx = (DocumentCtx *)instance_context;
    JSType *a;
    int i;

    if (! ictx)
	return JS_ERROR;

    document_write(cls, instance_context, interp,
		   argc, argv, result_return, error_return);
    Strcat_charp(ictx->write, "\n");

    return JS_OK;
}

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

JSInterpPtr
js_html_init(void)
{
    JSInterpPtr interp;
    JSClassPtr window_cls;
    JSClassPtr document_cls, navigator_cls, history_cls;
    JSClassPtr anchor_cls, form_cls, formitem_cls, image_cls;
    JSClassPtr cookie_cls, location_cls;
    JSInterpOptions options;

    js_init_default_options (&options);

#if 0
    options.verbose = 0;
    options.s_stdout = io_stdout;
    options.s_stderr = io_stderr;
    options.s_context = NULL;
#endif

    interp = js_create_interp (&options);

    window_cls = js_class_create("(Window)", NULL, 0, window_new);
    js_class_define_method(window_cls, "open", JS_CF_STATIC, window_open);
    js_class_define_method(window_cls, "close", JS_CF_STATIC, window_close);
    js_define_class(interp, window_cls, "Window");

    document_cls = js_class_create("(Document)", NULL, 0, document_new);
    js_class_define_method(document_cls, "write", JS_CF_STATIC, document_write);
    js_class_define_method(document_cls, "writeln", JS_CF_STATIC, document_writeln);
    js_define_class(interp, document_cls, "Document");

    navigator_cls = js_class_create("(Navigator)", NULL, 0, navigator_new);
    js_class_define_property(navigator_cls, "appCodeName",
		JS_CF_STATIC|JS_CF_IMMUTABLE, navigator_appcodename);
    js_class_define_property(navigator_cls, "appName",
		JS_CF_STATIC|JS_CF_IMMUTABLE, navigator_appname);
    js_class_define_property(navigator_cls, "appVersion",
		JS_CF_STATIC|JS_CF_IMMUTABLE, navigator_appversion);
    js_class_define_property(navigator_cls, "userAgent",
		JS_CF_STATIC|JS_CF_IMMUTABLE, navigator_useragent);
    js_define_class(interp, navigator_cls, "Navigator");

    history_cls = js_class_create("(History)", NULL, 0, history_new);
    js_class_define_method(history_cls, "back", JS_CF_STATIC, history_back);
    js_class_define_method(history_cls, "forward",
				JS_CF_STATIC, history_forward);
    js_class_define_method(history_cls, "go", JS_CF_STATIC, history_go);
    js_define_class(interp, history_cls, "History");

    anchor_cls = js_class_create("(Anchor)", NULL, 0, anchor_new);
    js_define_class(interp, anchor_cls, "Anchor");

    form_cls = js_class_create("(Form)", NULL, 0, form_new);
    js_class_define_method(form_cls, "submit", JS_CF_STATIC, form_submit);
    js_class_define_method(form_cls, "reset", JS_CF_STATIC, form_reset);
    js_define_class(interp, form_cls, "Form");

    formitem_cls = js_class_create("(FormItem)", NULL, 0, formitem_new);
    js_define_class(interp, formitem_cls, "FormItem");

    image_cls = js_class_create("(Image)", NULL, 0, image_new);
    js_define_class(interp, image_cls, "Image");

    cookie_cls = js_class_create("(Cookie)", NULL, 0, cookie_new);
    js_define_class(interp, cookie_cls, "Cookie");

    location_cls = js_class_create("(Location)", NULL, 0, location_new);
    js_class_define_method(location_cls, "replace", JS_CF_STATIC, location_replace);
    js_class_define_method(location_cls, "reload", JS_CF_STATIC, location_reload);
    js_class_define_property(location_cls, "href", JS_CF_STATIC, location_href);
    js_class_define_property(location_cls, "protocol", JS_CF_STATIC, location_protocol);
    js_class_define_property(location_cls, "host", JS_CF_STATIC, location_host);
    js_class_define_property(location_cls, "hostname", JS_CF_STATIC, location_hostname);
    js_class_define_property(location_cls, "port", JS_CF_STATIC, location_port);
    js_class_define_property(location_cls, "pathname", JS_CF_STATIC, location_pathname);
    js_class_define_property(location_cls, "search", JS_CF_STATIC, location_search);
    js_class_define_property(location_cls, "hash", JS_CF_STATIC, location_hash);
    js_define_class(interp, location_cls, "Location");

    return interp;
}
#endif
