/* -*- tab-width:8; c-basic-offset:4 -*- */
#include "fm.h"
#ifdef USE_SCRIPT
#ifdef USE_JAVASCRIPT
#include "js_html.h"

static void
script_buf2js(Buffer *buf, JSInterpPtr interp)
{
    Buffer *tb;
    int i, j;
    AnchorList *aln, *alh;
    Anchor *an, *ah;
    FormList *fl;
    FormItemList *fi;

    js_eval(interp, "window = new Window();");
    js_eval(interp, "document = new Document();");
    js_eval(interp, "navigator = new Navigator();");
    js_eval(interp, "history = new History();");
    if (buf != NULL) {
	js_eval(interp, Sprintf("location = new Location(\"%s\");",
		parsedURL2Str(&buf->currentURL)->ptr)->ptr);
    } else {
	js_eval(interp, "location = new Location();");
    }
    js_eval(interp, "document.anchors = new Array();");
    js_eval(interp, "document.forms = new Array();");

    i = 0;
    if (CurrentTab != NULL) {
	for (tb = Firstbuf; tb != NULL; i++, tb = tb->nextBuffer);
    }
    js_eval(interp, Sprintf("history.length = %d;", i)->ptr);

    if (buf != NULL) {
	char *t, *c;
	Str cs;

	js_eval(interp, Sprintf("document.URL = \"%s\";", parsedURL2Str(&buf->currentURL)->ptr)->ptr);

	switch (buf->currentURL.scheme) {
	case SCM_HTTP:
	case SCM_GOPHER:
	case SCM_FTP:
#ifdef USE_SSL
	case SCM_HTTPS:
#endif
	    if (buf->currentURL.host != NULL)
		js_eval(interp, Sprintf("document.domain = \"%s\";", buf->currentURL.host)->ptr);
	    break;
	}
	js_eval(interp, Sprintf("document.lastModified = \"%s\";", last_modified(buf))->ptr);
	c = t = "";
	if (buf->buffername != NULL)
	    t = buf->buffername;
#ifdef USE_COOKIE
	cs = find_cookie(&buf->currentURL);
	if (cs) c = cs->ptr;
#endif

	js_eval(interp, Sprintf("document.title = \"%s\";", t)->ptr);
	js_eval(interp, Sprintf("document.cookie = \"%s\";", c)->ptr);

	if (buf->name != NULL) {
	    aln = buf->name;
	    alh = buf->href;
	    for (i = 0; i < aln->nanchor; i++) {
		char *n, *h;

		an = &aln->anchors[i];
		ah = &alh->anchors[an->hseq];

		js_eval(interp, Sprintf("document.anchors[%d] = new Anchor();", i)->ptr);

		n = h = "";
		if (an->url != NULL)
		    n = an->url;
		if (ah->url != NULL)
		    h = ah->url; /* TODO: It shall be Absolute URL. */

		js_eval(interp, Sprintf("document.anchors[%d].name = \"%s\";", i, n)->ptr);
		js_eval(interp, Sprintf("document.anchors[%d].href = \"%s\";", i, h)->ptr);
		if (an->url != NULL) {
		    js_eval(interp, Sprintf("document.%s = document.anchors[%d];", n, i)->ptr);
/*		    js_eval(interp, Sprintf("document.anchor[\"%s\"] = document.anchors[%d];", n, i)->ptr);*/
		}
	    }
	}
	if (buf->formlist != NULL) {
	    for (i = 0, fl = buf->formlist; fl != NULL; i++, fl = fl->next) {
		char *m, *a, *e, *n, *t;

		js_eval(interp, Sprintf("document.forms[%d] = new Form();", i)->ptr);
		m = "get";
		if (fl->method == FORM_METHOD_POST) {
		    m = "post";
		}
		a = e = n = t = "";
		if (fl->enctype == FORM_ENCTYPE_MULTIPART) {
		    e = "multipart/form-data";
		}
		if (fl->action && fl->action->length > 0)
		    a = fl->action->ptr;
		if (fl->name != NULL)
		    n = fl->name;
		if (fl->target != NULL)
		    t = fl->target;

		js_eval(interp, Sprintf("document.forms[%d].method = \"%s\";", i, m)->ptr);
		js_eval(interp, Sprintf("document.forms[%d].action = \"%s\";", i, a)->ptr);
		js_eval(interp, Sprintf("document.forms[%d].encoding = \"%s\";", i, e)->ptr);
		js_eval(interp, Sprintf("document.forms[%d].name = \"%s\";", i, n)->ptr);
		if (fl->name != NULL) {
		    js_eval(interp, Sprintf("document.%s = document.forms[%d];", n, i)->ptr);
/*		    js_eval(interp, Sprintf("document.forms[\"%s\"] = document.forms[%d];", n, i)->ptr);*/
		}
		js_eval(interp, Sprintf("document.forms[%d].target = \"%s\";", i, t)->ptr);
		js_eval(interp, Sprintf("document.forms[%d].elements = new Array();", i, fl->nitems)->ptr);

		for (j = 0, fi = fl->item; fi != NULL; j++, fi = fi->next) {
		    char *n, *t, *v;

		    js_eval(interp, Sprintf("document.forms[%d].elements[%d] = new FormItem();", i, j)->ptr);

		    n = v = "";
		    if (fi->name && fi->name->length > 0)
			n = fi->name->ptr;

		    switch (fi->type) { /* TODO: ....... */
		    case FORM_INPUT_TEXT:
			t = "text";
			if (fi->init_value->length > 0)
			    js_eval(interp, Sprintf("document.forms[%d].elements[%d].defaultValue = \"%s\";", i, j, fi->init_value->ptr)->ptr);
			break;
		    case FORM_INPUT_PASSWORD:
			t = "password";
			if (fi->init_value->length > 0)
			    js_eval(interp, Sprintf("document.forms[%d].elements[%d].defaultValue = \"%s\";", i, j, fi->init_value->ptr)->ptr);
			break;
		    case FORM_INPUT_CHECKBOX:
			t = "checkbox";
			if (fi->init_checked)
			    js_eval(interp, Sprintf("document.forms[%d].elements[%d].defaultChecked = true;", i, j)->ptr);
			else
			    js_eval(interp, Sprintf("document.forms[%d].elements[%d].defaultChecked = false;", i, j)->ptr);
			if (fi->checked)
			    js_eval(interp, Sprintf("document.forms[%d].elements[%d].checked = true;", i, j)->ptr);
			else
			    js_eval(interp, Sprintf("document.forms[%d].elements[%d].checked = false;", i, j)->ptr);
			break;
		    case FORM_INPUT_RADIO:
			t = "radio";
			if (fi->init_checked)
			    js_eval(interp, Sprintf("document.forms[%d].elements[%d].defaultChecked = true;", i, j)->ptr);
			else
			    js_eval(interp, Sprintf("document.forms[%d].elements[%d].defaultChecked = false;", i, j)->ptr);
			if (fi->checked)
			    js_eval(interp, Sprintf("document.forms[%d].elements[%d].checked = true;", i, j)->ptr);
			else
			    js_eval(interp, Sprintf("document.forms[%d].elements[%d].checked = false;", i, j)->ptr);
			break;
		    case FORM_INPUT_SUBMIT:
			t = "submit"; break;
		    case FORM_INPUT_RESET:
			t = "reset"; break;
		    case FORM_INPUT_HIDDEN:
			t = "hidden"; break;
		    case FORM_INPUT_IMAGE:
			t = "image"; break;
		    case FORM_SELECT:
			t = "select";
#ifdef MENU_SELECT
			/* TODO: option ............ */
			js_eval(interp, Sprintf("document.forms[%d].elements[%d].selectedIndex = %d;", i, j, fi->selected)->ptr);
#endif
			break;
		    case FORM_TEXTAREA:
			t = "textarea";
			if (fi->init_value->length > 0)
			    js_eval(interp, Sprintf("document.forms[%d].elements[%d].defaultValue = \"%s\";", i, j, fi->init_value->ptr)->ptr);
			break;
		    case FORM_INPUT_BUTTON:
			t = "button"; break;
		    case FORM_INPUT_FILE:
			t = "file";
			if (fi->init_value && fi->init_value->length > 0) /* TODO: Read Only */
			    js_eval(interp, Sprintf("document.forms[%d].elements[%d].defaultValue = \"%s\";", i, j, fi->init_value->ptr)->ptr);
			break;
		    default:
			t = ""; break;
		    }

		    if (fi->value && fi->value->length > 0)
			v = fi->value->ptr;

		    js_eval(interp, Sprintf("document.forms[%d].elements[%d].name = \"%s\";", i, j, n)->ptr);
		    js_eval(interp, Sprintf("document.forms[%d].elements[%d].type = \"%s\";", i, j, t)->ptr);
		    js_eval(interp, Sprintf("document.forms[%d].elements[%d].value = \"%s\";", i, j, v)->ptr);
		    if (fi->name && fi->name->length > 0)
			js_eval(interp, Sprintf("document.forms[%d].%s = document.forms[%d].elements[%d];", i, n, i, j)->ptr);
		}
	    }
	}
    }

    /* js_eval(interp, s->ptr);*/
}

