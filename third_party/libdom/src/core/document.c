/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2007 John-Mark Bell <jmb@netsurf-browser.org>
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 */

#include <assert.h>
#include <stdlib.h>

#include <dom/functypes.h>
#include <dom/core/attr.h>
#include <dom/core/element.h>
#include <dom/core/document.h>
#include <dom/core/implementation.h>

#include "core/string.h"
#include "core/attr.h"
#include "core/cdatasection.h"
#include "core/comment.h"
#include "core/document.h"
#include "core/doc_fragment.h"
#include "core/element.h"
#include "core/entity_ref.h"
#include "core/namednodemap.h"
#include "core/nodelist.h"
#include "core/pi.h"
#include "core/text.h"
#include "utils/validate.h"
#include "utils/namespace.h"
#include "utils/utils.h"

/**
 * Item in list of active nodelists
 */
struct dom_doc_nl {
	dom_nodelist *list;	/**< Nodelist */

	struct dom_doc_nl *next;	/**< Next item */
	struct dom_doc_nl *prev;	/**< Previous item */
};

/* The virtual functions of this dom_document */
static const struct dom_document_vtable document_vtable = {
	{
		{
			DOM_NODE_EVENT_TARGET_VTABLE
		},
		DOM_NODE_VTABLE_DOCUMENT
	},
	DOM_DOCUMENT_VTABLE
};

static const struct dom_node_protect_vtable document_protect_vtable = {
	DOM_DOCUMENT_PROTECT_VTABLE
};


/*----------------------------------------------------------------------*/

/* Internally used helper functions */
static dom_exception dom_document_dup_node(dom_document *doc, 
		dom_node *node, bool deep, dom_node **result, 
		dom_node_operation opt);


/*----------------------------------------------------------------------*/

/* The constructors and destructors */

/**
 * Create a Document
 *
 * \param doc    Pointer to location to receive created document
 * \param daf    The default action fetcher
 * \return DOM_NO_ERR on success, DOM_NO_MEM_ERR on memory exhaustion.
 *
 * The returned document will already be referenced.
 */
dom_exception _dom_document_create(dom_events_default_action_fetcher daf,
				   void *daf_ctx,
				   dom_document **doc)
{
	dom_document *d;
	dom_exception err;

	/* Create document */
	d = malloc(sizeof(dom_document));
	if (d == NULL)
		return DOM_NO_MEM_ERR;

	/* Initialise the virtual table */
	d->base.base.vtable = &document_vtable;
	d->base.vtable = &document_protect_vtable;

	/* Initialise base class -- the Document has no parent, so
	 * destruction will be attempted as soon as its reference count
	 * reaches zero. Documents own themselves (this simplifies the 
	 * rest of the code, as it doesn't need to special case Documents)
	 */
	err = _dom_document_initialise(d, daf, daf_ctx);
	if (err != DOM_NO_ERR) {
		/* Clean up document */
		free(d);
		return err;
	}

	*doc = d;

	return DOM_NO_ERR;
}

/* Initialise the document */
dom_exception _dom_document_initialise(dom_document *doc,
				       dom_events_default_action_fetcher daf, 
				       void *daf_ctx)
{
	dom_exception err;
	dom_string *name;

	err = dom_string_create((const uint8_t *) "#document", 
			SLEN("#document"), &name);
	if (err != DOM_NO_ERR)
		return err;

	err = dom_string_create_interned((const uint8_t *) "about:blank",
			SLEN("about:blank"), &doc->uri);
	if (err != DOM_NO_ERR) {
		dom_string_unref(name);
		return err;
	}

	doc->nodelists = NULL;

	err = _dom_node_initialise(&doc->base, doc, DOM_DOCUMENT_NODE,
			name, NULL, NULL, NULL);
	dom_string_unref(name);
	if (err != DOM_NO_ERR) {
		dom_string_unref(doc->uri);
		return err;
	}

	list_init(&doc->pending_nodes);

	err = dom_string_create_interned((const uint8_t *) "id",
					 SLEN("id"), &doc->id_name);
	if (err != DOM_NO_ERR) {
		dom_string_unref(doc->uri);
		return err;
	}
	doc->quirks = DOM_DOCUMENT_QUIRKS_MODE_NONE;

	err = dom_string_create_interned((const uint8_t *) "class",
			SLEN("class"), &doc->class_string);
	if (err != DOM_NO_ERR) {
		dom_string_unref(doc->uri);
		dom_string_unref(doc->id_name);
		return err;
	}

	err = dom_string_create_interned((const uint8_t *) "script",
			SLEN("script"), &doc->script_string);
	if (err != DOM_NO_ERR) {
		dom_string_unref(doc->uri);
		dom_string_unref(doc->id_name);
		dom_string_unref(doc->class_string);
		return err;
	}

	/* Intern the empty string. The use of a space in the constant
	 * is to prevent the compiler warning about an empty string.
	 */
	err = dom_string_create_interned((const uint8_t *) " ", 0,
					 &doc->_memo_empty);
	if (err != DOM_NO_ERR) {
		dom_string_unref(doc->uri);
		dom_string_unref(doc->id_name);
		dom_string_unref(doc->class_string);
		dom_string_unref(doc->script_string);
		return err;
	}

	err = dom_string_create_interned((const uint8_t *) "DOMNodeInserted",
					 SLEN("DOMNodeInserted"),
					 &doc->_memo_domnodeinserted);
	if (err != DOM_NO_ERR) {
		dom_string_unref(doc->_memo_empty);
		dom_string_unref(doc->uri);
		dom_string_unref(doc->id_name);
		dom_string_unref(doc->class_string);
		dom_string_unref(doc->script_string);
		return err;
	}

	err = dom_string_create_interned((const uint8_t *) "DOMNodeRemoved",
					 SLEN("DOMNodeRemoved"),
					 &doc->_memo_domnoderemoved);
	if (err != DOM_NO_ERR) {
		dom_string_unref(doc->_memo_domnodeinserted);
		dom_string_unref(doc->_memo_empty);
		dom_string_unref(doc->uri);
		dom_string_unref(doc->id_name);
		dom_string_unref(doc->class_string);
		dom_string_unref(doc->script_string);
		return err;
	}

	err = dom_string_create_interned((const uint8_t *) "DOMNodeInsertedIntoDocument",
					 SLEN("DOMNodeInsertedIntoDocument"),
					 &doc->_memo_domnodeinsertedintodocument);
	if (err != DOM_NO_ERR) {
		dom_string_unref(doc->_memo_domnoderemoved);
		dom_string_unref(doc->_memo_domnodeinserted);
		dom_string_unref(doc->_memo_empty);
		dom_string_unref(doc->uri);
		dom_string_unref(doc->id_name);
		dom_string_unref(doc->class_string);
		dom_string_unref(doc->script_string);
		return err;
	}

	err = dom_string_create_interned((const uint8_t *) "DOMNodeRemovedFromDocument",
					 SLEN("DOMNodeRemovedFromDocument"),
					 &doc->_memo_domnoderemovedfromdocument);
	if (err != DOM_NO_ERR) {
		dom_string_unref(doc->_memo_domnodeinsertedintodocument);
		dom_string_unref(doc->_memo_domnoderemoved);
		dom_string_unref(doc->_memo_domnodeinserted);
		dom_string_unref(doc->_memo_empty);
		dom_string_unref(doc->uri);
		dom_string_unref(doc->id_name);
		dom_string_unref(doc->class_string);
		dom_string_unref(doc->script_string);
		return err;
	}

	err = dom_string_create_interned((const uint8_t *) "DOMAttrModified",
					 SLEN("DOMAttrModified"),
					 &doc->_memo_domattrmodified);
	if (err != DOM_NO_ERR) {
		dom_string_unref(doc->_memo_domnoderemovedfromdocument);
		dom_string_unref(doc->_memo_domnodeinsertedintodocument);
		dom_string_unref(doc->_memo_domnoderemoved);
		dom_string_unref(doc->_memo_domnodeinserted);
		dom_string_unref(doc->_memo_empty);
		dom_string_unref(doc->uri);
		dom_string_unref(doc->id_name);
		dom_string_unref(doc->class_string);
		dom_string_unref(doc->script_string);
		return err;
	}

	err = dom_string_create_interned((const uint8_t *) "DOMCharacterDataModified",
					 SLEN("DOMCharacterDataModified"),
					 &doc->_memo_domcharacterdatamodified);
	if (err != DOM_NO_ERR) {
		dom_string_unref(doc->_memo_domattrmodified);
		dom_string_unref(doc->_memo_domnoderemovedfromdocument);
		dom_string_unref(doc->_memo_domnodeinsertedintodocument);
		dom_string_unref(doc->_memo_domnoderemoved);
		dom_string_unref(doc->_memo_domnodeinserted);
		dom_string_unref(doc->_memo_empty);
		dom_string_unref(doc->uri);
		dom_string_unref(doc->id_name);
		dom_string_unref(doc->class_string);
		dom_string_unref(doc->script_string);
		return err;
	}

	err = dom_string_create_interned((const uint8_t *) "DOMSubtreeModified",
					 SLEN("DOMSubtreeModified"),
					 &doc->_memo_domsubtreemodified);
	if (err != DOM_NO_ERR) {
		dom_string_unref(doc->_memo_domcharacterdatamodified);
		dom_string_unref(doc->_memo_domattrmodified);
		dom_string_unref(doc->_memo_domnoderemovedfromdocument);
		dom_string_unref(doc->_memo_domnodeinsertedintodocument);
		dom_string_unref(doc->_memo_domnoderemoved);
		dom_string_unref(doc->_memo_domnodeinserted);
		dom_string_unref(doc->_memo_empty);
		dom_string_unref(doc->uri);
		dom_string_unref(doc->id_name);
		dom_string_unref(doc->class_string);
		dom_string_unref(doc->script_string);
		return err;
	}

	doc->dispatching_mutation = 0;
	doc->onyx_ids = NULL;	/* (Onyx) */

	/* We should not pass a NULL when all things hook up */
	return _dom_document_event_internal_initialise(&doc->dei, daf, daf_ctx);
}


