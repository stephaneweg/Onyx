/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2007 John-Mark Bell <jmb@netsurf-browser.org>
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 * Copyright 2012 Daniel Silverstone <dsilvers@netsurf-browser.org>
 */

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <hubbub/errors.h>
#include <hubbub/hubbub.h>
#include <hubbub/parser.h>

#include <dom/dom.h>

#include "parser.h"
#include "utils.h"

#include "core/document.h"
#include "core/string.h"
#include "core/node.h"
#include "core/element.h"

#include "html/html_document.h"
#include "html/html_button_element.h"
#include "html/html_input_element.h"
#include "html/html_select_element.h"
#include "html/html_text_area_element.h"

#include <libwapcaplet/libwapcaplet.h>

/**
 * libdom Hubbub parser context
 */
struct dom_hubbub_parser {
	hubbub_parser *parser;		/**< Hubbub parser instance */
	hubbub_tree_handler tree_handler;
					/**< Hubbub parser tree handler */

	struct dom_document *doc;	/**< DOM Document we're building within */

	dom_hubbub_encoding_source encoding_source;
					/**< The document's encoding source */
	const char *encoding; 		/**< The document's encoding */

	bool complete;			/**< Indicate stream completion */

	dom_msg msg;		/**< Informational messaging function */

	dom_script script;      /**< Script callback function */

	void *mctx;		/**< Pointer to client data */
};

/* Forward declaration to break reference loop */
static hubbub_error add_attributes_impl(void *parser, void *node,
		const hubbub_attribute *attributes, uint32_t n_attributes,
		bool only_missing);





/*--------------------- The callbacks definitions --------------------*/
static hubbub_error create_comment(void *parser, const hubbub_string *data,
		void **result)
{
	dom_hubbub_parser *dom_parser = (dom_hubbub_parser *) parser;
	dom_exception err;
	dom_string *str;
	struct dom_comment *comment;

	*result = NULL;

	err = dom_string_create(data->ptr, data->len, &str);
	if (err != DOM_NO_ERR) {
		dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
				"Can't create comment node text");
		return HUBBUB_UNKNOWN;
	}

	err = dom_document_create_comment(dom_parser->doc, str, &comment);
	if (err != DOM_NO_ERR) {
		dom_string_unref(str);
		dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
				"Can't create comment node with text '%.*s'",
				data->len, data->ptr);
		return HUBBUB_UNKNOWN;
	}

	*result = comment;

	dom_string_unref(str);

	return HUBBUB_OK;
}

static char *parser_strndup(const char *s, size_t n)
{
	size_t len;
	char *s2;

	for (len = 0; len != n && s[len] != '\0'; len++)
		continue;

	s2 = malloc(len + 1);
	if (s2 == NULL)
		return NULL;

	memcpy(s2, s, len);
	s2[len] = '\0';
	return s2;
}

static hubbub_error create_doctype(void *parser, const hubbub_doctype *doctype,
		void **result)
{
	dom_hubbub_parser *dom_parser = (dom_hubbub_parser *) parser;
	dom_exception err;
	char *qname, *public_id = NULL, *system_id = NULL;
	struct dom_document_type *dtype;

	*result = NULL;

	qname = parser_strndup((const char *) doctype->name.ptr,
			(size_t) doctype->name.len);
	if (qname == NULL) {
		dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
				"Can't create doctype name");
		goto fail;
	}

	if (doctype->public_missing == false) {
		public_id = parser_strndup(
				(const char *) doctype->public_id.ptr,
				(size_t) doctype->public_id.len);
	} else {
		public_id = strdup("");
	}
	if (public_id == NULL) {
		dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
				"Can't create doctype public id");
		goto clean1;
	}

	if (doctype->system_missing == false) {
		system_id = parser_strndup(
				(const char *) doctype->system_id.ptr,
				(size_t) doctype->system_id.len);
	} else {
		system_id = strdup("");
	}
	if (system_id == NULL) {
		dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
				"Can't create doctype system id");
		goto clean2;
	}

	err = dom_implementation_create_document_type(qname,
			public_id, system_id, &dtype);
	if (err != DOM_NO_ERR) {
		dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
				"Can't create the document type");
		goto clean3;
	}

	*result = dtype;

clean3:
	free(system_id);

clean2:
	free(public_id);

clean1:
	free(qname);

fail:
	if (*result == NULL)
		return HUBBUB_UNKNOWN;
	else
		return HUBBUB_OK;
}

static hubbub_error create_element(void *parser, const hubbub_tag *tag,
		void **result)
{
	dom_hubbub_parser *dom_parser = (dom_hubbub_parser *) parser;
	dom_exception err;
	dom_string *name;
	struct dom_element *element = NULL;
	hubbub_error herr;

	*result = NULL;

	err = dom_string_create_interned(tag->name.ptr, tag->name.len, &name);
	if (err != DOM_NO_ERR) {
		dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
				"Can't create element name");
		goto fail;
	}

	if (tag->ns == HUBBUB_NS_NULL) {
		err = dom_document_create_element(dom_parser->doc, name,
				&element);
		if (err != DOM_NO_ERR) {
			dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
					"Can't create the DOM element");
			goto clean1;
		}
	} else {
		/* Onyx: the token's name is the local name ("xyz:abc" is one) */
		err = _dom_html_document_create_element_parser(dom_parser->doc,
				dom_namespaces[tag->ns], name, &element);
		if (err != DOM_NO_ERR) {
			dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
					"Can't create the DOM element");
			goto clean1;
		}
	}

	/* By now, we MUST have constructed an element */
	assert(element != NULL);

	if (tag->n_attributes > 0) {
		herr = add_attributes_impl(parser, element, tag->attributes,
				tag->n_attributes, false);
		if (herr != HUBBUB_OK)
			goto clean1;
	}

	/* Now do some special per-element-type handling */
	dom_html_element_type tag_type;
	err = dom_html_element_get_tag_type(element, &tag_type);
	if (err != DOM_NO_ERR) {
		dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
				"Can't get tag type out of element");
		goto clean1;
	}

	switch (tag_type) {
	case DOM_HTML_ELEMENT_TYPE_SCRIPT: {
		/* Kickstart of https://html.spec.whatwg.org/multipage/scripting.html#script-processing-model */
		dom_html_script_element *script = (dom_html_script_element *)element;
		dom_html_script_element_flags flags;
		err = dom_html_script_element_get_flags(script, &flags);
		if (err != DOM_NO_ERR) {
			dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
					"Can't get flags out of script element");
			goto clean1;
		}
		flags |= DOM_HTML_SCRIPT_ELEMENT_FLAG_PARSER_INSERTED;
		err = dom_html_script_element_set_flags(script, flags);
		if (err != DOM_NO_ERR) {
			dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
					"Can't set flags into script element");
			goto clean1;
		}
		break;
	}
	default:
		/* Nothing */
		break;
	}


	*result = element;