static void
script_chBuf(int pos)
{
    int i;
    Buffer *buf;

    if (pos > 0) {
	for (i = 0; i < pos; i++) {
	    buf = prevBuffer(Firstbuf, Currentbuf);
	    if (!buf) {
		if (i == 0)
		    return;
		break;
	    }
	    Currentbuf = buf;
	}
    } else {
	for (i = 0; i < -pos; i++) {
	    buf = Currentbuf->nextBuffer;
	    if (!buf) {
		if (i == 0)
		    return;
		break;
	    }
	    Currentbuf = buf;
	}
    }
    displayBuffer(Currentbuf, B_FORCE_REDRAW);
}

static Str
script_js2buf(Buffer *buf, JSInterpPtr interp)
{
    JSType value;
    DocumentCtx *dictx = NULL;
    HistoryCtx	*hictx = NULL;
    LocationCtx *lictx = NULL;
    WindowCtx	*wictx = NULL;

    js_eval(interp, "window;");
    js_result(interp, &value);
    if (js_isa(interp, &value, js_lookup_class(interp, "Window"),
	(void **)&wictx) && wictx) {
	if (wictx->win && wictx->win->nitem) {
	    jse_windowopen_t	*w;
	    while ((w = popValue(wictx->win)) != NULL) {
		if (enable_js_windowopen) {
		    jWindowOpen(buf, w->url, w->name);
		}
	    }
	}
	if (wictx->close) {
	    if (CurrentTab != NULL && Currentbuf != NULL) {
		char *ans = "y";
		if (buf && confirm_js_windowclose) {
		    ans = inputChar("window.close() ? (y/n)");
		}
		if (ans && tolower(*ans) == 'y') {
		    closeT();
		    return NULL;
		}
	    }
	    wictx->close = FALSE;
	}
    }

    if (CurrentTab != NULL && Currentbuf != NULL) {
	js_eval(interp, "history;");
	js_result(interp, &value);
	if (js_isa(interp, &value, js_lookup_class(interp, "History"),
	    (void **)&hictx) && hictx && hictx->pos != 0) {
	    script_chBuf(hictx->pos);
	    hictx->pos = 0;
	    return NULL;
	}
    }

    js_eval(interp, "document.location;");
    js_result(interp, &value);
    if (buf && value.type == JS_TYPE_STRING && value.u.s->len)
	buf->location = allocStr(value.u.s->data, value.u.s->len);

    js_eval(interp, "location;");
    js_result(interp, &value);
    if (js_isa(interp, &value, js_lookup_class(interp, "Location"),
	(void **)&lictx) && lictx) {
	if (buf && lictx->url && lictx->refresh) {
	    if (lictx->refresh & JS_LOC_REFRESH) {
		buf->location = allocStr(lictx->url->ptr, lictx->url->length);
	    } else if (lictx->refresh & JS_LOC_HASH) {
		Anchor *a;
#ifdef JP_CHARSET
		a = searchURLLabel(buf,
				   conv(lictx->pu.label, buf->document_code,
					InnerCode)->ptr);
#else				/* not JP_CHARSET */
		a = searchURLLabel(buf, lictx->pu.label);
#endif				/* not JP_CHARSET */
		if (a != NULL) {
		    gotoLine(buf, a->start.line);
#ifdef LABEL_TOPLINE
		    if (label_topline)
			buf->topLine = lineSkip(buf, buf->topLine,
						buf->currentLine->linenumber
						- buf->topLine->linenumber,
						FALSE);
#endif
		    buf->pos = a->start.pos;
		    arrangeCursor(buf);
		}
	    }
	    lictx->refresh = 0;
	}
    }

    js_eval(interp, "document;");
    js_result(interp, &value);
    if (js_isa(interp, &value, js_lookup_class(interp, "Document"),
	(void **)&dictx) && dictx)
	return dictx->write;
    return NULL;
}

