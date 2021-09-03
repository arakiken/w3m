#include "fm.h"
#ifdef USE_SCRIPT
#ifdef USE_JAVASCRIPT
#include "js_html.h"

#include <ctype.h> /* tolower */

static char *
chop_last_modified(Buffer *buf) {
    char *lm = last_modified(buf);
    size_t len = strlen(lm);

    if (len > 0 && lm[len - 1] == '\n') {
	lm[len - 1] = '\0';
    }

    return lm;
}

static char *
remove_quotation(char *str) {
    char *p1 = str;
    char *p2 = str;

    if (*p1 == '\0') {
	return str;
    }

    do {
	if (*p1 != '\"' && *p1 != '\'') {
	    *(p2++) = *p1;
	}
    } while (*(++p1));

    *p2 = '\0';

    return str;
}

static void
put_form_element(void *interp, int i, int j, FormItemList *fi)
{
    char *id, *n, *t, *v;

    js_eval(interp, Sprintf("document.forms[%d].elements[%d] = new FormItem();", i, j)->ptr);

    id = n = v = "";
    if (fi->id && fi->id->length > 0)
	id = fi->id->ptr;
    if (fi->name && fi->name->length > 0)
	n = fi->name->ptr;
    if (fi->value && fi->value->length > 0)
	v = fi->value->ptr;

    switch (fi->type) { /* TODO: ... */
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

    js_eval(interp, Sprintf("document.forms[%d].elements[%d].id = \"%s\";", i, j, id)->ptr);
    js_eval(interp, Sprintf("document.forms[%d].elements[%d].name = \"%s\";", i, j, n)->ptr);
    js_eval(interp, Sprintf("document.forms[%d].elements[%d].type = \"%s\";", i, j, t)->ptr);
    js_eval(interp, Sprintf("document.forms[%d].elements[%d].value = \"%s\";", i, j, v)->ptr);
    if (fi->name && fi->name->length > 0)
	js_eval(interp, Sprintf("document.forms[%d].%s = document.forms[%d].elements[%d];", i, n, i, j)->ptr);
    if (fi->id && fi->id->length > 0)
	js_eval(interp, Sprintf("document.forms[%d].%s = document.forms[%d].elements[%d];", i, id, i, j)->ptr);
}

static void
script_buf2js(Buffer *buf, void *interp)
{
    int i;

    if (!js_is_object(js_eval(interp, "window;"))) {
	js_eval(interp, "var window = new Window();");
    }
    if (!js_is_object(js_eval(interp, "navigator;"))) {
	js_eval(interp, "var navigator = new Navigator();");
    }
    js_eval(interp, "var document = new Document();");
    js_eval(interp, "var history = new History();");
    if (buf != NULL) {
	js_eval(interp, Sprintf("var location = new Location(\"%s\");",
				   parsedURL2Str(&buf->currentURL)->ptr)->ptr);
    } else {
	js_eval(interp, "var location = new Location();");
    }

    js_eval(interp, "document.anchors = new Array();");
    js_eval(interp, "document.forms = new Array();");

    i = 0;
    if (CurrentTab != NULL) {
	Buffer *tb;

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
	js_eval(interp, Sprintf("document.lastModified = \"%s\";", chop_last_modified(buf))->ptr);
	c = t = "";
	if (buf->buffername != NULL)
	    t = buf->buffername;
#ifdef USE_COOKIE
	cs = find_cookie(&buf->currentURL);
	if (cs) c = cs->ptr;
#endif

	js_eval(interp, Sprintf("document.title = \"%s\";", t)->ptr);
	js_eval(interp, Sprintf("document.cookie = \"%s\";", remove_quotation(c))->ptr);

	if (buf->name != NULL) {
	    AnchorList *aln, *alh;
	    Anchor *an, *ah;

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
	    FormList *fl;

	    for (i = 0, fl = buf->formlist; fl != NULL; i++, fl = fl->next) {
		char *m, *a, *e, *n, *t, *id;
		FormItemList *fi;
		int j;

		js_eval(interp, Sprintf("document.forms[%d] = new Form();", i)->ptr);
		if (fl->method == FORM_METHOD_POST) {
		    m = "post";
		} else if (fl->method == FORM_METHOD_INTERNAL) {
		    m = "internal";
		} else {
		    m = "get";
		}
		a = e = n = t = id = "";
		if (fl->enctype == FORM_ENCTYPE_MULTIPART) {
		    e = "multipart/form-data";
		}
		if (fl->action && fl->action->length > 0)
		    a = fl->action->ptr;
		if (fl->name != NULL)
		    n = fl->name;
		if (fl->id != NULL)
		    id = fl->id;
		if (fl->target != NULL)
		    t = fl->target;

		js_eval(interp, Sprintf("document.forms[%d].method = \"%s\";", i, m)->ptr);
		js_eval(interp, Sprintf("document.forms[%d].action = \"%s\";", i, a)->ptr);
		js_eval(interp, Sprintf("document.forms[%d].encoding = \"%s\";", i, e)->ptr);
		js_eval(interp, Sprintf("document.forms[%d].name = \"%s\";", i, n)->ptr);
		js_eval(interp, Sprintf("document.forms[%d].id = \"%s\";", i, id)->ptr);
		if (fl->name != NULL) {
		    js_eval(interp, Sprintf("document.%s = document.forms[%d];", n, i)->ptr);
/*		    js_eval(interp, Sprintf("document.forms[\"%s\"] = document.forms[%d];", n, i)->ptr);*/
		}
		js_eval(interp, Sprintf("document.forms[%d].target = \"%s\";", i, t)->ptr);
		js_eval(interp, Sprintf("document.forms[%d].elements = new Array();", i, fl->nitems)->ptr);

		for (j = 0, fi = fl->item; fi != NULL; j++, fi = fi->next) {
		    put_form_element(interp, i, j, fi);
		}
	    }
	}
    }

    js_eval(interp,
	       "document.getElementById = function(id) {"
	       "  for (let i = 0; i < document.forms.length; i++) {"
	       "    if (document.forms[i].id === id) {"
	       "      return document.forms[i];"
	       "    }"
	       "    for (let j = 0; j < document.forms[i].elements.length; i++) {"
	       "      if (document.forms[i].elements[j].id === id) {"
	       "        return document.forms[i].elements[j];"
	       "      }"
	       "    }"
	       "  }"
	       "  return null;"
	       "};");
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

static void
get_form_element(void *interp, int i, int j, FormItemList *fi)
{
    JSValue value;
    Str str;
    char *cstr;

    value = js_eval(interp, Sprintf("document.forms[%d].elements[%d].checked;", i, j)->ptr);
    cstr = js_get_cstr(interp, value);
    if (cstr != NULL) {
	fi->checked = (strcmp(cstr, "true") == 0);
    }

    value = js_eval(interp, Sprintf("document.forms[%d].elements[%d].value;", i, j)->ptr);
    str = js_get_str(interp, value);
    if (str != NULL) {
	fi->value = str;
    }
}

static Str
script_js2buf(Buffer *buf, void *interp)
{
    JSValue value;
    char *str;

    value = js_eval(interp, "window;");
    if (js_is_object(value)) {
	WindowState *state = js_get_state(value, WindowClassID);
	if (state->win && state->win->nitem) {
	    jse_windowopen_t *w;
	    while ((w = popValue(state->win)) != NULL) {
		if (enable_js_windowopen) {
		    jWindowOpen(buf, w->url, w->name);
		}
	    }
	}
	if (state->close) {
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
	    state->close = FALSE;
	}
    }

    if (CurrentTab != NULL && Currentbuf != NULL) {
	value = js_eval(interp, "history;");
	if (js_is_object(value)) {
	    HistoryState *state = js_get_state(value, HistoryClassID);

	    if (state->pos != 0) {
		script_chBuf(state->pos);
		state->pos = 0;
		return NULL;
	    }
	}
    }

    value = js_eval(interp, "document.location;");
    str = js_get_cstr(interp, value);
    if (str != NULL) {
	buf->location = str;
    }

    value = js_eval(interp, "location;");
    if (js_is_object(value)) {
	LocationState *state = js_get_state(value, LocationClassID);
	if (buf && state->url && state->refresh) {
	    if (state->refresh & JS_LOC_REFRESH) {
		buf->location = allocStr(state->url->ptr, state->url->length);
	    } else if (state->refresh & JS_LOC_HASH) {
		Anchor *a;
#ifdef JP_CHARSET
		a = searchURLLabel(buf,
				   conv(state->pu.label, buf->document_code,
					InnerCode)->ptr);
#else				/* not JP_CHARSET */
		a = searchURLLabel(buf, state->pu.label);
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
	    state->refresh = 0;
	}
    }

    if (buf->formlist != NULL) {
	int i;
	FormList *fl;

	for (i = 0, fl = buf->formlist; fl != NULL; i++, fl = fl->next) {
	    value = js_eval(interp, Sprintf("document.forms[%d];", i)->ptr);
	    if (js_is_object(value)) {
		FormState *state = js_get_state(value, FormClassID);
		char *cstr;
		Str str;
		int j;
		FormItemList *fi;

		value = js_eval(interp, Sprintf("document.forms[%d].method;", i)->ptr);
		cstr = js_get_cstr(interp, value);
		if (cstr != NULL) {
		    if (strcasecmp(cstr, "post") == 0) {
			fl->method = FORM_METHOD_POST;
		    } else if (strcasecmp(cstr, "internal") == 0) {
			fl->method = FORM_METHOD_INTERNAL;
		    } else /* if (strcasecmp(cstr, "get") == 0) */ {
			fl->method = FORM_METHOD_GET;
		    }
		}

		value = js_eval(interp, Sprintf("document.forms[%d].action;", i)->ptr);
		str = js_get_str(interp, value);
		if (str != NULL) {
		    fl->action = str;
		}

		value = js_eval(interp, Sprintf("document.forms[%d].target;", i)->ptr);
		cstr = js_get_cstr(interp, value);
		if (cstr != NULL) {
		    fl->target = cstr;
		}

		value = js_eval(interp, Sprintf("document.forms[%d].name;", i)->ptr);
		cstr = js_get_cstr(interp, value);
		if (cstr != NULL) {
		    fl->name = cstr;
		}

		value = js_eval(interp, Sprintf("document.forms[%d].id;", i)->ptr);
		cstr = js_get_cstr(interp, value);
		if (cstr != NULL) {
		    fl->id = cstr;
		}

		for (j = 0, fi = fl->item; fi != NULL; j++, fi = fi->next) {
		    get_form_element(interp, i, j, fi);
		}

		if (state->submit) {
		    followForm(fl);
		    state->submit = 0;
		}

		if (state->reset) {
		    state->reset = 0;
		}
	    }
	}
    }

    value = js_eval(interp, "document;");
    if (js_is_object(value)) {
	DocumentState *state = js_get_state(value, DocumentClassID);

	if (state->write) {
	    return state->write;
	}
    }

    return NULL;
}

static Str
script_js_eval(Buffer *buf, char *script)
{
    void *interp;

    if (buf == NULL) {
	return NULL;
    }

    if (buf->script_interp) {
	interp = buf->script_interp;
    } else {
	interp = js_html_init();
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
	js_html_final(buf->script_interp);
	buf->script_interp = NULL;
    }
}
#endif

Str
script_eval(Buffer *buf, char *lang, char *script)
{
    if (buf == NULL) {
	return NULL;
    }

    if (buf->location)
	buf->location = NULL;
    if (! lang || ! script)
	return NULL;
    if (buf->script_lang) {
	if (strcasecmp(lang, buf->script_lang))
	    return NULL;
    } else
	buf->script_lang = lang;

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