clean1:
	dom_string_unref(name);

fail:
	if (*result == NULL)
		return HUBBUB_UNKNOWN;
	else
		return HUBBUB_OK;
}

static hubbub_error create_text(void *parser, const hubbub_string *data,
		void **result)
{
	dom_hubbub_parser *dom_parser = (dom_hubbub_parser *) parser;
	dom_exception err;
	dom_string *str;
	struct dom_text *text = NULL;

	*result = NULL;

	err = dom_string_create(data->ptr, data->len, &str);
	if (err != DOM_NO_ERR) {
		dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
				"Can't create text '%.*s'", data->len,
				data->ptr);
		goto fail;
	}

	err = dom_document_create_text_node(dom_parser->doc, str, &text);
	if (err != DOM_NO_ERR) {
		dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
				"Can't create the DOM text node");
		goto clean1;
	}

	*result = text;
clean1:
	dom_string_unref(str);

fail:
	if (*result == NULL)
		return HUBBUB_UNKNOWN;
	else
		return HUBBUB_OK;

}

static hubbub_error ref_node(void *parser, void *node)
{
	struct dom_node *dnode = (struct dom_node *) node;

	UNUSED(parser);

	dom_node_ref(dnode);

	return HUBBUB_OK;
}

static hubbub_error unref_node(void *parser, void *node)
{
	struct dom_node *dnode = (struct dom_node *) node;

	UNUSED(parser);

	dom_node_unref(dnode);

	return HUBBUB_OK;
}

static hubbub_error append_child(void *parser, void *parent, void *child,
		void **result)
{
	dom_hubbub_parser *dom_parser = (dom_hubbub_parser *) parser;
	dom_exception err;

	err = dom_node_append_child((struct dom_node *) parent,
				    (struct dom_node *) child,
				    (struct dom_node **) result);
	if (err != DOM_NO_ERR) {
		dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
				"Can't append child '%p' for parent '%p'",
				child, parent);
		return HUBBUB_UNKNOWN;
	}

	return HUBBUB_OK;
}

static hubbub_error insert_before(void *parser, void *parent, void *child,
		void *ref_child, void **result)
{
	dom_hubbub_parser *dom_parser = (dom_hubbub_parser *) parser;
	dom_exception err;

	err = dom_node_insert_before((struct dom_node *) parent,
			(struct dom_node *) child,
			(struct dom_node *) ref_child,
			(struct dom_node **) result);
	if (err != DOM_NO_ERR) {
		dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
				"Can't insert node '%p' before node '%p'",
				child, ref_child);
		return HUBBUB_UNKNOWN;
	}

	return HUBBUB_OK;
}

static hubbub_error remove_child(void *parser, void *parent, void *child,
		void **result)
{
	dom_hubbub_parser *dom_parser = (dom_hubbub_parser *) parser;
	dom_exception err;

	err = dom_node_remove_child((struct dom_node *) parent,
			(struct dom_node *) child,
			(struct dom_node **) result);
	if (err != DOM_NO_ERR) {
		dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
				"Can't remove child '%p'", child);
		return HUBBUB_UNKNOWN;
	}

	return HUBBUB_OK;
}

static hubbub_error clone_node(void *parser, void *node, bool deep,
		void **result)
{
	dom_hubbub_parser *dom_parser = (dom_hubbub_parser *) parser;
	dom_exception err;

	err = dom_node_clone_node((struct dom_node *) node, deep,
			(struct dom_node **) result);
	if (err != DOM_NO_ERR) {
		dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
				"Can't clone node '%p'", node);
		return HUBBUB_UNKNOWN;
	}

	return HUBBUB_OK;
}

static hubbub_error reparent_children(void *parser, void *node,
		void *new_parent)
{
	dom_hubbub_parser *dom_parser = (dom_hubbub_parser *) parser;
	dom_exception err;
	struct dom_node *child, *result;

	while(true) {
		err = dom_node_get_first_child((struct dom_node *) node,
				&child);
		if (err != DOM_NO_ERR) {
			dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
					"Error in dom_note_get_first_child");
			return HUBBUB_UNKNOWN;
		}
		if (child == NULL)
			break;

		err = dom_node_remove_child(node, (struct dom_node *) child,
				&result);
		if (err != DOM_NO_ERR) {
			dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
					"Error in dom_node_remove_child");
			goto fail;
		}
		dom_node_unref(result);

		err = dom_node_append_child((struct dom_node *) new_parent,
				(struct dom_node *) child, &result);
		if (err != DOM_NO_ERR) {
			dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
					"Error in dom_node_append_child");
			goto fail;
		}
		dom_node_unref(result);
		dom_node_unref(child);
	}
	return HUBBUB_OK;

fail:
	dom_node_unref(child);
	return HUBBUB_UNKNOWN;
}

static hubbub_error get_parent(void *parser, void *node, bool element_only,
		void **result)
{
	dom_hubbub_parser *dom_parser = (dom_hubbub_parser *) parser;
	dom_exception err;
	struct dom_node *parent;
	dom_node_type type = DOM_NODE_TYPE_COUNT;

	err = dom_node_get_parent_node((struct dom_node *) node,
			&parent);
	if (err != DOM_NO_ERR) {
		dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
				"Error in dom_node_get_parent");
		return HUBBUB_UNKNOWN;
	}
	if (element_only == false) {
		*result = parent;
		return HUBBUB_OK;
	}

	err = dom_node_get_node_type(parent, &type);
	if (err != DOM_NO_ERR) {
		dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
				"Error in dom_node_get_type");
		goto fail;
	}
	if (type == DOM_ELEMENT_NODE) {
		*result = parent;
		return HUBBUB_OK;
	} else {
		*result = NULL;
		dom_node_unref(parent);
		return HUBBUB_OK;
	}

	return HUBBUB_OK;
fail:
	dom_node_unref(parent);
	return HUBBUB_UNKNOWN;
}