/* Finalise the document */
static void onyx_id_free(dom_document *doc);	/* (Onyx: getElementById's index) */

bool _dom_document_finalise(dom_document *doc)
{
	onyx_id_free(doc);	/* (Onyx: before the tree's elements go) */

	/* Finalise base class, delete the tree in force */
	_dom_node_finalise(&doc->base);

	/* Now, the first_child and last_child should be null */
	doc->base.first_child = NULL;
	doc->base.last_child = NULL;

	/* Ensure list of nodes pending deletion is empty. If not,
	 * then we can't yet destroy the document (its destruction will
	 * have to wait until the pending nodes are destroyed) */
	if (doc->pending_nodes.next != &doc->pending_nodes)
		return false;

	/* Ok, the document tree is empty, as is the list of nodes pending
	 * deletion. Therefore, it is safe to destroy the document. */

	/* This is paranoia -- if there are any remaining nodelists,
	 * then the document's reference count will be
	 * non-zero as these data structures reference the document because
	 * they are held by the client. */
	doc->nodelists = NULL;

	if (doc->id_name != NULL)
		dom_string_unref(doc->id_name);

	dom_string_unref(doc->uri);
	dom_string_unref(doc->class_string);
	dom_string_unref(doc->script_string);
	dom_string_unref(doc->_memo_empty);
	dom_string_unref(doc->_memo_domnodeinserted);
	dom_string_unref(doc->_memo_domnoderemoved);
	dom_string_unref(doc->_memo_domnodeinsertedintodocument);
	dom_string_unref(doc->_memo_domnoderemovedfromdocument);
	dom_string_unref(doc->_memo_domattrmodified);
	dom_string_unref(doc->_memo_domcharacterdatamodified);
	dom_string_unref(doc->_memo_domsubtreemodified);
	
	_dom_document_event_internal_finalise(&doc->dei);

	return true;
}



/*----------------------------------------------------------------------*/

/* Public virtual functions */

/**
 * Retrieve the doctype of a document
 *
 * \param doc     The document to retrieve the doctype from
 * \param result  Pointer to location to receive result
 * \return DOM_NO_ERR.
 *
 * The returned node will have its reference count increased. It is
 * the responsibility of the caller to unref the node once it has
 * finished with it.
 */
dom_exception _dom_document_get_doctype(dom_document *doc,
		dom_document_type **result)
{
	dom_node_internal *c;

	for (c = doc->base.first_child; c != NULL; c = c->next) {
		if (c->type == DOM_DOCUMENT_TYPE_NODE)
			break;
	}

	if (c != NULL)
		dom_node_ref(c);

	*result = (dom_document_type *) c;

	return DOM_NO_ERR;
}

/**
 * Retrieve the DOM implementation that handles this document
 *
 * \param doc     The document to retrieve the implementation from
 * \param result  Pointer to location to receive result
 * \return DOM_NO_ERR.
 *
 * The returned implementation will have its reference count increased.
 * It is the responsibility of the caller to unref the implementation once
 * it has finished with it.
 */
dom_exception _dom_document_get_implementation(dom_document *doc,
		dom_implementation **result)
{
	UNUSED(doc);

	*result = (dom_implementation *) "libdom";

	return DOM_NO_ERR;
}

/**
 * Retrieve the document element of a document
 *
 * \param doc     The document to retrieve the document element from
 * \param result  Pointer to location to receive result
 * \return DOM_NO_ERR.
 *
 * The returned node will have its reference count increased. It is
 * the responsibility of the caller to unref the node once it has
 * finished with it.
 */
dom_exception _dom_document_get_document_element(dom_document *doc,
		dom_element **result)
{
	dom_node_internal *root;

	/* Find the first element node in child list */
	for (root = doc->base.first_child; root != NULL; root = root->next) {
		if (root->type == DOM_ELEMENT_NODE)
			break;
	}

	if (root != NULL)
		dom_node_ref(root);

	*result = (dom_element *) root;

	return DOM_NO_ERR;
}

