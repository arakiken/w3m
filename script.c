#include "fm.h"
#ifdef USE_SCRIPT
#ifdef USE_JAVASCRIPT
#include "js_html.h"

#include <ctype.h> /* tolower */

#if 1
#define SET_FORM_TO_OPAQUE
#endif

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
remove_quotation(char *str)
{
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

static Str
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

#if 0
static char *
escape(char *name)
{
    Str str = NULL;
    char *p = name;
    size_t nskip = 0;

    while (*p) {
	if (*p == ':' || *p == '.' || *p == '[' || *p == ']' || *p == ',' ||
	    *p == '=' || *p == '?') {
	    if (str == NULL) {
		str = Strnew_charp_n(name, nskip);
	    } else {
		Strcat_charp_n(str, p - nskip, nskip);
	    }
	    Strcat_charp(str, "\\\\");
	    Strcat_char(str, *p);
	    nskip = 0;
	} else {
	    nskip++;
	}
	p++;
    }

    if (str) {
	if (nskip > 0) {
	    Strcat_charp_n(str, p - nskip, nskip);
	}

	return str->ptr;
    } else {
	return name;
    }
}
#else
static int
check_property_name(char *name)
{
    while (*name) {
	if (*name == ':' || *name == '.' || *name == '[' || *name == ']' || *name == ',' ||
	    *name == '=' || *name == '?') {
	    return 0;
	}
	name++;
    }

    return 1;
}
#endif

/*
 * document -> documentElement(<html>) -> children == childNodes -> ...
 *          -> head                                |
 *          -> body      +-------------------------+
 *                       |
 *          -> children == childNodes -> head, body -> forms, orphan elements
 *          -> forms -> elements == children == childNodes -> options == children == childNodes
 *                      (see init_children() in js_html.c)
 */

static void
put_select_option(void *interp, int i, int j, int k, FormSelectOptionItem *opt)
{
    char *ov, *ol;

    ov = ol = "";
    if (opt->value)
	ov = i2us(opt->value)->ptr;
    if (opt->label)
	ol = i2us(opt->label)->ptr;

    js_eval(interp, Sprintf("document.forms[%d].elements[%d].appendChild(new HTMLElement(\"option\"));", i, j)->ptr);
    js_eval(interp, Sprintf("document.forms[%d].elements[%d].options[%d].value = \"%s\";", i, j, k, ov)->ptr);
    js_eval(interp, Sprintf("document.forms[%d].elements[%d].options[%d].text = \"%s\";", i, j, k, ol)->ptr);
    js_eval(interp, Sprintf("document.forms[%d].elements[%d].options[%d].selected = %s;", i, j, k, opt->checked ? "true" : "false")->ptr);

    /* http://www.shurey.com/js/samples/6_smp7.html */
    js_eval(interp, Sprintf("document.forms[%d].elements[%d][\"%d\"] = document.forms[%d].elements[%d].options[%d];", i, j, k, i, j, k)->ptr);

#ifdef SET_FORM_OPAQUE
    {
	JSValue o = js_eval2(interp, Sprintf("document.forms[%d].elements[%d].options[%d];", i, j, k)->ptr);
	js_set_state(o, opt);
	js_free(interp, o);
    }
#endif
}

static void
put_form_element(void *interp, int i, int j, FormItemList *fi)
{
    char *id, *n, *t, *v;
    int k;
    FormSelectOptionItem *opt;
    ListItem *item;

    if (fi->type == FORM_SELECT) {
	t = "select";
	js_eval(interp, Sprintf("document.forms[%d].appendChild(new HTMLSelectElement());", i)->ptr);
    } else {
	if (fi->type == FORM_TEXTAREA) {
	    t = "textarea";
	} else {
	    t = "input";
	}
	js_eval(interp, Sprintf("document.forms[%d].appendChild(new HTMLElement(\"%s\"));", i, t)->ptr);
    }

    id = n = v = "";
    if (fi->id && fi->id->length > 0)
	id = fi->id->ptr;
    if (fi->name && fi->name->length > 0)
	n = i2us(fi->name)->ptr;
    if (fi->value && fi->value->length > 0)
	v = escape_value(i2us(fi->value))->ptr;

    switch (fi->type) { /* TODO: ... */
    case FORM_INPUT_TEXT:
	t = "text";
	if (fi->init_value->length > 0)
	    js_eval(interp, Sprintf("document.forms[%d].elements[%d].defaultValue = \"%s\";", i, j, escape_value(i2us(fi->init_value))->ptr)->ptr);
	break;
    case FORM_INPUT_PASSWORD:
	t = "password";
	if (fi->init_value->length > 0)
	    js_eval(interp, Sprintf("document.forms[%d].elements[%d].defaultValue = \"%s\";", i, j, escape_value(i2us(fi->init_value))->ptr)->ptr);
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
#ifdef MENU_SELECT
	js_eval(interp, Sprintf("document.forms[%d].elements[%d].selectedIndex = %d;", i, j, fi->selected)->ptr);
	for (k = 0, opt = fi->select_option; opt != NULL; k++, opt = opt->next) {
	    put_select_option(interp, i, j, k, opt);
	}
#endif
	break;
    case FORM_TEXTAREA:
	if (fi->init_value->length > 0)
	    js_eval(interp, Sprintf("document.forms[%d].elements[%d].defaultValue = \"%s\";", i, j, escape_value(i2us(fi->init_value))->ptr)->ptr);
	break;
    case FORM_INPUT_BUTTON:
	t = "button"; break;
    case FORM_INPUT_FILE:
	t = "file";
	if (fi->init_value && fi->init_value->length > 0) /* TODO: Read Only */
	    js_eval(interp, Sprintf("document.forms[%d].elements[%d].defaultValue = \"%s\";", i, j, escape_value(i2us(fi->init_value))->ptr)->ptr);
	break;
    default:
	t = ""; break;
    }

    js_eval(interp, Sprintf("document.forms[%d].elements[%d].id = \"%s\";", i, j, id)->ptr);
    js_eval(interp, Sprintf("document.forms[%d].elements[%d].name = \"%s\";", i, j, n)->ptr);
    js_eval(interp, Sprintf("document.forms[%d].elements[%d].type = \"%s\";", i, j, t)->ptr);
    js_eval(interp, Sprintf("document.forms[%d].elements[%d].value = \"%s\";", i, j, v)->ptr);

    if (*n != '\0' && check_property_name(n)) {
	JSValue val = js_eval2(interp, Sprintf("document.forms[%d].%s;", i, n)->ptr);
	if (js_is_undefined(val)) {
	    js_eval(interp, Sprintf("document.forms[%d].%s = document.forms[%d].elements[%d];", i, n, i, j)->ptr);
	} else {
	    int len;
	    JSValue val2 = js_eval2(interp, Sprintf("document.forms[%d].%s.length;", i, n)->ptr);
	    if (!js_get_int(interp, &len, val2)) {
		js_eval(interp, Sprintf("document.forms[%d].%s = [document.forms[%d].%s];",
					i, n, i, n)->ptr);
		len = 1;
	    }

	    js_eval(interp, Sprintf("document.forms[%d].%s[%d] = document.forms[%d].elements[%d];", i, n, len, i, j)->ptr);
	}
	js_free(interp, val);
    }

    if (fi->onkeyup) {
	for (item = fi->onkeyup->first; item; item = item->next) {
	    JSValue val = js_eval2(interp, Sprintf("document.forms[%d].elements[%d];", i, j)->ptr);
	    js_add_event_listener(interp, val, "keyup", ((Str)item->ptr)->ptr);
	}
    }
    if (fi->onclick) {
	for (item = fi->onclick->first; item; item = item->next) {
	    JSValue val = js_eval2(interp, Sprintf("document.forms[%d].elements[%d];", i, j)->ptr);
	    js_add_event_listener(interp, val, "click", ((Str)item->ptr)->ptr);
	}
    }
    if (fi->onchange) {
	for (item = fi->onchange->first; item; item = item->next) {
	    JSValue val = js_eval2(interp, Sprintf("document.forms[%d].elements[%d];", i, j)->ptr);
	    js_add_event_listener(interp, val, "change", ((Str)item->ptr)->ptr);
	}
    }

#ifdef SET_FORM_OPAQUE
    {
	JSValue e = js_eval2(interp, Sprintf("document.forms[%d].elements[%d];", i, j)->ptr);
	js_set_state(e, fi);
	js_free(interp, e);
    }
#endif
}

static void
update_forms(Buffer *buf, void *interp)
{
    int i;

    js_eval(interp,
	    "if (document.forms) {"
	    "  for (let i = 0; i < document.forms.length; i++) {"
	    "    document.body.removeChild(document.forms[i]);"
	    "  }"
	    "}"
	    "document.forms = new HTMLCollection();");

    /* For document.children.length in script.c */
    if (buf->formlist != NULL) {
	FormList *fl;

	for (i = 0, fl = buf->formlist; fl != NULL; i++, fl = fl->next) {
	    char *m, *a, *e, *n, *t, *id;
	    FormItemList *fi;
	    int j;
	    ListItem *item;

	    js_eval(interp, Sprintf("document.forms[%d] = new HTMLFormElement();", i)->ptr);
	    js_eval(interp, Sprintf("document.body.appendChild(document.forms[%d]);", i)->ptr);
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
		n = i2uc(fl->name);
	    if (fl->id != NULL)
		id = fl->id;
	    if (fl->target != NULL)
		t = fl->target;

	    js_eval(interp, Sprintf("document.forms[%d].method = \"%s\";", i, m)->ptr);
	    js_eval(interp, Sprintf("document.forms[%d].action = \"%s\";", i, a)->ptr);
	    js_eval(interp, Sprintf("document.forms[%d].encoding = \"%s\";", i, e)->ptr);
	    js_eval(interp, Sprintf("document.forms[%d].name = \"%s\";", i, n)->ptr);
	    js_eval(interp, Sprintf("document.forms[%d].id = \"%s\";", i, id)->ptr);
	    /* fl->name might be '\0' (see process_input() in file.c) */
	    if (*n != '\0' && check_property_name(n)) {
		js_eval(interp, Sprintf("document.%s = document.forms[%d];", n, i)->ptr);
		/* js_eval(interp, Sprintf("document.forms[\"%s\"] = document.forms[%d];", n, i)->ptr);*/
	    }
	    js_eval(interp, Sprintf("document.forms[%d].target = \"%s\";", i, t)->ptr);

	    for (j = 0, fi = fl->item; fi != NULL; j++, fi = fi->next) {
		put_form_element(interp, i, j, fi);
	    }

	    /* http://alphasis.info/2013/12/javascript-gyakubiki-form-immediatelyreflect/ */
	    js_eval(interp, Sprintf("document.forms[%d].length = document.forms[%d].elements.length;", i, i)->ptr);

	    if (fl->onsubmit) {
		for (item = fl->onsubmit->first; item; item = item->next) {
		    JSValue val = js_eval2(interp, Sprintf("document.forms[%d];", i)->ptr);
		    js_add_event_listener(interp, val, "submit", ((Str)item->ptr)->ptr);
		}
	    }
	    if (fl->onreset) {
		for (item = fl->onreset->first; item; item = item->next) {
		    JSValue val = js_eval2(interp, Sprintf("document.forms[%d];", i)->ptr);
		    js_add_event_listener(interp, val, "reset", ((Str)item->ptr)->ptr);
		}
	    }
	}
    }
}

static void
script_buf2js(Buffer *buf, void *interp)
{
    JSValue ret;
    int i;
    char *t, *c;
#ifdef USE_COOKIE
    Str cs;
#endif

    ret = js_eval2(interp, "document;");
    if (js_is_exception(ret)) {
	js_eval(interp,
		"var self = globalThis;"
		"var window = globalThis;"
		"window.parent = window;"
		"window.top = window;"
		"window.window = window;"
		""
		"function w3m_initDocumentTree(doc) {"
		"  doc.documentElement = new HTMLElement(\"html\");"
		"  doc.children = doc.documentElement.children;"
		"  doc.childNodes = doc.children; /* XXX NodeList */"
		"  doc.head = new HTMLElement(\"head\");"
		"  doc.body = new HTMLElement(\"body\");"
		"  doc.documentElement.appendChild(doc.head);"
		"  doc.documentElement.appendChild(doc.body);"
		"}"
		""
		"var document = new Document();"
		"w3m_initDocumentTree(document);"
		"document.nodeName = \"#document\";"
		"document.compatMode = \"CSS1Compat\";"
		"document.nodeType = 9; /* DOCUMENT_NODE */"
		"document.referrer = \"\";"
		"document.readyState = \"complete\";"
		"document.visibilityState = \"visible\";" /* XXX */
		"document.activeElement = null;"
		"document.defaultView = window;"
		""
		"var screen = new Object();"
		"var navigator = new Navigator();"
		"var history = new History();"
		"var performance = new Object();"
		"var localStorage = new Object();"
		"var sessionStorage = new Object();"
		""
		"document.createDocumentFragment = function() {"
		"  let doc = Object.assign(new Document(), document);"
		"  w3m_initDocumentTree(doc);"
		"  doc.nodeName = \"#document-fragment\";"
		"  doc.nodeType = 11; /* DOCUMENT_FRAGMENT_NODE */"
		"  return doc;"
		"};"
		""
		"document.implementation = new Object();"
		"document.implementation.createHTMLDocument = function() {"
		"  let doc = Object.assign(new Document(), document);"
		"  w3m_initDocumentTree(doc);"
		"  return doc;"
		"};"
		""
	        "document.createElement = function(tagname) {"
		"  tagname = tagname.toLowerCase();"
		"  if (tagname === \"form\") {"
		"    return new HTMLFormElement();"
		"  } else if (tagname === \"img\") {"
		"    return new HTMLImageElement();"
		"  } else if (tagname === \"select\") {"
		"    return new HTMLSelectElement();"
		"  } else if (tagname === \"script\") {"
		"    return new HTMLScriptElement();"
		"  } else {"
		"    return new HTMLElement(tagname);"
		"  }"
		"};"
		""
		"/* see element_text_content_set() in js_html.c */"
	        "document.createTextNode = function(text) {"
		"  let element = new HTMLElement(\"#text\");"
		"  element.nodeValue = text;"
		"  return element;"
		"};"
		""
	        "document.createComment = function(data) {"
		"  let element = new HTMLElement(\"#comment\");"
		"  element.nodeValue = data;"
		"  return element;"
		"};"
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
		"document.getElementById = function(id) {"
		"  let hit = w3m_getElementById(document, id);"
#ifdef USE_LIBXML2
		"  return hit;"
#else
		"  if (hit != null) {"
		"    return hit;"
		"  }"
		"  let element = new HTMLElement(\"span\");"
		"  element.id = id;"
		"  element.value = \"\";"
		"  return document.body.appendChild(element);"
#endif
		"};"
		""
		"function w3m_getElementsByTagName(element, name, elements) {"
		"  for (let i = 0; i < element.children.length; i++) {"
		"    if (name === \"*\" ? element.children[i].nodeType == 1 :"
		"                         element.children[i].tagName.toLowerCase() === name) {"
		"      elements.push(element.children[i]);"
		"    }"
		"    w3m_getElementsByTagName(element.children[i], name, elements);"
		"  }"
		"}"
		""
		"document.getElementsByTagName = function(name) {"
		"  name = name.toLowerCase();"
		"  if (name === \"form\") {"
		"    return document.forms;"
		"  } else {"
		"    let elements = new HTMLCollection();"
		"    if (name === \"html\") {"
		"      elements.push(document.documentElement);"
		"    } else {"
		"      w3m_getElementsByTagName(document, name, elements);"
#ifndef USE_LIBXML2
		"      if (elements.length == 0) {"
		"        if (name === \"*\") {"
		"          name = \"span\";"
		"        }"
		"        let element = new HTMLElement(name);"
		"        element.value = \"\";"
		"        document.body.appendChild(element);"
		"        elements.push(element);"
		"      }"
#endif
		"    }"
		"    return elements;"
		"  }"
		"};"
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
		"document.getElementsByName = function(name) {"
		"  let elements = new HTMLCollection();"
		"  w3m_getElementsByName(document, name, elements);"
#ifndef USE_LIBXML2
		"  if (elements.length == 0) {"
		"    let element = new HTMLElement(\"span\");"
		"    element.name = name;"
		"    element.value = \"\";"
		"    document.body.appendChild(element);"
		"    elements.push(element);"
		"  }"
#endif
		"  return elements;"
		"};"
		""
		"function w3m_getElementsByClassName(element, name, elements) {"
		"  for (let i = 0; i < element.children.length; i++) {"
		"    if (element.children[i].className === name) {"
		"      elements.push(element.children[i]);"
		"    }"
		"    w3m_getElementsByClassName(element.children[i], name, elements);"
		"  }"
		"}"
		""
		"document.getElementsByClassName = function(name) {"
		"  let elements = new HTMLCollection();"
		"  w3m_getElementsByClassName(document, name, elements);"
#ifndef USE_LIBXML2
		"  if (elements.length == 0) {"
		"    let element = new HTMLElement(\"span\");"
		"    element.className = name;"
		"    element.value = \"\";"
		"    document.body.appendChild(element);"
		"    elements.push(element);"
		"  }"
#endif
		"  return elements;"
		"};"
		""
		"function w3m_parseSelector(sel) {"
		"  let array = new Array(3);"
		"  array[0] = array[1] = array[2] = null;"
		"  let tmp = sel.match(/^[^,\\[\\] >]+/);"
		"  if (tmp == null) {"
		"    return array;"
		"  }"
		"  sel = tmp[0];"
		"  tmp = sel.match(/^[^.#]+/); /* tag */"
		"  if (tmp != null) {"
		"    array[0] = tmp[0];"
		"  }"
		"  tmp = sel.match(/\\.[^.#]+/); /* class */"
		"  if (tmp != null) {"
		"    array[1] = tmp[0];"
		"  }"
		"  tmp = sel.match(/#[^.#]+/); /* id */"
		"  if (array[2] != null) {"
		"    array[2] = tmp[0];"
		"  }"
		"  return array;"
		"}"
		""
		"document.querySelectorAll = function(sel) {"
		"  let array = w3m_parseSelector(sel);"
		"  if (array[0] != null) {"
		"    return document.getElementsByTagName(array[0]);"
		"  } else if (array[1] != null) {"
		"    return document.getElementsByClassName(array[1].substring(1));"
		"  } else {"
		"    let elements = new NodeList();"
#ifdef USE_LIBXML2
		"    if (array[2] != null) {"
		"      let element = document.getElementById(array[2].substring(1));"
		"      if (element != null) {"
		"        elements.push(element);"
		"      }"
		"    }"
#else
		"    if (array[2] != null) {"
		"      elements.push(document.getElementById(array[2].substring(1)));"
		"    } else {"
		"      elements.push(document.body.appendChild(new HTMLElement(\"span\")));"
		"    }"
#endif
		"    return elements;"
		"  }"
		"};"
		""
		"document.querySelector = function(sel) {"
		"  let array = w3m_parseSelector(sel);"
		"  let element;"
		"  if (array[0] != null) {"
		"    element = document.getElementsByTagName(array[0])[0];"
		"  } else if (array[1] != null) {"
		"    element = document.getElementsByClassName(array[1])[0];"
		"  } else if (array[2] != null) {"
		"    element = document.getElementById(array[2]);"
		"  } else {"
#ifdef USE_LIBXML2
		"    return null;"
#else
		"    element = document.body.appendChild(new HTMLElement(\"span\"));"
		"    element.value = \"\";"
#endif
		"  }"
		"  return element;"
		"};"
		""
		"document.createEvent = function() {"
		"  return new Object();"
		"};"
		""
		"window.requestAnimationFrame = function(callback) {"
		"  return null;"
		"};"
		""
		"window.cancelAnimationFrame = function(callback) {"
		"  return null;"
		"};"
		""
		"window.setInterval = window.setTimeout = function(fn, tm) {"
		"  if (typeof fn == \"string\") {"
		"    document.addEventListener(\"DOMContentLoaded\", new Function(fn));"
		"  } else if (typeof fn == \"function\") {"
		"    document.addEventListener(\"DOMContentLoaded\", fn);"
		"  }"
		"};"
		""
		"window.clearTimeout = function(id) { ; };"
		""
		"performance.now = function() {"
		"  /* performance.now() should return DOMHighResTimeStamp */"
		"  return Date.now();"
		"};"
		""
		"sessionStorage.getItem = localStorage.getItem = function(key) {"
		"  return null;"
		"};"
		""
		"sessionStorage.setItem = localStorage.setItem = function(key, value) { ; };"
		""
		"sessionStorage.removeItem = localStorage.removeItem = function(key) { ; };"
		""
		"function Image() {"
		"  return new HTMLImageElement();"
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
		"}");
	js_eval(interp, Sprintf("document.characterSet = \"%s\";",
				wc_ces_to_charset(buf->document_charset))->ptr);
	if (buf->buffername != NULL) {
	    js_eval(interp, Sprintf("document.title = \"%s\";", i2uc(buf->buffername))->ptr);
	}
    } else {
	js_eval(interp, "w3m_initDocumentTree(document);");
    }
    js_free(interp, ret);

    js_eval(interp, Sprintf("screen.width = screen.availWidth = document.body.clientWidth = document.documentElement.clientWidth = window.innerWidth = window.outerWidth = %d;", buf->COLS * term_ppc)->ptr);
    js_eval(interp, Sprintf("screen.height = screen.availHeight = document.body.clientHeight = document.documentElement.clientHeight = window.innerHeight = window.outerHeight = %d;", buf->LINES * term_ppl)->ptr);
    js_eval(interp, "window.scrollX = window.scrollY = document.body.scrollLeft = document.body.scrollTop = document.documentElement.scrollLeft = document.documentElement.scrollTop = 0;");

    js_eval(interp, Sprintf("var location = new Location(\"%s\");",
			    parsedURL2Str(&buf->currentURL)->ptr)->ptr);
    js_eval(interp, "window.location = location;");

    i = 0;
    if (CurrentTab != NULL) {
	Buffer *tb;

	for (tb = Firstbuf; tb != NULL; i++, tb = tb->nextBuffer);
    }
    js_eval(interp, Sprintf("history.length = %d;", i)->ptr);

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

    js_eval(interp, Sprintf("document.title = \"%s\";", i2uc(t))->ptr);
    js_eval(interp, Sprintf("document.cookie = \"%s\";", remove_quotation(c))->ptr);

    js_eval(interp, "document.scripts = new HTMLCollection();");
#ifndef USE_LIBXML2
    if (buf->scripts != NULL) {
	ListItem *l;

	for (l = buf->scripts->first; l != NULL; l = l->next) {
	    js_eval(interp,
		    "{"
		    "  let element = new HTMLScriptElement();"
		    "  document.scripts.push(element);"
		    "  document.body.appendChild(element);"
		    "}");
	}
    }
#endif

    update_forms(buf, interp);
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

static int
get_select_option(void *interp, int i, int j, int k, FormItemList *fi, FormSelectOptionItem *opt)
{
    int changed = 0;
    JSValue val2 = js_eval2(interp, Sprintf("document.forms[%d].elements[%d].options[%d].selected;", i, j, k)->ptr);
    int flag = js_is_true(interp, val2);

    if (flag != -1) {
	if (flag != opt->checked) {
	    if (flag) {
		FormSelectOptionItem *o;
		int idx = 0;

		for (o = fi->select_option; o != NULL; o = o->next, idx++) {
		    if (o == opt) {
			fi->selected = idx;
			opt->checked = TRUE;
		    } else {
			o->checked = 0;
		    }
		}
	    } else {
		opt->checked = FALSE;
		fi->selected = 0;
		fi->select_option->checked = 1;
	    }
	    changed = 1;
	}
    }

    return changed;
}

static Str
get_form_element_event(void *interp, int i, int j, const char *type)
{
    char *script =
	Sprintf("if (document.forms[%d].elements[%d].myevents) {"
		"  let listener = undefined;"
		"  for (let i = 0; i < document.forms[%d].elements[%d].myevents.length; i++) {"
		"    if (document.forms[%d].elements[%d].myevents[i].type === \"%s\") {"
		"      listener = document.forms[%d].elements[%d].myevents[i].listener;"
		"      document.forms[%d].elements[%d].myevents.splice(i, 1);"
		"      break;"
		"    }"
		"  }"
		"  if (typeof listener == \"object\" && listener.handleEvent) {"
		"    listener.handleEvent;"
		"  } else {"
		"    listener;"
		"  }"
		"} else {"
		"  undefined;"
		"}", i, j, i, j, i, j, type, i, j, i, j)->ptr;

    return js_get_function(interp, script);
}

static Str
get_form_event(void *interp, int i, const char *type)
{
    char *script =
	Sprintf("if (document.forms[%d].myevents) {"
		"  let listener = undefined;"
		"  for (let i = 0; i < document.forms[%d].myevents.length; i++) {"
		"    if (document.forms[%d].myevents[i].type === \"%s\") {"
		"      listener = document.forms[%d].myevents[i].listener;"
		"      document.forms[%d].myevents.splice(i, 1);"
		"      break;"
		"    }"
		"  }"
		"  if (typeof listener == \"object\" && listener.handleEvent) {"
		"    listener.handleEvent;"
		"  } else {"
		"    listener;"
		"  }"
		"} else {"
		"  undefined;"
		"}", i, i, i, type, i, i)->ptr;

    return js_get_function(interp, script);
}

/* XXX ? */
static Str
get_document_event(void *interp, const char *type)
{
    char *script =
	Sprintf("if (document.myevents) {"
		"  let listener = undefined;"
		"  for (let i = 0; i < document.myevents.length; i++) {"
		"    if (document.myevents[i].type === \"%s\") {"
		"      listener = document.myevents[i].listener;"
		"      document.myevents.splice(i, 1);"
		"      break;"
		"    }"
		"  }"
		"  if (typeof listener == \"object\" && listener.handleEvent) {"
		"    listener.handleEvent;"
		"  } else {"
		"    listener;"
		"  }"
		"} else {"
		"  undefined;"
		"}", type)->ptr;

    return js_get_function(interp, script);
}

static void
reset_func_list(GeneralList *list)
{
    if (list != NULL) {
	ListItem *item = list->last;
	while (item != NULL) {
	    if (strncmp(((Str)item->ptr)->ptr, "func_id:", 8) == 0) {
		delValue(list, item);
		item = list->last;
	    } else {
		item = item->prev;
	    }
	}
    }
}

static void
push_func(GeneralList **list, Str func)
{
    if (*list == NULL) {
	*list = newGeneralList();
    }

    pushValue(*list, func);
}

static int
get_form_element(Buffer *buf, void *interp, int i, int j, FormItemList *fi)
{
    JSValue val;
    int changed = 0;
    Str str;
    int flag;
    int new_idx;
    char *keyev[] = { "keypress", "keyup", "keydown" };
    int idx;

    val = js_eval2(interp, Sprintf("document.forms[%d].children[%d].value;", i, j)->ptr);
    str = js_get_str(interp, val);
    if (str != NULL) {
	str = u2is(str);
	if (Strcmp(str, escape_value(fi->value)) != 0) {
	    fi->value = str;
	    changed = 1;
	}
    }

    str = js_get_function(interp, Sprintf("document.forms[%d].elements[%d].onkeyup;", i, j)->ptr);
    reset_func_list(fi->onkeyup);
    if (str != NULL) {
	push_func(&fi->onkeyup, str);
    }
    for (idx = 0; idx < sizeof(keyev) / sizeof(keyev[0]); idx++) {
	while ((str = get_form_element_event(interp, i, j, keyev[idx])) != NULL) {
	    push_func(&fi->onkeyup, str);
	}
    }

    str = js_get_function(interp, Sprintf("document.forms[%d].elements[%d].onclick;", i, j)->ptr);
    reset_func_list(fi->onclick);
    if (str != NULL) {
	push_func(&fi->onclick, str);
    }
    while ((str = get_form_element_event(interp, i, j, "click")) != NULL) {
	push_func(&fi->onclick, str);
    }

    str = js_get_function(interp, Sprintf("document.forms[%d].elements[%d].onchange;", i, j)->ptr);
    reset_func_list(fi->onchange);
    if (str != NULL) {
	push_func(&fi->onchange, str);
    }
    while ((str = get_form_element_event(interp, i, j, "change")) != NULL) {
	push_func(&fi->onchange, str);
    }

    val = js_eval2(interp, Sprintf("document.forms[%d].elements[%d].checked;", i, j)->ptr);
    flag = js_is_true(interp, val);
    if (flag != -1) {
	if ((!flag || !trigger_click_event(buf, fi)) && flag != fi->checked) {
	    if (flag && fi->type == FORM_INPUT_RADIO) {
		FormList *fl = fi->parent;
		FormItemList *i;
		for (i = fl->item; i != NULL; i = i->next) {
		    if (i->type == FORM_INPUT_RADIO &&
			i->name && Strcmp(i->name, fi->name) == 0) {
			i->checked = 0;
		    }
		}
	    }

	    fi->checked = flag;
	    changed = 1;
	}
    }

    val = js_eval2(interp, Sprintf("document.forms[%d].elements[%d].selectedIndex;", i, j)->ptr);
    if (js_get_int(interp, &new_idx, val)) {
	int k;
	FormSelectOptionItem *opt;

	if (new_idx != fi->selected) {
	    for (k = 0, opt = fi->select_option; opt != NULL; k++, opt = opt->next) {
		if (new_idx != fi->selected) {
		    if (k == new_idx) {
			opt->checked = 1;
		    } else if (k == fi->selected) {
			opt->checked = 0;
		    }
		}
	    }
	    fi->selected = new_idx;
	    changed = 1;
	} else {
#ifdef SET_FORM_OPAQUE
	    for (k = 0; ; k++) {
		JSValue o = js_eval2(interp,
				     Sprintf("document.forms[%d].elements[%d].options[%d];", i, j, k)->ptr);
		if (js_is_object(o)) {
		    opt = js_get_state(o, HTMLElementClassID);
		    if (opt != NULL) {
			changed |= get_select_option(interp, i, j, k, fi, opt);
		    }
		    js_free(interp, o);
		} else {
		    js_free(interp, o);
		    break;
		}
	    }
#else
	    for (k = 0, opt = fi->select_option; opt != NULL; k++, opt = opt->next) {
		changed |= get_select_option(interp, i, j, k, fi, opt);
	    }
#endif
	}
    }

    return changed;
}

#ifdef USE_COOKIE
/* Same processing as readHeader() in file.c */
static void
set_cookie(char *p, ParsedURL *pu)
{
    extern char *violations[];
    char *q;
    char *emsg;
    Str name = Strnew(), value = Strnew(), domain = NULL, path = NULL,
	comment = NULL, commentURL = NULL, port = NULL, tmp2;
    int version, quoted, flag = 0;
    time_t expires = (time_t) - 1;

    SKIP_BLANKS(p);
    while (*p != '=' && !IS_ENDT(*p))
	Strcat_char(name, *(p++));
    Strremovetrailingspaces(name);
    if (*p == '=') {
	p++;
	SKIP_BLANKS(p);
	quoted = 0;
	while (!IS_ENDL(*p) && (quoted || *p != ';')) {
	    if (!IS_SPACE(*p))
		q = p;
	    if (*p == '"')
		quoted = (quoted) ? 0 : 1;
	    Strcat_char(value, *(p++));
	}
	if (q)
	    Strshrink(value, p - q - 1);
    }
    while (*p == ';') {
	p++;
	SKIP_BLANKS(p);
	if (matchattr(p, "expires", 7, &tmp2)) {
	    /* version 0 */
	    expires = mymktime(tmp2->ptr);
	}
	else if (matchattr(p, "max-age", 7, &tmp2)) {
	    /* XXX Is there any problem with max-age=0? (RFC 2109 ss. 4.2.1, 4.2.2 */
	    expires = time(NULL) + atol(tmp2->ptr);
	}
	else if (matchattr(p, "domain", 6, &tmp2)) {
	    domain = tmp2;
	}
	else if (matchattr(p, "path", 4, &tmp2)) {
	    path = tmp2;
	}
	else if (matchattr(p, "secure", 6, NULL)) {
	    flag |= COO_SECURE;
	}
	else if (matchattr(p, "comment", 7, &tmp2)) {
	    comment = tmp2;
	}
	else if (matchattr(p, "version", 7, &tmp2)) {
	    version = atoi(tmp2->ptr);
	}
	else if (matchattr(p, "port", 4, &tmp2)) {
	    /* version 1, Set-Cookie2 */
	    port = tmp2;
	}
	else if (matchattr(p, "commentURL", 10, &tmp2)) {
	    /* version 1, Set-Cookie2 */
	    commentURL = tmp2;
	}
	else if (matchattr(p, "discard", 7, NULL)) {
	    /* version 1, Set-Cookie2 */
	    flag |= COO_DISCARD;
	}
	quoted = 0;
	while (!IS_ENDL(*p) && (quoted || *p != ';')) {
	    if (*p == '"')
		quoted = (quoted) ? 0 : 1;
	    p++;
	}
    }
    if (pu && name->length > 0) {
	int err;
	if (show_cookie) {
	    if (flag & COO_SECURE)
		disp_message_nsec("Received a secured cookie", FALSE, 1,
				  TRUE, FALSE);
	    else
		disp_message_nsec(Sprintf("Received cookie: %s=%s",
					  name->ptr, value->ptr)->ptr,
				  FALSE, 1, TRUE, FALSE);
	}
	err =
	    add_cookie(pu, name, value, expires, domain, path, flag,
		       comment, version, port, commentURL);
	if (err) {
	    char *ans = (accept_bad_cookie == ACCEPT_BAD_COOKIE_ACCEPT)
		? "y" : NULL;
	    if (fmInitialized && (err & COO_OVERRIDE_OK) &&
		accept_bad_cookie == ACCEPT_BAD_COOKIE_ASK) {
		Str msg = Sprintf("Accept bad cookie from %s for %s?",
				  pu->host,
				  ((domain && domain->ptr)
				   ? domain->ptr : "<localdomain>"));
		if (msg->length > COLS - 10)
		    Strshrink(msg, msg->length - (COLS - 10));
		Strcat_charp(msg, " (y/n)");
		ans = inputAnswer(msg->ptr);
	    }
	    if (ans == NULL || TOLOWER(*ans) != 'y' ||
		(err =
		 add_cookie(pu, name, value, expires, domain, path,
			    flag | COO_OVERRIDE, comment, version,
			    port, commentURL))) {
		err = (err & ~COO_OVERRIDE_OK) - 1;
		if (err >= 0 && err < COO_EMAX) {
		    emsg = Sprintf("This cookie was rejected "
				   "to prevent security violation. [%s]",
				   violations[err])->ptr;
		}
		else
		    emsg =
			"This cookie was rejected to prevent security violation.";
		record_err_message(emsg);
		if (show_cookie)
		    disp_message_nsec(emsg, FALSE, 1, TRUE, FALSE);
	    }
	    else
		if (show_cookie)
		    disp_message_nsec(Sprintf
				      ("Accepting invalid cookie: %s=%s",
				       name->ptr, value->ptr)->ptr, FALSE,
				      1, TRUE, FALSE);
	}
    }
}
#endif

static Str
script_js2buf(Buffer *buf, void *interp)
{
    JSValue value;
    char *cstr;
    Str ret;

    value = js_eval2(interp, "window;");
    if (js_is_object(value)) {
	WindowState *state = js_get_state(value, WindowClassID);

	if (state->win && state->win->nitem) {
	    OpenWindow *w;
	    while ((w = popValue(state->win)) != NULL) {
		if (enable_js_windowopen) {
		    jWindowOpen(buf, w->url, w->name);
		}
	    }
	}
	if (state->close) {
	    if (CurrentTab != NULL && Currentbuf != NULL) {
		char *ans = "y";
		if (confirm_js_windowclose) {
		    ans = inputChar("window.close() ? (y/n)");
		}
		if (ans && tolower(*ans) == 'y') {
		    closeT();
		    js_free(interp, value);

		    return NULL;
		}
	    }
	    state->close = FALSE;
	}
    }
    js_free(interp, value);

    if (CurrentTab != NULL && Currentbuf != NULL) {
	value = js_eval2(interp, "history;");
	if (js_is_object(value)) {
	    HistoryState *state = js_get_state(value, HistoryClassID);

	    if (state->pos != 0) {
		script_chBuf(state->pos);
		state->pos = 0;
		js_free(interp, value);

		return NULL;
	    }
	}
	js_free(interp, value);
    }

    value = js_eval2(interp, "location;");
    if (js_is_object(value)) {
	LocationState *state = js_get_state(value, LocationClassID);
	if (state->url && state->refresh) {
	    if (state->refresh & JS_LOC_REFRESH) {
		buf->location = allocStr(state->url->ptr, state->url->length);
	    } else if (state->refresh & JS_LOC_HASH) {
		Anchor *a;
#if 0 /*def USE_M17N*/
		/* XXX */
		a = searchURLLabel(buf,
				   wc_conv(state->pu.label, buf->document_charset,
					   InnerCharset)->ptr);
#else
		a = searchURLLabel(buf, state->pu.label);
#endif
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
    js_free(interp, value);

    if (buf->formlist != NULL) {
	int i;
	FormList *fl;
	int changed = 0;

	for (i = 0, fl = buf->formlist; fl != NULL; i++, fl = fl->next) {
	    value = js_eval2(interp, Sprintf("document.forms[%d];", i)->ptr);
	    if (js_is_object(value)) {
		HTMLFormElementState *state = js_get_state(value, HTMLFormElementClassID);
		Str str;
		int j;
		FormItemList *fi;
		JSValue value2;

		value2 = js_eval2(interp, Sprintf("document.forms[%d].method;", i)->ptr);
		cstr = js_get_cstr(interp, value2);
		if (cstr != NULL) {
		    if (strcasecmp(cstr, "post") == 0) {
			fl->method = FORM_METHOD_POST;
		    } else if (strcasecmp(cstr, "internal") == 0) {
			fl->method = FORM_METHOD_INTERNAL;
		    } else /* if (strcasecmp(cstr, "get") == 0) */ {
			fl->method = FORM_METHOD_GET;
		    }
		}

		value2 = js_eval2(interp, Sprintf("document.forms[%d].action;", i)->ptr);
		str = js_get_str(interp, value2);
		if (str != NULL) {
		    fl->action = str;
		}

		value2 = js_eval2(interp, Sprintf("document.forms[%d].target;", i)->ptr);
		cstr = js_get_cstr(interp, value2);
		if (cstr != NULL) {
		    fl->target = cstr;
		}

		value2 = js_eval2(interp, Sprintf("document.forms[%d].name;", i)->ptr);
		cstr = js_get_cstr(interp, value2);
		if (cstr != NULL) {
		    fl->name = u2ic(cstr);
		}

		value2 = js_eval2(interp, Sprintf("document.forms[%d].id;", i)->ptr);
		cstr = js_get_cstr(interp, value2);
		if (cstr != NULL) {
		    fl->id = cstr;
		}

		str = js_get_function(interp, Sprintf("document.forms[%d].onsubmit;", i)->ptr);
		reset_func_list(fl->onsubmit);
		if (str != NULL) {
		    push_func(&fl->onsubmit, str);
		}
		while ((str = get_form_event(interp, i, "submit")) != NULL) {
		    push_func(&fl->onsubmit, str);
		}
		while ((str = get_document_event(interp, "submit")) != NULL) {
		    push_func(&fl->onsubmit, str);
		}

		str = js_get_function(interp, Sprintf("document.forms[%d].onreset;", i)->ptr);
		reset_func_list(fl->onreset);
		if (str != NULL) {
		    push_func(&fl->onreset, str);
		}
		while ((str = get_form_event(interp, i, "reset")) != NULL) {
		    push_func(&fl->onreset, str);
		}
		while ((str = get_document_event(interp, "reset")) != NULL) {
		    push_func(&fl->onreset, str);
		}

#ifdef SET_FORM_OPAQUE
		for (j = 0; ; j++) {
		    JSValue e = js_eval2(interp,
					 Sprintf("document.forms[%d].elements[%d];", i, j)->ptr);
		    if (js_is_object(e)) {
			fi = js_get_state(e, HTMLElementClassID);
			if (fi != NULL) {
			    changed |= get_form_element(buf, interp, i, j, fi);
			}
			js_free(interp, e);
		    } else {
			js_free(interp, e);
			break;
		    }
		}
#else
		for (j = 0, fi = fl->item; fi != NULL; j++, fi = fi->next) {
		    changed |= get_form_element(buf, interp, i, j, fi);
		}
#endif
		if (state->submit) {
		    followForm(fl);
		    state->submit = 0;
		}

		if (state->reset) {
		    state->reset = 0;
		}
	    }
	    js_free(interp, value);
	}

	if (changed) {
	    formResetBuffer(buf, buf->formitem);
	}
    }

    value = js_eval2(interp, "w3m_textNodesToStr(document);");
    Str str = js_get_str(interp, value);
    if (str != NULL && *str->ptr != '\0') {
#ifdef SCRIPT_DEBUG
	FILE *fp = fopen("scriptlog.txt", "a");
	fprintf(fp, "[TEXT]: %s\n", str->ptr);
	fclose(fp);
#endif
	set_delayed_message(u2is(str)->ptr);
    }

    if (alert_msg) {
	set_delayed_message(u2ic(alert_msg));
	alert_msg = NULL;
    }

    ret = NULL;
    value = js_eval2(interp, "document;");
    if (js_is_object(value)) {
	DocumentState *state = js_get_state(value, DocumentClassID);

	if (state->cookie_changed) {
#ifdef USE_COOKIE
	    set_cookie(state->cookie->ptr, &buf->currentURL);
#endif
	    state->cookie_changed = 0;
	}

	if (state->open) {
	    if (CurrentTab != NULL) {
		Buffer *new_buf = nullBuffer();
		new_buf->currentURL = buf->currentURL;
		if (new_buf != NULL) {
		    pushBuffer(new_buf);
		}

		if (state->write) {
		    process_html_str(new_buf, u2is(state->write)->ptr);
		    state->write = NULL;
		}
	    }
	    state->open = 0;
	} else if (state->write) {
	    ret = u2is(state->write);
	    state->write = NULL;
	}
    }
    js_free(interp, value);

    return ret;
}

static void
onload(void *interp)
{
    js_eval(interp,
	    "function w3m_onload(obj) {"
	    "  if (obj.myevents && obj.myevents.length > 0) {"
	    "    /*"
	    "     * 'i = 0; ...;i++' falls infinite loop if a listener calls addEventListener()."
	    "     * The case of calling removeEventListener() is not considered."
	    "     */"
	    "    for (let i = obj.myevents.length - 1; i >= 0; i--) {"
	    "      if (obj.myevents[i].type === \"load\" ||"
	    "          obj.myevents[i].type === \"DOMContentLoaded\" ||"
	    "          obj.myevents[i].type === \"visibilitychange\") {"
	    "        if (typeof obj.myevents[i].listener == \"function\") {"
	    "          obj.myevents[i].listener(obj.myevents[i]);"
	    "        } else if (obj.myevents[i].listener.handleEvent) {"
	    "          obj.myevents[i].listener.handleEvent(obj.myevents[i]);"
	    "        }"
	    "        obj.myevents.splice(i, 1);"
	    "      }"
	    "    }"
	    "  }"
	    "  if (obj.onload != undefined) {"
	    "    obj.onload();"
	    "    obj.onload = undefined;"
	    "  }"
	    "}"
	    "function w3m_element_onload(element) {"
	    "  w3m_onload(element);"
	    "  for (let i = 0; i < element.children.length; i++) {"
	    "    w3m_onload(element.children[i]);"
	    "  }"
	    "}"
	    ""
	    "w3m_element_onload(document);"
	    "w3m_onload(window);"
	    ""
	    "if (window.onload != undefined) {"
	    "  window.onload();"
	    "  window.onload = undefined;"
	    "}");
}

static int
script_js_eval(Buffer *buf, char *script, int buf2js, int js2buf, FormList *fl, Str *output)
{
    void *interp;
    int formidx = -1;
    JSValue ret;
    char *p;

    if (buf == NULL) {
	return 0;
    }

    if (buf->script_interp != NULL) {
	interp = buf->script_interp;
    } else {
	buf->script_interp = interp = js_html_init(buf);
	buf2js = 1;
    }

    if (buf2js) {
	if (buf2js > 0) {
	    js_reset_functions(interp);
	    script_buf2js(buf, interp);
	    create_dom_tree(interp, buf->sourcefile, wc_ces_to_charset(buf->document_charset));
	} else {
	    update_forms(buf, interp);
	}
    }

    /* 0x1f(^_) -> ' ' to avoid quickjs error. */
    for (p = script; *p; p++) {
	if (*p == 0x1f) {
	    *p = ' ';
	}
    }

    if (fl) {
	int i;
	FormList *l;

	for (i = 0, l = buf->formlist; l != NULL; i++, l = l->next) {
	    if (fl == l) {
		formidx = i;
		break;
	    }
	}
    }

    if (formidx == -1) {
	ret = js_eval2(interp, script);
    } else {
	/*
	 * Support 'this.form'.
	 * (http://www9.plala.or.jp/oyoyon/html/script/change_misc.html)
	 */
	ret = js_eval2_this(interp, formidx, script);
    }
    onload(interp);

    if (js2buf) {
	Str str = script_js2buf(buf, interp);
	if (output) {
	    *output = str;
	}
    } else {
	if (output) {
	    *output = NULL;
	}
    }

    return js_is_true(interp, ret);
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

int
script_eval(Buffer *buf, char *lang, char *script, int buf2js, int js2buf, FormList *fl,
	    Str *output)
{
    if (buf == NULL) {
	return 0;
    }

    if (buf->location)
	buf->location = NULL;
    if (! lang || ! script)
	return 0;
    if (buf->script_lang) {
	if (strcasecmp(lang, buf->script_lang))
	    return 0;
    } else
	buf->script_lang = lang;

#ifdef USE_JAVASCRIPT
    if (! strcasecmp(lang, "javascript") ||
	! strcasecmp(lang, "jscript"))
	return script_js_eval(buf, script, buf2js, js2buf, fl, output);
    else
#endif
	return 0;
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