static hubbub_error has_children(void *parser, void *node, bool *result)
{
	dom_hubbub_parser *dom_parser = (dom_hubbub_parser *) parser;
	dom_exception err;

	err = dom_node_has_child_nodes((struct dom_node *) node, result);
	if (err != DOM_NO_ERR) {
		dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
				"Error in dom_node_has_child_nodes");
		return HUBBUB_UNKNOWN;
	}
	return HUBBUB_OK;
}

static hubbub_error form_associate(void *parser, void *form, void *node)
{
	dom_hubbub_parser *dom_parser = (dom_hubbub_parser *) parser;
	dom_html_form_element *form_ele = form;
	dom_node_internal *ele = node;
	dom_html_document *doc = (dom_html_document *)ele->owner;
	dom_exception err = DOM_NO_ERR;
	
	/* Determine the kind of the node we have here. */
	if (dom_string_caseless_isequal(ele->name,
			doc->elements[DOM_HTML_ELEMENT_TYPE_BUTTON])) {
		err = _dom_html_button_element_set_form(
			(dom_html_button_element *)node, form_ele);
		if (err != DOM_NO_ERR) {
			dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
					"Error in form_associate");
			return HUBBUB_UNKNOWN;
		}
	} else if (dom_string_caseless_isequal(ele->name,
			doc->elements[DOM_HTML_ELEMENT_TYPE_INPUT])) {
		err = _dom_html_input_element_set_form(
			(dom_html_input_element *)node, form_ele);
		if (err != DOM_NO_ERR) {
			dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
					"Error in form_associate");
			return HUBBUB_UNKNOWN;
		}
	} else if (dom_string_caseless_isequal(ele->name,
			doc->elements[DOM_HTML_ELEMENT_TYPE_SELECT])) {
		err = _dom_html_select_element_set_form(
			(dom_html_select_element *)node, form_ele);
		if (err != DOM_NO_ERR) {
			dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
					"Error in form_associate");
			return HUBBUB_UNKNOWN;
		}
	} else if (dom_string_caseless_isequal(ele->name,
			doc->elements[DOM_HTML_ELEMENT_TYPE_TEXTAREA])) {
		err = _dom_html_text_area_element_set_form(
			(dom_html_text_area_element *)node, form_ele);
		if (err != DOM_NO_ERR) {
			dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
					"Error in form_associate");
			return HUBBUB_UNKNOWN;
		}
	}
	
	return HUBBUB_OK;
}

static hubbub_error add_attributes_impl(void *parser, void *node,
		const hubbub_attribute *attributes, uint32_t n_attributes,
		bool only_missing)
{
	dom_hubbub_parser *dom_parser = (dom_hubbub_parser *) parser;
	dom_exception err;
	uint32_t i;

	for (i = 0; i < n_attributes; i++) {
		dom_string *name, *value;

		err = dom_string_create_interned(attributes[i].name.ptr,
				attributes[i].name.len, &name);
		if (err != DOM_NO_ERR) {
			dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
					"Can't create attribute name");
			goto fail;
		}

		/* Onyx: <html> / <body> again: their attributes are added only if missing */
		if (only_missing && attributes[i].ns == HUBBUB_NS_NULL) {
			bool has = false;
			if (dom_element_has_attribute((struct dom_element *) node,
					name, &has) == DOM_NO_ERR && has) {
				dom_string_unref(name);
				continue;
			}
		}

		err = dom_string_create(attributes[i].value.ptr,
				attributes[i].value.len, &value);
		if (err != DOM_NO_ERR) {
			dom_parser->msg(DOM_MSG_CRITICAL, dom_parser->mctx,
					"Can't create attribute value");
			dom_string_unref(name);
			goto fail;
		}

		if (attributes[i].ns == HUBBUB_NS_NULL) {
			_dom_element_parser_attrs = true;	/* Onyx */
			err = dom_element_set_attribute(
					(struct dom_element *) node, name,
					value);
			_dom_element_parser_attrs = false;
			dom_string_unref(name);
			dom_string_unref(value);
			if (err != DOM_NO_ERR) {
				dom_parser->msg(DOM_MSG_CRITICAL,
						dom_parser->mctx,
						"Can't add attribute");
			}
		} else {
			err = dom_element_set_attribute_ns(
					(struct dom_element *) node,
					dom_namespaces[attributes[i].ns], name,
					value);
			dom_string_unref(name);
			dom_string_unref(value);
			if (err != DOM_NO_ERR) {
				dom_parser->msg(DOM_MSG_CRITICAL,
						dom_parser->mctx,
						"Can't add attribute ns");
			}
		}
	}

	return HUBBUB_OK;

fail:
	return HUBBUB_UNKNOWN;
}

static hubbub_error add_attributes(void *parser, void *node,
		const hubbub_attribute *attributes, uint32_t n_attributes)
{
	return add_attributes_impl(parser, node, attributes, n_attributes, true);
}

/* Onyx: a template's contents -- a document fragment kept on the element (user data) */
static dom_string *template_content_key;

static void template_content_handler(dom_node_operation operation,
		dom_string *key, void *data, struct dom_node *src,
		struct dom_node *dst)
{
	UNUSED(key);
	UNUSED(src);
	UNUSED(dst);
	if (operation == DOM_NODE_DELETED && data != NULL) {
		/* the template goes (maybe with its whole document): the fragment's end
		 * must not destroy the document from inside the document's own
		 * destruction (its pending nodes' list emptied) -- held for the call */
		struct dom_document *doc = ((dom_node_internal *) data)->owner;
		if (doc != NULL)
			doc->base.base.refcnt++;
		dom_node_unref((struct dom_node *) data);
		if (doc != NULL)
			doc->base.base.refcnt--;
	}
}