/**
 * Create an element
 *
 * \param doc       The document owning the element
 * \param tag_name  The name of the element
 * \param result    Pointer to location to receive result
 * \return DOM_NO_ERR                on success,
 *         DOM_INVALID_CHARACTER_ERR if \p tag_name is invalid.
 *
 * \p doc and \p tag_name will have their reference counts increased.
 *
 * The returned node will have its reference count increased. It is
 * the responsibility of the caller to unref the node once it has
 * finished with it.
 */
dom_exception _dom_document_create_element(dom_document *doc,
		dom_string *tag_name, dom_element **result)
{
	if (_dom_validate_name(tag_name) == false)
		return DOM_INVALID_CHARACTER_ERR;

	return _dom_element_create(doc, tag_name, NULL, NULL, result);
}

/**
 * Create a document fragment
 *
 * \param doc     The document owning the fragment
 * \param result  Pointer to location to receive result
 * \return DOM_NO_ERR.
 *
 * The returned node will have its reference count increased. It is
 * the responsibility of the caller to unref the node once it has
 * finished with it.
 */
dom_exception _dom_document_create_document_fragment(dom_document *doc,
		dom_document_fragment **result)
{
	dom_string *name;
	dom_exception err;

	err = dom_string_create((const uint8_t *) "#document-fragment", 
			SLEN("#document-fragment"), &name);
	if (err != DOM_NO_ERR)
		return err;
	
	err = _dom_document_fragment_create(doc, name, NULL, result);
	dom_string_unref(name);

	return err;
}

/**
 * Create a text node
 *
 * \param doc     The document owning the node
 * \param data    The data for the node
 * \param result  Pointer to location to receive result
 * \return DOM_NO_ERR.
 *
 * The returned node will have its reference count increased. It is
 * the responsibility of the caller to unref the node once it has
 * finished with it.
 */
dom_exception _dom_document_create_text_node(dom_document *doc,
		dom_string *data, dom_text **result)
{
	dom_string *name;
	dom_exception err;

	err = dom_string_create((const uint8_t *) "#text", 
			SLEN("#text"), &name);
	if (err != DOM_NO_ERR)
		return err;
	
	err = _dom_text_create(doc, name, data, result);
	dom_string_unref(name);

	return err;
}

/**
 * Create a comment node
 *
 * \param doc     The document owning the node
 * \param data    The data for the node
 * \param result  Pointer to location to receive result
 * \return DOM_NO_ERR.
 *
 * The returned node will have its reference count increased. It is
 * the responsibility of the caller to unref the node once it has
 * finished with it.
 */
dom_exception _dom_document_create_comment(dom_document *doc,
		dom_string *data, dom_comment **result)
{
	dom_string *name;
	dom_exception err;

	err = dom_string_create((const uint8_t *) "#comment", SLEN("#comment"),
			&name);
	if (err != DOM_NO_ERR)
		return err;
	
	err = _dom_comment_create(doc, name, data, result);
	dom_string_unref(name);

	return err;
}

/**
 * Create a CDATA section
 *
 * \param doc     The document owning the section
 * \param data    The data for the section contents
 * \param result  Pointer to location to receive result
 * \return DOM_NO_ERR            on success,
 *         DOM_NOT_SUPPORTED_ERR if this is an HTML document.
 *
 * The returned node will have its reference count increased. It is
 * the responsibility of the caller to unref the node once it has
 * finished with it.
 */
dom_exception _dom_document_create_cdata_section(dom_document *doc,
		dom_string *data, dom_cdata_section **result)
{
	dom_string *name;
	dom_exception err;

	err = dom_string_create((const uint8_t *) "#cdata-section", 
			SLEN("#cdata-section"), &name);
	if (err != DOM_NO_ERR)
		return err;

	err = _dom_cdata_section_create(doc, name, data, result);
	dom_string_unref(name);

	return err;
}

/**
 * Create a processing instruction
 *
 * \param doc     The document owning the instruction
 * \param target  The instruction target
 * \param data    The data for the node
 * \param result  Pointer to location to receive result
 * \return DOM_NO_ERR                on success,
 *         DOM_INVALID_CHARACTER_ERR if \p target is invalid,
 *         DOM_NOT_SUPPORTED_ERR     if this is an HTML document.
 *
 * The returned node will have its reference count increased. It is
 * the responsibility of the caller to unref the node once it has
 * finished with it.
 */
dom_exception _dom_document_create_processing_instruction(
		dom_document *doc, dom_string *target,
		dom_string *data,
		dom_processing_instruction **result)
{
	if (_dom_validate_name(target) == false)
		return DOM_INVALID_CHARACTER_ERR;

	return _dom_processing_instruction_create(doc, target, data, result);
}

/**
 * Create an attribute
 *
 * \param doc     The document owning the attribute
 * \param name    The name of the attribute
 * \param result  Pointer to location to receive result
 * \return DOM_NO_ERR                on success,
 *         DOM_INVALID_CHARACTER_ERR if \p name is invalid.
 *
 * The constructed attribute will always be classified as 'specified'.
 *
 * The returned node will have its reference count increased. It is
 * the responsibility of the caller to unref the node once it has
 * finished with it.
 */
dom_exception _dom_document_create_attribute(dom_document *doc,
		dom_string *name, dom_attr **result)
{
	if (_dom_validate_name(name) == false)
		return DOM_INVALID_CHARACTER_ERR;

	return _dom_attr_create(doc, name, NULL, NULL, true, result);
}

/**
 * Create an entity reference
 *
 * \param doc     The document owning the reference
 * \param name    The name of the entity to reference
 * \param result  Pointer to location to receive result
 * \return DOM_NO_ERR                on success,
 *         DOM_INVALID_CHARACTER_ERR if \p name is invalid,
 *         DOM_NOT_SUPPORTED_ERR     if this is an HTML document.
 *
 * The returned node will have its reference count increased. It is
 * the responsibility of the caller to unref the node once it has
 * finished with it.
 */
dom_exception _dom_document_create_entity_reference(dom_document *doc,
		dom_string *name,
		dom_entity_reference **result)
{
	if (_dom_validate_name(name) == false)
		return DOM_INVALID_CHARACTER_ERR;

	return _dom_entity_reference_create(doc, name, NULL, result);
}

/**
 * Retrieve a list of all elements with a given tag name
 *
 * \param doc      The document to search in
 * \param tagname  The tag name to search for ("*" for all)
 * \param result   Pointer to location to receive result
 * \return DOM_NO_ERR.
 *
 * The returned list will have its reference count increased. It is
 * the responsibility of the caller to unref the list once it has
 * finished with it.
 */
dom_exception _dom_document_get_elements_by_tag_name(dom_document *doc,
		dom_string *tagname, dom_nodelist **result)
{
	return _dom_document_get_nodelist(doc, DOM_NODELIST_BY_NAME, 
			(dom_node_internal *) doc,  tagname, NULL, NULL, 
			result);
}