static Str
script_js_eval(Buffer *buf, char *script)
{
    JSInterpPtr interp;

    if (buf && buf->script_interp) {
	interp = (JSInterpPtr)buf->script_interp;
    } else {
	interp = js_html_init();
	if (buf)
	    buf->script_interp = (void *)interp;
    }
    script_buf2js(buf, interp);
    js_eval(interp, script);
    return script_js2buf(buf, interp);
}

static void
script_js_close(Buffer *buf)
{
    if (buf && buf->script_interp) {
	js_destroy_interp((JSInterpPtr)buf->script_interp);
	buf->script_interp = NULL;
    }
}
#endif

Str
script_eval(Buffer *buf, char *lang, char *script)
{
    if (buf && buf->location)
	buf->location = NULL;
    if (! lang || ! script)
	return NULL;
    if (buf) {
	if (buf->script_lang) {
	    if (strcasecmp(lang, buf->script_lang))
		return NULL;
	} else
	    buf->script_lang = lang;
    }
#ifdef USE_JAVASCRIPT
    if (! strcasecmp(lang, "javascript") ||
	! strcasecmp(lang, "jscript"))
	return script_js_eval(buf, script);
    else
#endif
	return NULL;
}

void
script_close(Buffer *buf)
{
    if (! buf || ! buf->script_lang)
	return;
#ifdef USE_JAVASCRIPT
    if (! strcasecmp(buf->script_lang, "javascript") ||
	! strcasecmp(buf->script_lang, "jscript"))
	script_js_close(buf);
#endif
    buf->script_interp = NULL;
    buf->script_lang = NULL;
}
#endif