dom_exception dom_hubbub_template_content(dom_element *template_element,
		dom_document_fragment **result)
{
	void *data = NULL, *prev = NULL;
	dom_document *doc = NULL;
	dom_document_fragment *f = NULL;
	dom_exception err;

	*result = NULL;
	{
		dom_html_element_type type;
		if (dom_html_element_get_tag_type(template_element, &type) != DOM_NO_ERR ||
				type != DOM_HTML_ELEMENT_TYPE_TEMPLATE)
			return DOM_NOT_SUPPORTED_ERR;
	}
	if (template_content_key == NULL) {
		err = dom_string_create_interned((const uint8_t *)
				"__onyx_template_content", 23, &template_content_key);
		if (err != DOM_NO_ERR)
			return err;
	}
	err = dom_node_get_user_data(template_element, template_content_key, &data);
	if (err == DOM_NO_ERR && data != NULL) {
		*result = (dom_document_fragment *) dom_node_ref((struct dom_node *) data);
		return DOM_NO_ERR;
	}
	err = dom_node_get_owner_document(template_element, &doc);
	if (err != DOM_NO_ERR || doc == NULL)
		return err != DOM_NO_ERR ? err : DOM_NOT_SUPPORTED_ERR;
	err = dom_document_create_document_fragment(doc, &f);
	dom_node_unref(doc);
	if (err != DOM_NO_ERR)
		return err;
	/* the user data holds the creation reference */
	err = dom_node_set_user_data(template_element, template_content_key, f,
			template_content_handler, &prev);
	if (err != DOM_NO_ERR) {
		dom_node_unref(f);
		return err;
	}
	*result = (dom_document_fragment *) dom_node_ref((struct dom_node *) f);
	return DOM_NO_ERR;
}

/* Onyx: shadow roots (the DOM standard's): a document fragment the host keeps as user data
 * (a reference), the host kept on the fragment (no reference), the root's mode and options
 * on it too; the document notes it has shadow roots (NetSurf then builds its boxes from the
 * flat tree) */
static dom_string *shadow_root_key, *shadow_host_key, *shadow_flags_key, *shadow_doc_key;

static dom_exception shadow_keys(void)
{
	dom_exception err = DOM_NO_ERR;
	if (shadow_root_key == NULL)
		err = dom_string_create_interned((const uint8_t *) "__onyx_shadow_root", 18,
				&shadow_root_key);
	if (err == DOM_NO_ERR && shadow_host_key == NULL)
		err = dom_string_create_interned((const uint8_t *) "__onyx_shadow_host", 18,
				&shadow_host_key);
	if (err == DOM_NO_ERR && shadow_flags_key == NULL)
		err = dom_string_create_interned((const uint8_t *) "__onyx_shadow_flags", 19,
				&shadow_flags_key);
	if (err == DOM_NO_ERR && shadow_doc_key == NULL)
		err = dom_string_create_interned((const uint8_t *) "__onyx_has_shadow", 17,
				&shadow_doc_key);
	return err;
}

static void shadow_root_handler(dom_node_operation operation,
		dom_string *key, void *data, struct dom_node *src,
		struct dom_node *dst)
{
	UNUSED(key);
	UNUSED(src);
	UNUSED(dst);
	if (operation == DOM_NODE_DELETED && data != NULL) {
		/* the host goes: its shadow root forgets it, and goes too unless a script
		 * still holds it (as template_content_handler: the document held) */
		struct dom_document *doc = ((dom_node_internal *) data)->owner;
		void *prev = NULL;
		dom_node_set_user_data((struct dom_node *) data, shadow_host_key, NULL, NULL,
				&prev);
		if (doc != NULL)
			doc->base.base.refcnt++;
		dom_node_unref((struct dom_node *) data);
		if (doc != NULL)
			doc->base.base.refcnt--;
	}
}