/**
 * Import a node from another document into this one
 *
 * \param doc     The document to import into
 * \param node    The node to import
 * \param deep    Whether to copy the node's subtree
 * \param result  Pointer to location to receive imported node in this document.
 * \return DOM_NO_ERR                on success,
 *         DOM_INVALID_CHARACTER_ERR if any of the names are invalid,
 *         DOM_NOT_SUPPORTED_ERR     if the type of \p node is unsupported,
 *         DOM_NO_MODIFICATION_ALLOWED_ERR if \p node or \p doc is readonly
 *
 * The returned node will have its reference count increased. It is
 * the responsibility of the caller to unref the node once it has
 * finished with it.
 */
dom_exception _dom_document_import_node(dom_document *doc,
		dom_node *node, bool deep, dom_node **result)
{
	/* TODO: The DOM_INVALID_CHARACTER_ERR exception */

	return dom_document_dup_node(doc, node, deep, result,
			DOM_NODE_IMPORTED);
}

/**
 * Create an element from the qualified name and namespace URI
 *
 * \param doc        The document owning the element
 * \param namespace  The namespace URI to use, or NULL for none
 * \param qname      The qualified name of the element
 * \param result     Pointer to location to receive result
 * \return DOM_NO_ERR                on success,
 *         DOM_INVALID_CHARACTER_ERR if \p qname is invalid,
 *         DOM_NAMESPACE_ERR         if \p qname is malformed, or it has a
 *                                   prefix and \p namespace is NULL, or
 *                                   \p qname has a prefix "xml" and
 *                                   \p namespace is not
 *                                   "http://www.w3.org/XML/1998/namespace",
 *                                   or \p qname has a prefix "xmlns" and
 *                                   \p namespace is not
 *                                   "http://www.w3.org/2000/xmlns", or
 *                                   \p namespace is
 *                                   "http://www.w3.org/2000/xmlns" and
 *                                   \p qname is not (or is not prefixed by)
 *                                   "xmlns",
 *         DOM_NOT_SUPPORTED_ERR     if \p doc does not support the "XML"
 *                                   feature.
 *
 * The returned node will have its reference count increased. It is
 * the responsibility of the caller to unref the node once it has
 * finished with it.
 */
dom_exception _dom_document_create_element_ns(dom_document *doc,
		dom_string *namespace, dom_string *qname,
		dom_element **result)
{
	dom_string *prefix, *localname;
	dom_exception err;

	if (_dom_validate_name(qname) == false)
		return DOM_INVALID_CHARACTER_ERR;

	/* Validate qname */
	err = _dom_namespace_validate_qname(qname, namespace);
	if (err != DOM_NO_ERR) {
		return err;
	}

	/* Divide QName into prefix/localname pair */
	err = _dom_namespace_split_qname(qname, &prefix, &localname);
	if (err != DOM_NO_ERR) {
		return err;
	}

	/* Attempt to create element */
	err = _dom_element_create(doc, localname, namespace, prefix, result);

	/* Tidy up */
	if (localname != NULL) {
		dom_string_unref(localname);
	}

	if (prefix != NULL) {
		dom_string_unref(prefix);
	}

	return err;
}

/**
 * Create an attribute from the qualified name and namespace URI
 *
 * \param doc        The document owning the attribute
 * \param namespace  The namespace URI to use
 * \param qname      The qualified name of the attribute
 * \param result     Pointer to location to receive result
 * \return DOM_NO_ERR                on success,
 *         DOM_INVALID_CHARACTER_ERR if \p qname is invalid,
 *         DOM_NAMESPACE_ERR         if \p qname is malformed, or it has a
 *                                   prefix and \p namespace is NULL, or
 *                                   \p qname has a prefix "xml" and
 *                                   \p namespace is not
 *                                   "http://www.w3.org/XML/1998/namespace",
 *                                   or \p qname has a prefix "xmlns" and
 *                                   \p namespace is not
 *                                   "http://www.w3.org/2000/xmlns", or
 *                                   \p namespace is
 *                                   "http://www.w3.org/2000/xmlns" and
 *                                   \p qname is not (or is not prefixed by)
 *                                   "xmlns",
 *         DOM_NOT_SUPPORTED_ERR     if \p doc does not support the "XML"
 *                                   feature.
 *
 * The returned node will have its reference count increased. It is
 * the responsibility of the caller to unref the node once it has
 * finished with it.
 */
dom_exception _dom_document_create_attribute_ns(dom_document *doc,
		dom_string *namespace, dom_string *qname,
		dom_attr **result)
{
	dom_string *prefix, *localname;
	dom_exception err;

	if (_dom_validate_name(qname) == false)
		return DOM_INVALID_CHARACTER_ERR;

	/* Validate qname */
	err = _dom_namespace_validate_qname(qname, namespace);
	if (err != DOM_NO_ERR) {
		return err;
	}

	/* Divide QName into prefix/localname pair */
	err = _dom_namespace_split_qname(qname, &prefix, &localname);
	if (err != DOM_NO_ERR) {
		return err;
	}

	/* Attempt to create attribute */
	err = _dom_attr_create(doc, localname, namespace, prefix, true, result);

	/* Tidy up */
	if (localname != NULL) {
		dom_string_unref(localname);
	}

	if (prefix != NULL) {
		dom_string_unref(prefix);
	}

	return err;
}

/**
 * Retrieve a list of all elements with a given local name and namespace URI
 *
 * \param doc        The document to search in
 * \param namespace  The namespace URI
 * \param localname  The local name
 * \param result     Pointer to location to receive result
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 *
 * The returned list will have its reference count increased. It is
 * the responsibility of the caller to unref the list once it has
 * finished with it.
 */
dom_exception _dom_document_get_elements_by_tag_name_ns(
		dom_document *doc, dom_string *namespace,
		dom_string *localname, dom_nodelist **result)
{
	return _dom_document_get_nodelist(doc, DOM_NODELIST_BY_NAMESPACE, 
			(dom_node_internal *) doc, NULL, namespace, localname, 
			result);
}

/**
 * Retrieve the element that matches the specified ID
 *
 * \param doc     The document to search in
 * \param id      The ID to search for
 * \param result  Pointer to location to receive result
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 *
 * The returned node will have its reference count increased. It is
 * the responsibility of the caller to unref the node once it has
 * finished with it.
 */
/* ---- Onyx: getElementById's index (document.h) ---- */