static bool shadow_host_name_ok(dom_element *host)
{
	static const char *const ok[] = { "article", "aside", "blockquote", "body",
		"div", "footer", "h1", "h2", "h3", "h4", "h5", "h6", "header", "main",
		"nav", "p", "section", "span" };
	dom_string *name = NULL;
	const char *s;
	size_t len, i;
	bool res = false;

	if ((dom_node_get_local_name(host, &name) != DOM_NO_ERR || name == NULL) &&
			(dom_node_get_node_name(host, &name) != DOM_NO_ERR || name == NULL))
		return false;
	s = dom_string_data(name);
	len = dom_string_byte_length(name);
	for (i = 0; i < sizeof ok / sizeof ok[0] && !res; i++)
		res = strlen(ok[i]) == len && strncasecmp(ok[i], s, len) == 0;
	/* a valid custom element name: an ASCII letter first (libdom may keep an HTML
	 * element's name upper case), a hyphen */
	if (!res && len > 1 && ((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z')) &&
			memchr(s, '-', len) != NULL)
		res = true;
	dom_string_unref(name);
	return res;
}

/* exported function documented in parser.h (Onyx) */
dom_exception dom_onyx_attach_shadow(dom_element *host, unsigned int flags,
		dom_document_fragment **result)
{
	dom_document *doc = NULL;
	dom_document_fragment *f = NULL;
	void *data = NULL, *prev = NULL;
	dom_exception err;

	*result = NULL;
	err = shadow_keys();
	if (err != DOM_NO_ERR)
		return err;
	if (!(flags & DOM_ONYX_SHADOW_ANY_NAME) && !shadow_host_name_ok(host))
		return DOM_NOT_SUPPORTED_ERR;
	err = dom_node_get_user_data(host, shadow_root_key, &data);
	if (err == DOM_NO_ERR && data != NULL)
		return DOM_NOT_SUPPORTED_ERR;
	err = dom_node_get_owner_document(host, &doc);
	if (err != DOM_NO_ERR || doc == NULL)
		return err != DOM_NO_ERR ? err : DOM_NOT_SUPPORTED_ERR;
	err = dom_document_create_document_fragment(doc, &f);
	if (err == DOM_NO_ERR)
		err = dom_node_set_user_data(doc, shadow_doc_key, (void *) 1, NULL, &prev);
	dom_node_unref(doc);
	if (err != DOM_NO_ERR) {
		if (f != NULL)
			dom_node_unref(f);
		return err;
	}
	err = dom_node_set_user_data(f, shadow_host_key, host, NULL, &prev);
	if (err == DOM_NO_ERR)
		err = dom_node_set_user_data(f, shadow_flags_key,
				(void *) (uintptr_t) (flags | 0x100), NULL, &prev);
	/* the host's user data holds the creation reference */
	if (err == DOM_NO_ERR)
		err = dom_node_set_user_data(host, shadow_root_key, f,
				shadow_root_handler, &prev);
	if (err != DOM_NO_ERR) {
		dom_node_unref(f);
		return err;
	}
	*result = (dom_document_fragment *) dom_node_ref((struct dom_node *) f);
	return DOM_NO_ERR;
}

/* exported function documented in parser.h (Onyx) */
struct dom_node *dom_onyx_shadow_root(struct dom_node *host)
{
	void *data = NULL;
	if (shadow_root_key == NULL || host == NULL ||
			((dom_node_internal *) host)->user_data == NULL)
		return NULL;
	dom_node_get_user_data(host, shadow_root_key, &data);
	return data;
}

/* exported function documented in parser.h (Onyx) */
struct dom_node *dom_onyx_shadow_host(struct dom_node *root)
{
	void *data = NULL;
	if (shadow_host_key == NULL || root == NULL ||
			((dom_node_internal *) root)->type != DOM_DOCUMENT_FRAGMENT_NODE ||
			((dom_node_internal *) root)->user_data == NULL)
		return NULL;
	dom_node_get_user_data(root, shadow_host_key, &data);
	return data;
}

/* exported function documented in parser.h (Onyx) */
unsigned int dom_onyx_shadow_flags(struct dom_node *root)
{
	void *data = NULL;
	if (shadow_flags_key == NULL || root == NULL)
		return 0;
	dom_node_get_user_data(root, shadow_flags_key, &data);
	return (unsigned int) (uintptr_t) data & 0xff;
}

/* exported function documented in parser.h (Onyx) */
bool dom_onyx_has_shadow(struct dom_document *doc)
{
	void *data = NULL;
	if (shadow_doc_key == NULL || doc == NULL)
		return false;
	dom_node_get_user_data(doc, shadow_doc_key, &data);
	return data != NULL;
}

/* exported function documented in parser.h (Onyx) */
struct dom_node *dom_onyx_node_root(struct dom_node *node)
{
	dom_node_internal *n = (dom_node_internal *) node;
	while (n != NULL && n->parent != NULL)
		n = n->parent;
	return (struct dom_node *) n;
}

/* Onyx: declarative shadow DOM -- <template shadowrootmode> in the document parser: a
 * shadow root attached to the host, the template's contents (never inserted) */
static hubbub_error attach_shadow(void *parser, void *host, void *template_node)
{
	dom_string *mode = NULL, *s = NULL, *key = NULL;
	dom_document_fragment *f = NULL;
	void *prev = NULL;
	unsigned int flags = DOM_ONYX_SHADOW_DECLARATIVE;
	dom_exception err;
	static const struct { const char *name; unsigned int flag; } opts[] = {
		{ "shadowrootdelegatesfocus", DOM_ONYX_SHADOW_DELEGATES_FOCUS },
		{ "shadowrootclonable", DOM_ONYX_SHADOW_CLONABLE },
		{ "shadowrootserializable", DOM_ONYX_SHADOW_SERIALIZABLE },
	};
	size_t i;

	UNUSED(parser);
	if (dom_string_create((const uint8_t *) "shadowrootmode", 14, &key) != DOM_NO_ERR)
		return HUBBUB_UNKNOWN;
	err = dom_element_get_attribute(template_node, key, &mode);
	dom_string_unref(key);
	if (err != DOM_NO_ERR || mode == NULL)
		return HUBBUB_UNKNOWN;
	if (dom_string_byte_length(mode) == 6 &&
			strncasecmp(dom_string_data(mode), "closed", 6) == 0)
		flags |= DOM_ONYX_SHADOW_CLOSED;
	else if (dom_string_byte_length(mode) != 4 ||
			strncasecmp(dom_string_data(mode), "open", 4) != 0) {
		dom_string_unref(mode);
		return HUBBUB_UNKNOWN;
	}
	dom_string_unref(mode);
	for (i = 0; i < sizeof opts / sizeof opts[0]; i++) {
		bool has = false;
		if (dom_string_create((const uint8_t *) opts[i].name, strlen(opts[i].name),
				&key) != DOM_NO_ERR)
			return HUBBUB_UNKNOWN;
		err = dom_element_has_attribute(template_node, key, &has);
		dom_string_unref(key);
		if (err == DOM_NO_ERR && has)
			flags |= opts[i].flag;
	}
	UNUSED(s);
	if (dom_onyx_attach_shadow(host, flags, &f) != DOM_NO_ERR)
		return HUBBUB_UNKNOWN;
	/* the template's contents are the shadow root (its user data holds a reference) */
	if (template_content_key == NULL &&
			dom_string_create_interned((const uint8_t *) "__onyx_template_content",
				23, &template_content_key) != DOM_NO_ERR) {
		dom_node_unref(f);
		return HUBBUB_UNKNOWN;
	}
	if (dom_node_set_user_data(template_node, template_content_key, f,
			template_content_handler, &prev) != DOM_NO_ERR) {
		dom_node_unref(f);
		return HUBBUB_UNKNOWN;
	}
	return HUBBUB_OK;
}

static hubbub_error template_content(void *parser, void *node, void **result)
{
	UNUSED(parser);
	if (dom_hubbub_template_content((dom_element *) node,
			(dom_document_fragment **) result) != DOM_NO_ERR)
		return HUBBUB_UNKNOWN;
	return HUBBUB_OK;
}

/* Onyx: the standard's "insert a character": onto the text node before the place, if any */
static hubbub_error insert_text(void *parser, void *parent, void *ref_child,
		const hubbub_string *data)
{
	dom_hubbub_parser *dom_parser = (dom_hubbub_parser *) parser;
	struct dom_node *prev = NULL, *res = NULL;
	struct dom_text *text = NULL;
	dom_node_type type;
	dom_string *str;
	dom_exception err;

	if (ref_child != NULL)
		err = dom_node_get_previous_sibling((struct dom_node *) ref_child, &prev);
	else
		err = dom_node_get_last_child((struct dom_node *) parent, &prev);
	if (err != DOM_NO_ERR)
		prev = NULL;

	err = dom_string_create(data->ptr, data->len, &str);
	if (err != DOM_NO_ERR) {
		if (prev != NULL)
			dom_node_unref(prev);
		return HUBBUB_NOMEM;
	}

	if (prev != NULL && dom_node_get_node_type(prev, &type) == DOM_NO_ERR &&
			type == DOM_TEXT_NODE) {
		err = dom_characterdata_append_data(prev, str);
		dom_node_unref(prev);
		dom_string_unref(str);
		return err == DOM_NO_ERR ? HUBBUB_OK : HUBBUB_UNKNOWN;
	}
	if (prev != NULL)
		dom_node_unref(prev);

	err = dom_document_create_text_node(dom_parser->doc, str, &text);
	dom_string_unref(str);
	if (err != DOM_NO_ERR)
		return HUBBUB_UNKNOWN;
	if (ref_child != NULL)
		err = dom_node_insert_before((struct dom_node *) parent,
				(struct dom_node *) text, (struct dom_node *) ref_child, &res);
	else
		err = dom_node_append_child((struct dom_node *) parent,
				(struct dom_node *) text, &res);
	if (res != NULL)
		dom_node_unref(res);
	dom_node_unref(text);
	return err == DOM_NO_ERR ? HUBBUB_OK : HUBBUB_UNKNOWN;
}

static hubbub_error set_quirks_mode(void *parser, hubbub_quirks_mode mode)
{
	dom_hubbub_parser *dom_parser = (dom_hubbub_parser *) parser;

	switch (mode) {
	case HUBBUB_QUIRKS_MODE_NONE:
		dom_document_set_quirks_mode(dom_parser->doc,
					     DOM_DOCUMENT_QUIRKS_MODE_NONE);
		break;
	case HUBBUB_QUIRKS_MODE_LIMITED:
		dom_document_set_quirks_mode(dom_parser->doc,
					     DOM_DOCUMENT_QUIRKS_MODE_LIMITED);
		break;
	case HUBBUB_QUIRKS_MODE_FULL:
		dom_document_set_quirks_mode(dom_parser->doc,
					     DOM_DOCUMENT_QUIRKS_MODE_FULL);
		break;
	}

	return HUBBUB_OK;
}

static hubbub_error change_encoding(void *parser, const char *charset)
{
	dom_hubbub_parser *dom_parser = (dom_hubbub_parser *) parser;
	hubbub_charset_source source;
	const char *name;

	/* If we have an encoding here, it means we are *certain* */
	if (dom_parser->encoding != NULL) {
		return HUBBUB_OK;
	}

	/* Find the confidence otherwise (can only be from a BOM) */
	name = hubbub_parser_read_charset(dom_parser->parser, &source);

	if (source == HUBBUB_CHARSET_CONFIDENT) {
		dom_parser->encoding_source = DOM_HUBBUB_ENCODING_SOURCE_DETECTED;
		dom_parser->encoding = charset;
		return HUBBUB_OK;
	}

	/* So here we have something of confidence tentative... */
	/* http://www.whatwg.org/specs/web-apps/current-work/#change */

	/* 2. "If the new encoding is identical or equivalent to the encoding
	 * that is already being used to interpret the input stream, then set
	 * the confidence to confident and abort these steps." */

	/* Whatever happens, the encoding should be set here; either for
	 * reprocessing with a different charset, or for confirming that the
	 * charset is in fact correct */
	dom_parser->encoding = charset;
	dom_parser->encoding_source = DOM_HUBBUB_ENCODING_SOURCE_META;

	/* Equal encodings will have the same string pointers */
	return (charset == name) ? HUBBUB_OK : HUBBUB_ENCODINGCHANGE;
}

static hubbub_error complete_script(void *parser, void *script)
{
	dom_hubbub_parser *dom_parser = (dom_hubbub_parser *) parser;
	dom_hubbub_error err;

	err = dom_parser->script(dom_parser->mctx, (struct dom_node *)script);

	if (err == DOM_HUBBUB_OK) {
		return HUBBUB_OK;
	}

	if ((err & DOM_HUBBUB_HUBBUB_ERR) != 0) {
		return err & (~DOM_HUBBUB_HUBBUB_ERR);
	}

	return HUBBUB_UNKNOWN;
}

static hubbub_tree_handler tree_handler = {
	.create_comment = create_comment,
	.create_doctype = create_doctype,
	.create_element = create_element,
	.create_text = create_text,
	.ref_node = ref_node,
	.unref_node = unref_node,
	.append_child = append_child,
	.insert_before = insert_before,
	.remove_child = remove_child,
	.clone_node = clone_node,
	.reparent_children = reparent_children,
	.get_parent = get_parent,
	.has_children = has_children,
	.form_associate = form_associate,
	.add_attributes = add_attributes,
	.set_quirks_mode = set_quirks_mode,
	.encoding_change = change_encoding,
	.complete_script = complete_script,
	.ctx = NULL,
	/* Onyx */
	.template_content = template_content,
	.insert_text = insert_text,
	.attach_shadow = attach_shadow,
};

/**
 * Default message callback
 */
static void dom_hubbub_parser_default_msg(uint32_t severity, void *ctx,
		const char *msg, ...)
{
	UNUSED(severity);
	UNUSED(ctx);
	UNUSED(msg);
}

/**
 * Default script callback.
 */
static dom_hubbub_error
dom_hubbub_parser_default_script(void *ctx, struct dom_node *node)
{
	UNUSED(ctx);
	UNUSED(node);
	return DOM_HUBBUB_OK;
}

/**
 * Create a Hubbub parser instance
 *
 * \param params The binding creation parameters
 * \param parser Pointer to location to recive instance.
 * \param document Pointer to location to receive document.
 * \return Error code
 */
dom_hubbub_error
dom_hubbub_parser_create(dom_hubbub_parser_params *params,
			 dom_hubbub_parser **parser,
			 dom_document **document)
{
	dom_hubbub_parser *binding;
	hubbub_parser_optparams optparams;
	hubbub_error error;
	dom_exception err;
	dom_string *idname = NULL;

	/* check result parameters */
	if (document == NULL) {
		return DOM_HUBBUB_BADPARM;
	}

	if (parser == NULL) {
		return DOM_HUBBUB_BADPARM;
	}

	/* setup binding parser context */
	binding = malloc(sizeof(dom_hubbub_parser));
	if (binding == NULL) {
		return DOM_HUBBUB_NOMEM;
	}

	binding->parser = NULL;
	binding->doc = NULL;
	binding->encoding = params->enc;

	if (params->enc != NULL) {
		binding->encoding_source = DOM_HUBBUB_ENCODING_SOURCE_HEADER;
	} else {
		binding->encoding_source = DOM_HUBBUB_ENCODING_SOURCE_DETECTED;
	}

	binding->complete = false;

	if (params->msg == NULL) {
		binding->msg = dom_hubbub_parser_default_msg;
	} else {
		binding->msg = params->msg;
	}
	binding->mctx = params->ctx;

	/* ensure script function is valid or use the default */
	if (params->script == NULL) {
		binding->script = dom_hubbub_parser_default_script;
	} else {
		binding->script = params->script;
	}

	/* create hubbub parser */
	error = hubbub_parser_create(binding->encoding,
				     params->fix_enc,
				     &binding->parser);
	if (error != HUBBUB_OK)	 {
		free(binding);
		return (DOM_HUBBUB_HUBBUB_ERR | error);
	}

	/* create DOM document */
	err = dom_implementation_create_document(DOM_IMPLEMENTATION_HTML,
						 NULL,
						 NULL,
						 NULL,
						 params->daf,
						 params->ctx,
						 &binding->doc);
	if (err != DOM_NO_ERR) {
		hubbub_parser_destroy(binding->parser);
		free(binding);
		return DOM_HUBBUB_DOM;
	}

	binding->tree_handler = tree_handler;
	binding->tree_handler.ctx = (void *)binding;

	/* set tree handler on parser */
	optparams.tree_handler = &binding->tree_handler;
	hubbub_parser_setopt(binding->parser,
			     HUBBUB_PARSER_TREE_HANDLER,
			     &optparams);

	/* set document node*/
	optparams.document_node = dom_node_ref((struct dom_node *)binding->doc);
	hubbub_parser_setopt(binding->parser,
			     HUBBUB_PARSER_DOCUMENT_NODE,
			     &optparams);

	/* set scripting state */
	optparams.enable_scripting = params->enable_script;
	hubbub_parser_setopt(binding->parser,
			     HUBBUB_PARSER_ENABLE_SCRIPTING,
			     &optparams);

	/* set the document id parameter before the parse so searches
	 * based on id succeed.
	 */
	err = dom_string_create_interned((const uint8_t *) "id",
					 SLEN("id"),
					 &idname);
	if (err != DOM_NO_ERR) {
		binding->msg(DOM_MSG_ERROR, binding->mctx, "Can't set DOM document id name");
		hubbub_parser_destroy(binding->parser);
		free(binding);
		return DOM_HUBBUB_DOM;
	}
	_dom_document_set_id_name(binding->doc, idname);
	dom_string_unref(idname);

	/* set return parameters */
	*document = (dom_document *)dom_node_ref(binding->doc);
	*parser = binding;

	return DOM_HUBBUB_OK;
}


/**
 * Create a Hubbub parser instance
 *
 * \param params The binding creation parameters
 * \param parser Pointer to location to recive instance.
 * \param document Pointer to location to receive document.
 * \return Error code
 */
dom_hubbub_error
dom_hubbub_fragment_parser_create(dom_hubbub_parser_params *params,
				  dom_document *document,
				  dom_hubbub_parser **parser,
				  dom_document_fragment **fragment)
{
	dom_hubbub_parser *binding;
	hubbub_parser_optparams optparams;
	hubbub_error error;
	dom_exception err;

	if (document == NULL) {
		return DOM_HUBBUB_BADPARM;
	}

	/* check result parameters */
	if (fragment == NULL) {
		return DOM_HUBBUB_BADPARM;
	}

	if (parser == NULL) {
		return DOM_HUBBUB_BADPARM;
	}

	/* setup binding parser context */
	binding = malloc(sizeof(dom_hubbub_parser));
	if (binding == NULL) {
		return DOM_HUBBUB_NOMEM;
	}

	binding->parser = NULL;
	binding->doc = (struct dom_document *)dom_node_ref(document);
	binding->encoding = params->enc;

	if (params->enc != NULL) {
		binding->encoding_source = DOM_HUBBUB_ENCODING_SOURCE_HEADER;
	} else {
		binding->encoding_source = DOM_HUBBUB_ENCODING_SOURCE_DETECTED;
	}

	binding->complete = false;

	if (params->msg == NULL) {
		binding->msg = dom_hubbub_parser_default_msg;
	} else {
		binding->msg = params->msg;
	}
	binding->mctx = params->ctx;

	/* ensure script function is valid or use the default */
	if (params->script == NULL) {
		binding->script = dom_hubbub_parser_default_script;
	} else {
		binding->script = params->script;
	}

	/* create hubbub parser */
	error = hubbub_parser_create(binding->encoding,
				     params->fix_enc,
				     &binding->parser);
	if (error != HUBBUB_OK)	 {
		dom_node_unref(binding->doc);
		free(binding);
		return (DOM_HUBBUB_HUBBUB_ERR | error);
	}

	/* create DOM document fragment */
	err = dom_document_create_document_fragment(binding->doc,
						    fragment);
	if (err != DOM_NO_ERR) {
		hubbub_parser_destroy(binding->parser);
		dom_node_unref(binding->doc);
		free(binding);
		return DOM_HUBBUB_DOM;
	}

	binding->tree_handler = tree_handler;
	binding->tree_handler.ctx = (void *)binding;

	/* set tree handler on parser */
	optparams.tree_handler = &binding->tree_handler;
	hubbub_parser_setopt(binding->parser,
			     HUBBUB_PARSER_TREE_HANDLER,
			     &optparams);

	/* set document node*/
	optparams.document_node = dom_node_ref((struct dom_node *)*fragment);
	hubbub_parser_setopt(binding->parser,
			     HUBBUB_PARSER_DOCUMENT_NODE,
			     &optparams);

	/* set scripting state */
	optparams.enable_scripting = params->enable_script;
	hubbub_parser_setopt(binding->parser,
			     HUBBUB_PARSER_ENABLE_SCRIPTING,
			     &optparams);

	/* set return parameters */
	*parser = binding;
	/* fragment is already set up */

	return DOM_HUBBUB_OK;
}


/**
 * Onyx: create a parser for a fragment parsed in the context of an element (innerHTML,
 * insertAdjacentHTML, createContextualFragment...: the HTML standard's fragment parsing
 * algorithm). The nodes go into *fragment (not under an html / body element). context may
 * be NULL (a <body> then).
 */
dom_hubbub_error
dom_hubbub_fragment_parser_create_ctx(dom_hubbub_parser_params *params,
		dom_document *document, dom_element *context,
		dom_hubbub_parser **parser, dom_document_fragment **fragment)
{
	dom_hubbub_error herr;
	hubbub_parser_optparams o;
	char name[64] = "body";
	dom_string *ns = NULL, *local = NULL;
	struct dom_node *n = NULL;
	dom_document_quirks_mode quirks = DOM_DOCUMENT_QUIRKS_MODE_NONE;

	herr = dom_hubbub_fragment_parser_create(params, document, parser, fragment);
	if (herr != DOM_HUBBUB_OK)
		return herr;

	memset(&o, 0, sizeof(o));
	o.fragment_context.ns = HUBBUB_NS_HTML;
	o.fragment_context.name = name;
	if (context != NULL) {
		dom_node_get_namespace(context, &ns);
		dom_node_get_local_name(context, &local);
		if (ns != NULL && dom_string_isequal(ns, dom_namespaces[DOM_NAMESPACE_SVG]))
			o.fragment_context.ns = HUBBUB_NS_SVG;
		else if (ns != NULL && dom_string_isequal(ns,
				dom_namespaces[DOM_NAMESPACE_MATHML]))
			o.fragment_context.ns = HUBBUB_NS_MATHML;
		if (local != NULL) {
			size_t l = dom_string_byte_length(local), i;
			if (l > sizeof(name) - 1)
				l = sizeof(name) - 1;
			memcpy(name, dom_string_data(local), l);
			name[l] = 0;
			/* libdom keeps an HTML element's name in upper case */
			if (o.fragment_context.ns == HUBBUB_NS_HTML)
				for (i = 0; i < l; i++)
					if (name[i] >= 'A' && name[i] <= 'Z')
						name[i] += 32;
		}
		if (o.fragment_context.ns == HUBBUB_NS_MATHML &&
				strcasecmp(name, "annotation-xml") == 0) {
			dom_string *enc = NULL, *attr = NULL;
			dom_string_create((const uint8_t *) "encoding", 8, &attr);
			if (attr != NULL && dom_element_get_attribute(context, attr, &enc) ==
					DOM_NO_ERR && enc != NULL) {
				const char *e = dom_string_data(enc);
				size_t el = dom_string_byte_length(enc);
				if ((el == 9 && strncasecmp(e, "text/html", 9) == 0) ||
						(el == 21 && strncasecmp(e,
						"application/xhtml+xml", 21) == 0))
					o.fragment_context.html_integration_point = true;
				dom_string_unref(enc);
			}
			if (attr != NULL)
				dom_string_unref(attr);
		}
		o.fragment_context.node = context;

		/* the form element pointer: the nearest form, the context included */
		n = dom_node_ref((struct dom_node *) context);
		while (n != NULL) {
			struct dom_node *p = NULL;
			dom_node_type t;
			dom_string *nn = NULL;
			if (dom_node_get_node_type(n, &t) == DOM_NO_ERR &&
					t == DOM_ELEMENT_NODE &&
					dom_node_get_local_name(n, &nn) == DOM_NO_ERR &&
					nn != NULL) {
				bool form = dom_string_byte_length(nn) == 4 &&
					strncasecmp(dom_string_data(nn), "form", 4) == 0;
				dom_string_unref(nn);
				if (form) {
					o.fragment_context.form = n;
					break;
				}
			}
			dom_node_get_parent_node(n, &p);
			dom_node_unref(n);
			n = p;
		}
	}
	dom_document_get_quirks_mode(document, &quirks);
	o.fragment_context.quirks = quirks == DOM_DOCUMENT_QUIRKS_MODE_FULL;

	hubbub_parser_setopt((*parser)->parser, HUBBUB_PARSER_FRAGMENT_CONTEXT, &o);

	if (n != NULL)
		dom_node_unref(n);
	if (ns != NULL)
		dom_string_unref(ns);
	if (local != NULL)
		dom_string_unref(local);
	return DOM_HUBBUB_OK;
}

dom_hubbub_error
dom_hubbub_parser_insert_chunk(dom_hubbub_parser *parser,
			       const uint8_t *data,
			       size_t length)
{
	hubbub_parser_insert_chunk(parser->parser, data, length);

	return DOM_HUBBUB_OK;
}


/**
 * Destroy a Hubbub parser instance
 *
 * \param parser  The Hubbub parser object
 */
void dom_hubbub_parser_destroy(dom_hubbub_parser *parser)
{
	hubbub_parser_destroy(parser->parser);
	parser->parser = NULL;

	if (parser->doc != NULL) {
		dom_node_unref((struct dom_node *) parser->doc);
		parser->doc = NULL;
	}

	free(parser);
}

/**
 * Parse data with Hubbub parser
 *
 * \param parser  The parser object
 * \param data    The data to be parsed
 * \param len     The length of the data to be parsed
 * \return DOM_HUBBUB_OK on success,
 *         DOM_HUBBUB_HUBBUB_ERR | <hubbub_error> on failure
 */
dom_hubbub_error dom_hubbub_parser_parse_chunk(dom_hubbub_parser *parser,
		const uint8_t *data, size_t len)
{
	hubbub_error err;

	err = hubbub_parser_parse_chunk(parser->parser, data, len);
	if (err != HUBBUB_OK)
		return DOM_HUBBUB_HUBBUB_ERR | err;

	return DOM_HUBBUB_OK;
}

/**
 * Notify the parser to complete parsing
 *
 * \param parser  The parser object
 * \return DOM_HUBBUB_OK                          on success,
 *         DOM_HUBBUB_HUBBUB_ERR | <hubbub_error> on underlaying parser failure
 *         DOMHUBBUB_UNKNOWN | <lwc_error>        on libwapcaplet failure
 */
dom_hubbub_error dom_hubbub_parser_completed(dom_hubbub_parser *parser)
{
	hubbub_error err;

	err = hubbub_parser_completed(parser->parser);
	if (err != HUBBUB_OK) {
		parser->msg(DOM_MSG_ERROR, parser->mctx,
				"hubbub_parser_completed failed: %d", err);
		return DOM_HUBBUB_HUBBUB_ERR | err;
	}

	parser->complete = true;

	return DOM_HUBBUB_OK;
}

/**
 * Retrieve the encoding
 *
 * \param parser  The parser object
 * \param source  The encoding_source
 * \return the encoding name
 */
const char *dom_hubbub_parser_get_encoding(dom_hubbub_parser *parser,
		dom_hubbub_encoding_source *source)
{
	*source = parser->encoding_source;

	return parser->encoding != NULL ? parser->encoding
					: "Windows-1252";
}

/**
 * Set the Parse pause state.
 *
 * \param parser  The parser object
 * \param pause   The pause state to set.
 * \return DOM_HUBBUB_OK on success,
 *         DOM_HUBBUB_HUBBUB_ERR | <hubbub_error> on failure
 */
dom_hubbub_error dom_hubbub_parser_pause(dom_hubbub_parser *parser, bool pause)
{
	hubbub_error err;
	hubbub_parser_optparams params;

	params.pause_parse = pause;
	err = hubbub_parser_setopt(parser->parser, HUBBUB_PARSER_PAUSE, &params);
	if (err != HUBBUB_OK)
		return DOM_HUBBUB_HUBBUB_ERR | err;

	return DOM_HUBBUB_OK;
}