static uint32_t onyx_id_hash(dom_string *id)
{
	const uint8_t *p = (const uint8_t *) dom_string_data(id);
	size_t i, len = dom_string_byte_length(id);
	uint32_t h = 2166136261u;

	for (i = 0; i < len; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}

/* the document a node belongs to (the document itself for a document) */
static dom_document *onyx_doc_of(dom_node_internal *n)
{
	return n->type == DOM_DOCUMENT_NODE ? (dom_document *) n : n->owner;
}

/* is n in doc's tree? */
static bool onyx_in_doc(dom_node_internal *n, dom_document *doc)
{
	while (n->parent != NULL)
		n = n->parent;
	return n == &doc->base;
}

void _dom_onyx_id_unlink(struct dom_element *e)
{
	struct dom_onyx_identry *x = e->onyx_id;

	if (x == NULL)
		return;
	*x->pprev = x->next;
	if (x->next != NULL)
		x->next->pprev = x->pprev;
	x->map->count--;
	e->onyx_id = NULL;
	free(x);
}

static void onyx_id_link(struct dom_onyx_idmap *m, struct dom_onyx_identry *x)
{
	struct dom_onyx_identry **b = &m->b[x->hash & (m->nb - 1)];

	x->next = *b;
	if (*b != NULL)
		(*b)->pprev = &x->next;
	x->pprev = b;
	*b = x;
}

static void onyx_id_grow(struct dom_onyx_idmap *m)
{
	uint32_t nb = m->nb * 2, i;
	struct dom_onyx_identry **old = m->b, **b = calloc(nb, sizeof(*b));

	if (b == NULL)
		return;
	m->b = b;
	m->nb = nb;
	for (i = 0; i < nb / 2; i++) {
		struct dom_onyx_identry *x = old[i], *next;
		for (; x != NULL; x = next) {
			next = x->next;
			onyx_id_link(m, x);
		}
	}
	free(old);
}

/* e's id is id: its entry (re)made in its document's index */
static void onyx_id_put(struct dom_onyx_idmap *m, struct dom_element *e, dom_string *id)
{
	struct dom_onyx_identry *x;
	uint32_t h;

	if (id == NULL || dom_string_byte_length(id) == 0) {
		_dom_onyx_id_unlink(e);
		return;
	}
	h = onyx_id_hash(id);
	if (e->onyx_id != NULL && e->onyx_id->map == m && e->onyx_id->hash == h)
		return;
	_dom_onyx_id_unlink(e);
	x = malloc(sizeof(*x));
	if (x == NULL) {
		m->lost = true;	/* (out of memory: a miss is checked by the walk) */
		return;
	}
	x->map = m;
	x->ele = e;
	x->hash = h;
	onyx_id_link(m, x);
	e->onyx_id = x;
	if (++m->count > m->nb * 2)
		onyx_id_grow(m);
}

void _dom_onyx_id_set(struct dom_element *e, dom_string *id)
{
	dom_document *doc = e->base.owner;

	if (doc != NULL && doc->onyx_ids != NULL)
		onyx_id_put(doc->onyx_ids, e, id);
}

/* the subtree's elements indexed in m */
static void onyx_id_walk(struct dom_onyx_idmap *m, dom_node_internal *root)
{
	dom_node_internal *n = root;

	while (n != NULL) {
		if (n->type == DOM_ELEMENT_NODE) {
			dom_string *id = NULL;
			_dom_element_get_id((struct dom_element *) n, &id);
			if (id != NULL) {
				onyx_id_put(m, (struct dom_element *) n, id);
				dom_string_unref(id);
			}
		}
		if (n->first_child != NULL) {
			n = n->first_child;
			continue;
		}
		while (n != NULL && n != root && n->next == NULL)
			n = n->parent;
		if (n == NULL || n == root)
			break;
		n = n->next;
	}
}

void _dom_onyx_id_index_tree(dom_node_internal *n)
{
	dom_document *doc = onyx_doc_of(n);

	if (doc == NULL || doc->onyx_ids == NULL)
		return;
	if (n->type != DOM_ELEMENT_NODE && n->first_child == NULL)
		return;
	/* (only a subtree in the document: one outside is indexed when it is inserted) */
	if (onyx_in_doc(n, doc))
		onyx_id_walk(doc->onyx_ids, n);
}

/* the index freed (the document finalised): its elements told */
static void onyx_id_free(dom_document *doc)
{
	struct dom_onyx_idmap *m = doc->onyx_ids;
	uint32_t i;

	if (m == NULL)
		return;
	doc->onyx_ids = NULL;
	for (i = 0; i < m->nb; i++) {
		struct dom_onyx_identry *x = m->b[i], *next;
		for (; x != NULL; x = next) {
			next = x->next;
			x->ele->onyx_id = NULL;
			free(x);
		}
	}
	free(m->b);
	free(m);
}

dom_exception _dom_document_get_element_by_id(dom_document *doc,
		dom_string *id, dom_element **result)
{
	dom_node_internal *root;
	dom_exception err;
	struct dom_onyx_idmap *m = doc->onyx_ids;

	*result = NULL;

	err = dom_document_get_document_element(doc, (void *) &root);
	if (err != DOM_NO_ERR)
		return err;
	if (root == NULL)
		return DOM_NO_ERR;

	/* Onyx: the index (made at the first lookup: a walk of the tree) */
	if (m == NULL && id != NULL && dom_string_byte_length(id) > 0) {
		m = calloc(1, sizeof(*m));
		if (m != NULL) {
			m->nb = 64;
			m->b = calloc(m->nb, sizeof(*m->b));
			if (m->b == NULL) {
				free(m);
				m = NULL;
			} else {
				doc->onyx_ids = m;
				onyx_id_walk(m, &doc->base);
			}
		}
	}
	if (m != NULL && id != NULL && dom_string_byte_length(id) > 0) {
		uint32_t h = onyx_id_hash(id), found = 0;
		struct dom_onyx_identry *x;
		struct dom_element *first = NULL;

		for (x = m->b[h & (m->nb - 1)]; x != NULL; x = x->next) {
			dom_string *real = NULL;

			if (x->hash != h)
				continue;
			/* (the entry checked: that id still, in this document) */
			_dom_element_get_id(x->ele, &real);
			if (real == NULL)
				continue;
			if (dom_string_isequal(real, id) &&
			    onyx_in_doc((dom_node_internal *) x->ele, doc)) {
				if (first == NULL)
					first = x->ele;
				found++;
			}
			dom_string_unref(real);
		}
		if (found == 1 || (found == 0 && !m->lost)) {
			/* (none: no element of the document has it -- each one that has
			 * it was indexed when it got it or when it was inserted) */
			dom_node_unref(root);
			*result = first;
			if (first != NULL)
				dom_node_ref(first);
			return DOM_NO_ERR;
		}
		/* (more than one element has it: the first in the tree's order, found
		 * by the walk) */
	}

	err = _dom_find_element_by_id(root, id, result);
	dom_node_unref(root);

	if (*result != NULL)
		dom_node_ref(*result);

	return err;
}

/**
 * Retrieve the input encoding of the document
 *
 * \param doc     The document to query
 * \param result  Pointer to location to receive result
 * \return DOM_NOT_SUPPORTED_ERR, we don't support this API now.
 *
 * The returned string will have its reference count increased. It is
 * the responsibility of the caller to unref the string once it has
 * finished with it.
 */
dom_exception _dom_document_get_input_encoding(dom_document *doc,
		dom_string **result)
{
	UNUSED(doc);
	UNUSED(result);

	return DOM_NOT_SUPPORTED_ERR;
}

/**
 * Retrieve the XML encoding of the document
 *
 * \param doc     The document to query
 * \param result  Pointer to location to receive result
 * \return DOM_NOT_SUPPORTED_ERR, we don't support this API now.
 *
 * The returned string will have its reference count increased. It is
 * the responsibility of the caller to unref the string once it has
 * finished with it.
 */
dom_exception _dom_document_get_xml_encoding(dom_document *doc,
		dom_string **result)
{
	UNUSED(doc);
	UNUSED(result);

	return DOM_NOT_SUPPORTED_ERR;
}

/**
 * Retrieve the standalone status of the document
 *
 * \param doc     The document to query
 * \param result  Pointer to location to receive result
 * \return DOM_NOT_SUPPORTED_ERR, we don't support this API now.
 */
dom_exception _dom_document_get_xml_standalone(dom_document *doc,
		bool *result)
{
	UNUSED(doc);
	UNUSED(result);

	return DOM_NOT_SUPPORTED_ERR;
}

/**
 * Set the standalone status of the document
 *
 * \param doc         The document to query
 * \param standalone  Standalone status to use
 * \return DOM_NO_ERR            on success,
 *         DOM_NOT_SUPPORTED_ERR if the document does not support the "XML"
 *                               feature.
 *
 * We don't support this API now, so the return value is always 
 * DOM_NOT_SUPPORTED_ERR.
 */
dom_exception _dom_document_set_xml_standalone(dom_document *doc,
		bool standalone)
{
	UNUSED(doc);
	UNUSED(standalone);

	return DOM_NOT_SUPPORTED_ERR;
}

/**
 * Retrieve the XML version of the document
 *
 * \param doc     The document to query
 * \param result  Pointer to location to receive result
 * \return DOM_NO_ERR
 *
 * The returned string will have its reference count increased. It is
 * the responsibility of the caller to unref the string once it has
 * finished with it.
 *
 * We don't support this API now, so the return value is always 
 * DOM_NOT_SUPPORTED_ERR.
 */
dom_exception _dom_document_get_xml_version(dom_document *doc,
		dom_string **result)
{
	UNUSED(doc);
	UNUSED(result);

	return DOM_NOT_SUPPORTED_ERR;
}

/**
 * Set the XML version of the document
 *
 * \param doc      The document to query
 * \param version  XML version to use
 * \return DOM_NO_ERR            on success,
 *         DOM_NOT_SUPPORTED_ERR if the document does not support the "XML"
 *                               feature.
 *
 * We don't support this API now, so the return value is always 
 * DOM_NOT_SUPPORTED_ERR.
 */
dom_exception _dom_document_set_xml_version(dom_document *doc,
		dom_string *version)
{
	UNUSED(doc);
	UNUSED(version);

	return DOM_NOT_SUPPORTED_ERR;
}

/**
 * Retrieve the error checking mode of the document
 *
 * \param doc     The document to query
 * \param result  Pointer to location to receive result
 * \return DOM_NOT_SUPPORTED_ERR, we don't support this API now.
 */
dom_exception _dom_document_get_strict_error_checking(
		dom_document *doc, bool *result)
{
	UNUSED(doc);
	UNUSED(result);

	return DOM_NOT_SUPPORTED_ERR;
}

/**
 * Set the error checking mode of the document
 *
 * \param doc     The document to query
 * \param strict  Whether to use strict error checking
 * \return DOM_NOT_SUPPORTED_ERR, we don't support this API now.
 */
dom_exception _dom_document_set_strict_error_checking(
		dom_document *doc, bool strict)
{
	UNUSED(doc);
	UNUSED(strict);

	return DOM_NOT_SUPPORTED_ERR;
}

/**
 * Retrieve the URI of the document
 *
 * \param doc     The document to query
 * \param result  Pointer to location to receive result
 * \return DOM_NO_ERR.
 *
 * The returned string will have its reference count increased. It is
 * the responsibility of the caller to unref the string once it has
 * finished with it.
 */
dom_exception _dom_document_get_uri(dom_document *doc,
		dom_string **result)
{
	*result = dom_string_ref(doc->uri);

	return DOM_NO_ERR;
}

/**
 * Set the URI of the document
 *
 * \param doc  The document to query
 * \param uri  The URI to use
 * \return DOM_NO_ERR.
 *
 * The returned string will have its reference count increased. It is
 * the responsibility of the caller to unref the string once it has
 * finished with it.
 */
dom_exception _dom_document_set_uri(dom_document *doc,
		dom_string *uri)
{
	dom_string_unref(doc->uri);

	doc->uri = dom_string_ref(uri);

	return DOM_NO_ERR;
}

/**
 * Attempt to adopt a node from another document into this document
 *
 * \param doc     The document to adopt into
 * \param node    The node to adopt
 * \param result  Pointer to location to receive adopted node
 * \return DOM_NO_ERR                      on success,
 *         DOM_NO_MODIFICATION_ALLOWED_ERR if \p node or \p doc is readonly,
 *         DOM_NOT_SUPPORTED_ERR           if \p node is of type Document or
 *                                         DocumentType
 *
 * The returned node will have its reference count increased. It is
 * the responsibility of the caller to unref the node once it has
 * finished with it.
 *
 * @note: The spec said adoptNode may be light weight than the importNode
 *	  because the former need no Node creation. But in our implementation
 *	  this can't be ensured. Both adoptNode and importNode create new
 *	  nodes using the importing/adopting document's resource manager. So,
 *	  generally, the adoptNode and importNode call the same function
 *	  dom_document_dup_node.
 */
dom_exception _dom_document_adopt_node(dom_document *doc,
		dom_node *node, dom_node **result)
{
	dom_node_internal *n = (dom_node_internal *) node;
	dom_exception err;
	dom_node_internal *parent;
	dom_node_internal *tmp;
	
	*result = NULL;

	if (n->type == DOM_DOCUMENT_NODE ||
			n->type == DOM_DOCUMENT_TYPE_NODE) {
		return DOM_NOT_SUPPORTED_ERR;		
	}

	if (n->type == DOM_ENTITY_NODE ||
			n->type == DOM_NOTATION_NODE ||
			n->type == DOM_PROCESSING_INSTRUCTION_NODE ||
			n->type == DOM_TEXT_NODE ||
			n->type == DOM_CDATA_SECTION_NODE ||
			n->type == DOM_COMMENT_NODE) {
		*result = NULL;
		return DOM_NO_ERR;
	}

	/* Support XML when necessary */
	if (n->type == DOM_ENTITY_REFERENCE_NODE) {
		return DOM_NOT_SUPPORTED_ERR;
	}

	err = dom_document_dup_node(doc, node, true, result, DOM_NODE_ADOPTED);
	if (err != DOM_NO_ERR) {
		*result = NULL;
		return err;
	}

	parent = n->parent;
	if (parent != NULL) {
		err = dom_node_remove_child(parent, node, (void *) &tmp);
		if (err != DOM_NO_ERR) {
			dom_node_unref(*result);
			*result = NULL;
			return err;
		}
                dom_node_unref(tmp);
	}

	return DOM_NO_ERR;
}

/**
 * Retrieve the DOM configuration associated with a document
 *
 * \param doc     The document to query
 * \param result  Pointer to location to receive result
 * \return DOM_NOT_SUPPORTED_ERR, we don't support this API now.
 *
 * The returned object will have its reference count increased. It is
 * the responsibility of the caller to unref the object once it has
 * finished with it.
 */
dom_exception _dom_document_get_dom_config(dom_document *doc,
		struct dom_configuration **result)
{
	UNUSED(doc);
	UNUSED(result);

	return DOM_NOT_SUPPORTED_ERR;
}

/**
 * Normalize a document
 *
 * \param doc  The document to normalize
 * \return DOM_NOT_SUPPORTED_ERR, we don't support this API now.
 */
dom_exception _dom_document_normalize(dom_document *doc)
{
	UNUSED(doc);

	return DOM_NOT_SUPPORTED_ERR;
}

/**
 * Rename a node in a document
 *
 * \param doc        The document containing the node
 * \param node       The node to rename
 * \param namespace  The new namespace for the node
 * \param qname      The new qualified name for the node
 * \param result     Pointer to location to receive renamed node
 * \return DOM_NO_ERR                on success,
 *         DOM_INVALID_CHARACTER_ERR if \p tag_name is invalid,
 *         DOM_WRONG_DOCUMENT_ERR    if \p node was created in a different
 *                                   document
 *         DOM_NAMESPACE_ERR         if \p qname is malformed, or it has a
 *                                   prefix and \p namespace is NULL, or
 *                                   \p qname has a prefix "xml" and
 *                                   \p namespace is not
 *                                   "http://www.w3.org/XML/1998/namespace",
 *                                   or \p qname has a prefix "xmlns" and
 *                                   \p namespace is not
 *                                   "http://www.w3.org/2000/xmlns", or
 *                                   \p namespace is
 *                                   "http://www.w3.org/2000/xmlns" and
 *                                   \p qname is not (or is not prefixed by)
 *                                   "xmlns",
 *         DOM_NOT_SUPPORTED_ERR     if \p doc does not support the "XML"
 *                                   feature.
 *
 * The returned node will have its reference count increased. It is
 * the responsibility of the caller to unref the node once it has
 * finished with it.
 *
 * We don't support this API now, so the return value is always 
 * DOM_NOT_SUPPORTED_ERR.
 */
dom_exception _dom_document_rename_node(dom_document *doc,
		dom_node *node,
		dom_string *namespace, dom_string *qname,
		dom_node **result)
{
	UNUSED(doc);
	UNUSED(node);
	UNUSED(namespace);
	UNUSED(qname);
	UNUSED(result);

	return DOM_NOT_SUPPORTED_ERR;
}

dom_exception _dom_document_get_text_content(dom_node_internal *node,
					     dom_string **result)
{
	UNUSED(node);
	
	*result = NULL;
	
	return DOM_NO_ERR;
}

dom_exception _dom_document_set_text_content(dom_node_internal *node,
					     dom_string *content)
{
	UNUSED(node);
	UNUSED(content);
	
	return DOM_NO_ERR;
}

/*-----------------------------------------------------------------------*/

/* Overload protected virtual functions */

/* The virtual destroy function of this class */
void _dom_document_destroy(dom_node_internal *node)
{
	dom_document *doc = (dom_document *) node;

	if (_dom_document_finalise(doc) == true) {
		free(doc);
	}
}

/* The copy constructor function of this class */
dom_exception _dom_document_copy(dom_node_internal *old, 
		dom_node_internal **copy)
{
	UNUSED(old);
	UNUSED(copy);

	return DOM_NOT_SUPPORTED_ERR;
}


/* ----------------------------------------------------------------------- */

/* Helper functions */

/**
 * Get a nodelist, creating one if necessary
 *
 * \param doc        The document to get a nodelist for
 * \param type	     The type of the NodeList
 * \param root       Root node of subtree that list applies to
 * \param tagname    Name of nodes in list (or NULL)
 * \param namespace  Namespace part of nodes in list (or NULL)
 * \param localname  Local part of nodes in list (or NULL)
 * \param list       Pointer to location to receive list
 * \return DOM_NO_ERR on success, DOM_NO_MEM_ERR on memory exhaustion
 *
 * The returned list will have its reference count increased. It is
 * the responsibility of the caller to unref the list once it has
 * finished with it.
 */
dom_exception _dom_document_get_nodelist(dom_document *doc,
		nodelist_type type, dom_node_internal *root,
		dom_string *tagname, dom_string *namespace,
		dom_string *localname, dom_nodelist **list)
{
	struct dom_doc_nl *l;
	dom_exception err;

	for (l = doc->nodelists; l; l = l->next) {
		if (_dom_nodelist_match(l->list, type, root, tagname,
				namespace, localname))
			break;
	}

	if (l != NULL) {
		/* Found an existing list, so use it */
		dom_nodelist_ref(l->list);
	} else {
		/* No existing list */

		/* Create active list entry */
		l = malloc(sizeof(struct dom_doc_nl));
		if (l == NULL)
			return DOM_NO_MEM_ERR;

		/* Create nodelist */
		err = _dom_nodelist_create(doc, type, root, tagname, namespace,
				localname, &l->list);
		if (err != DOM_NO_ERR) {
			free(l);
			return err;
		}

		/* Add to document's list of active nodelists */
		l->prev = NULL;
		l->next = doc->nodelists;
		if (doc->nodelists)
			doc->nodelists->prev = l;
		doc->nodelists = l;
	}

	/* Note: the document does not claim a reference on the nodelist
	 * If it did, the nodelist's reference count would never reach zero,
	 * and the list would remain indefinitely. This is not a problem as
	 * the list notifies the document of its destruction via
	 * _dom_document_remove_nodelist. */

	*list = l->list;

	return DOM_NO_ERR;
}

/**
 * Remove a nodelist from a document
 *
 * \param doc   The document to remove the list from
 * \param list  The list to remove
 */
void _dom_document_remove_nodelist(dom_document *doc,
		dom_nodelist *list)
{
	struct dom_doc_nl *l;

	for (l = doc->nodelists; l; l = l->next) {
		if (l->list == list)
			break;
	}

	if (l == NULL) {
		/* This should never happen; we should probably abort here */
		return;
	}

	/* Remove from list */
	if (l->prev != NULL)
		l->prev->next = l->next;
	else
		doc->nodelists = l->next;

	if (l->next != NULL)
		l->next->prev = l->prev;

	/* And free item */
	free(l);
}

/**
 * Find element with certain ID in the subtree rooted at root 
 *
 * \param root    The root element from where we start
 * \param id      The ID of the target element
 * \param result  The result element
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception _dom_find_element_by_id(dom_node_internal *root, 
		dom_string *id, dom_element **result)
{
	dom_node_internal *node = root;

	*result = NULL;

	while (node != NULL) {
		if (node->type == DOM_ELEMENT_NODE) {
			dom_string *real_id;

			_dom_element_get_id((dom_element *) node, &real_id);
			if (real_id != NULL) {
				if (dom_string_isequal(real_id, id)) {
					dom_string_unref(real_id);
					*result = (dom_element *) node;
					return DOM_NO_ERR;
				}

				dom_string_unref(real_id);
			}
		}

		if (node->first_child != NULL) {
			/* Move to child */
			node = node->first_child;
		} else {
			while (node != NULL) {
				if (node->next != NULL) {
					/* Move to next sibling */
					node = node->next;
					break;
				} else if (node->parent != root) {
					/* Move back up to ancestors to
					 * get to next siblings */
					node = node->parent;
				} else {
					/* No more nodes below root. */
					node = NULL;
				}
			}
		}
	}

	return DOM_NO_ERR;
}

/**
 * Duplicate a Node
 *
 * \param doc     The documen
 * \param node    The node to duplicate
 * \param deep    Whether to make a deep copy
 * \param result  The returned node
 * \param opt     Whether this is adopt or import operation
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception dom_document_dup_node(dom_document *doc, dom_node *node,
		bool deep, dom_node **result, dom_node_operation opt)
{
	dom_node_internal *n = (dom_node_internal *) node;
	dom_node_internal *ret;
	dom_exception err;
	dom_node_internal *child, *r;
	dom_user_data *ud;

	if (_dom_node_readonly_owner(&doc->base) || _dom_node_readonly(n))
		return DOM_NO_MODIFICATION_ALLOWED_ERR;
	
	if (n->type == DOM_DOCUMENT_NODE ||
			n->type == DOM_DOCUMENT_TYPE_NODE)
		return DOM_NOT_SUPPORTED_ERR;

	err = dom_node_copy(node, &ret);
	if (err != DOM_NO_ERR)
		return err;

	if (n->type == DOM_ATTRIBUTE_NODE) {
		_dom_attr_set_specified((dom_attr *) node, true);
		deep = true;
	}

	if (n->type == DOM_ENTITY_REFERENCE_NODE) {
		deep = false;
	}

	if (n->type == DOM_ELEMENT_NODE) {
		/* Specified attributes are copyied but not default attributes,
		 * if the document object hold all the default attributes, we 
		 * have nothing to do here */
	}

	if (opt == DOM_NODE_ADOPTED && (n->type == DOM_ENTITY_NODE ||
			n->type == DOM_NOTATION_NODE)) {
		/* We did not support XML now */
		return DOM_NOT_SUPPORTED_ERR;
	}

	if (deep == true) {
		child = ((dom_node_internal *) node)->first_child;
		while (child != NULL) {
			err = dom_document_import_node(doc, child, deep,
					(void *) &r);
			if (err != DOM_NO_ERR) {
				dom_node_unref(ret);
				return err;
			}

			err = dom_node_append_child(ret, r, (void *) &r);
			if (err != DOM_NO_ERR) {
				dom_node_unref(ret);
				dom_node_unref(r);
				return err;
			}
			dom_node_unref(r);

			child = child->next;
		}
	}

	/* Call the dom_user_data_handlers */
	ud = n->user_data;
	while (ud != NULL) {
		if (ud->handler != NULL) {
			ud->handler(opt, ud->key, ud->data, node, 
					(dom_node *) ret);
		}
		ud = ud->next;
	}

	*result = (dom_node *) ret;

	return DOM_NO_ERR;
}

/**
 * Try to destroy the document. 
 *
 * \param doc  The instance of Document
 *
 * Delete the document if:
 * 1. The refcnt reach zero
 * 2. The pending list is empty
 *
 * else, do nothing.
 */
void _dom_document_try_destroy(dom_document *doc)
{
	if (doc->base.base.refcnt != 0 || doc->base.parent != NULL)
		return;

	_dom_document_destroy((dom_node_internal *) doc);
}

/**
 * Set the ID attribute name of this document
 *
 * \param doc   The document object
 * \param name  The ID name of the elements in this document
 */
void _dom_document_set_id_name(dom_document *doc, dom_string *name)
{
	if (doc->id_name != NULL)
		dom_string_unref(doc->id_name);
	doc->id_name = dom_string_ref(name);
	onyx_id_free(doc);	/* (Onyx: made again at the next lookup) */
}

/*-----------------------------------------------------------------------*/
/* Semi-internal API extensions for NetSurf */

dom_exception _dom_document_get_quirks_mode(dom_document *doc,
		dom_document_quirks_mode *result)
{
	*result = doc->quirks;
	return DOM_NO_ERR;
}

dom_exception _dom_document_set_quirks_mode(dom_document *doc,
		dom_document_quirks_mode quirks)
{
	doc->quirks = quirks;
	return DOM_NO_ERR;
}


/* Onyx: the document's mutation-event guard (while a mutation event is dispatched, libdom
 * refuses every change: NO_MODIFICATION_ALLOWED) set to `depth`, its old value returned -- a
 * script inserted runs from the DOMNodeInserted default action, and its own changes were all
 * refused (yahoo.com's loaders: "bad attribute") */
uint32_t dom_document_onyx_mutation_guard(dom_document *doc, uint32_t depth)
{
	uint32_t old = doc->dispatching_mutation;
	doc->dispatching_mutation = depth;
	return old;
}

/* Onyx: the subtree's nodes (and their attributes) made doc's */
static void onyx_adopt_tree(dom_node_internal *n, dom_document *doc)
{
	dom_node_internal *c;

	n->owner = doc;
	if (n->type == DOM_ELEMENT_NODE)
		_dom_element_onyx_set_owner((struct dom_element *) n, doc);
	for (c = n->first_child; c != NULL; c = c->next)
		onyx_adopt_tree(c, doc);
}

/* Onyx: the DOM's "adopt" in place -- node, out of its tree (taken out of its parent
 * first), and its subtree become doc's: the same nodes (a script keeps them), where
 * libdom's adopt_node made copies. An insertion into another document's tree adopts
 * first (google.com's HTML sanitizer appends the nodes it built into a document of
 * its own: WRONG_DOCUMENT_ERR, the suggestions were empty). */
dom_exception dom_document_onyx_adopt(dom_document *doc, dom_node *node)
{
	dom_node_internal *n = (dom_node_internal *) node;
	dom_document *old = n->owner;
	dom_exception err;

	if (n->type == DOM_DOCUMENT_NODE || n->type == DOM_DOCUMENT_TYPE_NODE ||
	    n->type == DOM_ATTRIBUTE_NODE)
		return DOM_NOT_SUPPORTED_ERR;
	if (old == doc)
		return DOM_NO_ERR;
	if (n->parent != NULL) {
		dom_node_internal *res = NULL;
		err = dom_node_remove_child(n->parent, n, (void *) &res);
		if (err != DOM_NO_ERR)
			return err;
		if (res != NULL)
			dom_node_unref(res);
	}
	/* (out of the old document's nodes pending deletion, into the new one's) */
	dom_node_ref(n);
	if (n->pending_list.prev != &n->pending_list)
		list_del(&n->pending_list);
	onyx_adopt_tree(n, doc);
	list_append(&doc->pending_nodes, &n->pending_list);
	dom_node_unref(n);
	if (old != NULL)
		_dom_document_try_destroy(old);
	return DOM_NO_ERR;
}
