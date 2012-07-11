/*
 * cups - Python bindings for CUPS
 * Copyright (C) 2002, 2005, 2006, 2007, 2008, 2009, 2010, 2011  Red Hat, Inc.
 * Author: Tim Waugh <twaugh@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "cupsipp.h"
#include "cupsmodule.h"

/* Compatibility code for older (pre-2.5) versions of Python */
#if PY_VERSION_HEX < 0x02050000
typedef int Py_ssize_t;
#define PY_SSIZE_T_MAX INT_MAX
#define PY_SSIZE_T_MIN INT_MIN
#endif

//////////////////
// IPPAttribute //
//////////////////

static PyObject *
IPPAttribute_new (PyTypeObject *type, PyObject *args, PyObject *kwds)
{
  IPPAttribute *self;
  self = (IPPAttribute *) type->tp_alloc (type, 0);
  if (self != NULL) {
    self->group_tag = IPP_TAG_ZERO;
    self->value_tag = IPP_TAG_ZERO;
    self->name = NULL;
    self->values = NULL;
  }

  return (PyObject *) self;
}

static int
IPPAttribute_init (IPPAttribute *self, PyObject *args, PyObject *kwds)
{
  ipp_tag_t group_tag, value_tag;
  const char *name;
  PyObject *value = NULL;
  PyObject *list = NULL;

  if (!PyArg_ParseTuple (args, "iis|O", &group_tag, &value_tag, &name,
			 &value))
    return -1;

  if (value == NULL) {
    switch (value_tag) {
    case IPP_TAG_ZERO:
    case IPP_TAG_NOVALUE:
    case IPP_TAG_NOTSETTABLE:
    case IPP_TAG_ADMINDEFINE:
      break;

    default:
      PyErr_SetString (PyExc_RuntimeError, "missing value list");
      return -1;
    }
  } else {
    size_t i, n;
    int valid = 1;

    if (PyList_Check (value)) {
      list = value;
      Py_INCREF (value);
      n = PyList_Size (value);
    } else {
      list = PyList_New (0);
      PyList_Append (list, value);
      n = 1;
    }

    for (i = 0; valid && i < n; i++) {
      PyObject *v = PyList_GetItem (list, i); // borrowed
      switch (value_tag) {
      case IPP_TAG_INTEGER:
      case IPP_TAG_ENUM:
      case IPP_TAG_RANGE:
	valid = PyInt_Check (v);
	break;

      case IPP_TAG_BOOLEAN:
	valid = PyBool_Check (v);
	break;

      case IPP_TAG_TEXT:
	valid = PyUnicode_Check (v);
	break;

      case IPP_TAG_NAME:
      case IPP_TAG_KEYWORD:
      case IPP_TAG_URI:
      case IPP_TAG_MIMETYPE:
      case IPP_TAG_CHARSET:
      case IPP_TAG_LANGUAGE:
	valid = PyString_Check (v);
	break;

      default:
	valid = 0;
      }
    }

    if (!valid) {
      PyErr_SetString (PyExc_RuntimeError, "invalid value");
      Py_DECREF (list);
      return -1;
    }
  }

  self->group_tag = group_tag;
  self->value_tag = value_tag;
  self->values = list;
  self->name = strdup (name);
  return 0;
}

static void
IPPAttribute_dealloc (IPPAttribute *self)
{
  Py_XDECREF (self->values);
  if (self->name)
    free (self->name);

  self->ob_type->tp_free ((PyObject *) self);
}

static PyObject *
IPPAttribute_repr (IPPAttribute *self)
{
  PyObject *ret;
  PyObject *values_repr = NULL;
  char *values = NULL;
  if (self->values) {
    values_repr = PyList_Type.tp_repr (self->values);
    values = PyString_AsString (values_repr);
  }
  ret = PyString_FromFormat ("<cups.IPPAttribute %s (%d:%d)%s%s>",
			     self->name, self->group_tag, self->value_tag,
			     values ? ": " : "",
			     values ? values : "");
  Py_XDECREF (values_repr);
  return ret;
}

//////////////////
// IPPAttribute // ATTRIBUTES
//////////////////

static PyObject *
IPPAttribute_getGroupTag (IPPAttribute *self, void *closure)
{
  return PyInt_FromLong (self->group_tag);
}

static PyObject *
IPPAttribute_getValueTag (IPPAttribute *self, void *closure)
{
  return PyInt_FromLong (self->value_tag);
}

static PyObject *
IPPAttribute_getName (IPPAttribute *self, void *closure)
{
  return PyString_FromString (self->name);
}

static PyObject *
IPPAttribute_getValues (IPPAttribute *self, void *closure)
{
  Py_INCREF (self->values);
  return self->values;
}

PyGetSetDef IPPAttribute_getseters[] =
  {
    { "group_tag",
      (getter) IPPAttribute_getGroupTag, (setter) NULL,
      "IPP group tag", NULL },

    { "value_tag",
      (getter) IPPAttribute_getValueTag, (setter) NULL,
      "IPP value tag", NULL },

    { "name",
      (getter) IPPAttribute_getName, (setter) NULL,
      "IPP attribute name", NULL },

    { "values",
      (getter) IPPAttribute_getValues, (setter) NULL,
      "List of values" },

    { NULL }, /* Sentinel */
  };

PyMethodDef IPPAttribute_methods[] =
  {
    { NULL } /* Sentinel */
  };

PyTypeObject cups_IPPAttributeType =
  {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "cups.IPPAttribute",       /*tp_name*/
    sizeof(IPPAttribute),      /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)IPPAttribute_dealloc, /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    (reprfunc)IPPAttribute_repr, /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,        /*tp_flags*/
    "IPP Attribute\n"
    "=============\n"
    "  An IPP attribute.\n\n"
    "@type group_tag: int\n"
    "@ivar group_tag: IPP group tag\n"
    "@type value_tag: int\n"
    "@ivar value_tag: IPP value tag\n"
    "@type values: list\n"
    "@ivar value: list of values\n"
    "",                        /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    IPPAttribute_methods,      /* tp_methods */
    0,                         /* tp_members */
    IPPAttribute_getseters,    /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)IPPAttribute_init, /* tp_init */
    0,                         /* tp_alloc */
    IPPAttribute_new,          /* tp_new */
  };

////////////////
// IPPRequest //
////////////////

static PyObject *
IPPRequest_new (PyTypeObject *type, PyObject *args, PyObject *kwds)
{
  IPPRequest *self;
  self = (IPPRequest *) type->tp_alloc (type, 0);
  if (self != NULL)
    self->ipp = NULL;

  return (PyObject *) self;
}

static int
IPPRequest_init (IPPRequest *self, PyObject *args, PyObject *kwds)
{
  unsigned long op = -1;
  if (!PyArg_ParseTuple (args, "|i", &op))
    return -1;

  if (op == -1)
    self->ipp = ippNew ();
  else
    self->ipp = ippNewRequest (op);

  return 0;
}

static void
IPPRequest_dealloc (IPPRequest *self)
{
  ippDelete (self->ipp);
  self->ob_type->tp_free ((PyObject *) self);
}

static ssize_t
cupsipp_iocb_read (PyObject *callable, ipp_uchar_t *buffer, size_t len)
{
  PyObject *args = Py_BuildValue ("(i)", len);
  PyObject *result;
  Py_ssize_t got = -1;
  char *gotbuffer;

  debugprintf ("-> cupsipp_iocb_read\n");

  if (!args) {
    debugprintf ("Py_BuildValue failed\n");
    goto out;
  }

  result = PyEval_CallObject (callable, args);
  Py_DECREF (args);

  if (result == NULL) {
    debugprintf ("Exception in read callback\n");
    goto out;
  }

  if (PyString_Check (result)) {
    PyString_AsStringAndSize (result, &gotbuffer, &got);
    if (got > len) {
      debugprintf ("More data returned than requested!  Truncated...\n");
      got = len;
    }

    memcpy (buffer, gotbuffer, got);
  } else {
    debugprintf ("Unknown result object type!\n");
  }

  Py_DECREF (result);

 out:
  debugprintf ("<- cupsipp_iocb_read() == %zd\n", got);
  return got;
}

static PyObject *
IPPRequest_readIO (IPPRequest *self, PyObject *args, PyObject *kwds)
{
  PyObject *cb;
  char blocking = 1;
  static char *kwlist[] = { "read_fn", "blocking", NULL };

  if (!PyArg_ParseTupleAndKeywords (args, kwds, "O|b", kwlist,
				    &cb, &blocking))
    return NULL;

  if (!PyCallable_Check (cb)) {
    PyErr_SetString (PyExc_TypeError, "Parameter must be callable");
    return NULL;
  }

  return PyInt_FromLong (ippReadIO (cb,
				    (ipp_iocb_t) cupsipp_iocb_read,
				    blocking, NULL, self->ipp));
}

static PyObject *
IPPRequest_getAttributes (IPPRequest *self, void *closure)
{
  PyObject *attrs = PyList_New (0);
  ipp_attribute_t *attr;
  for (attr = ippFirstAttribute (self->ipp); attr; attr = ippNextAttribute(self->ipp))
    {
      PyObject *largs = NULL;
      PyObject *lkwlist = NULL;
      PyObject *values = NULL;
      IPPAttribute *attribute = NULL;

      debugprintf ("%s: ", ippGetName (attr));
      if (ippGetValueTag (attr) == IPP_TAG_ZERO ||
	  ippGetValueTag (attr) == IPP_TAG_NOVALUE ||
	  ippGetValueTag (attr) == IPP_TAG_NOTSETTABLE ||
	  ippGetValueTag (attr) == IPP_TAG_ADMINDEFINE) {
	debugprintf ("no value\n");
      } else {
	PyObject *value = NULL;
	int i;
	int unknown_value_tag = 0;

	values = PyList_New (0);
	if (!values)
	  goto fail_add;

	for (i = 0; i < ippGetCount (attr); i++) {
	  switch (ippGetValueTag (attr)) {
	  case IPP_TAG_INTEGER:
	  case IPP_TAG_ENUM:
	  case IPP_TAG_RANGE:
	    value = PyInt_FromLong (ippGetInteger (attr, i));
	    debugprintf ("i%d", ippGetInteger (attr, i));
	    break;

	  case IPP_TAG_BOOLEAN:
	    value = PyBool_FromLong (ippGetBoolean (attr, i));
	    debugprintf ("b%d", ippGetInteger (attr, i));
	    break;

	  case IPP_TAG_TEXT:
	    value = PyUnicode_Decode (ippGetString (attr, i, NULL),
				      strlen (ippGetString (attr, i, NULL)),
				      "utf-8", NULL);
	    debugprintf ("u%s", ippGetString (attr, i, NULL));
	    break;

	  case IPP_TAG_NAME:
	  case IPP_TAG_KEYWORD:
	  case IPP_TAG_URI:
	  case IPP_TAG_MIMETYPE:
	  case IPP_TAG_CHARSET:
	  case IPP_TAG_LANGUAGE:
	    value = PyString_FromString (ippGetString (attr, i, NULL));
	    debugprintf ("s%s", ippGetString (attr, i, NULL));
	    break;

	  default:
	    value = NULL;
	    unknown_value_tag = 1;
	    debugprintf ("Unable to encode value tag %d\n", ippGetValueTag (attr));
	  }

	  if (!value)
	    break; /* out of values loop */

	  debugprintf ("(%p), ", value);
	  if (PyList_Append (values, value) != 0) {
	    Py_DECREF (values);
	    Py_DECREF (value);
	    goto fail_add;
	  }

	  Py_DECREF (value);
	}

	if (unknown_value_tag) {
	  Py_DECREF (values);

	  /* Next attribute */
	  continue;
	}

	if (!value)
	  /* Failed to build object */
	  goto fail_add;

	debugprintf ("\n");
      }

      if (values) {
	largs = Py_BuildValue ("(iisO)",
			       ippGetGroupTag (attr),
			       ippGetValueTag (attr),
			       ippGetName (attr),
			       values);
	Py_DECREF (values);
	values = NULL;
      } else
	largs = Py_BuildValue ("(iis)", ippGetGroupTag (attr), ippGetValueTag (attr),
			       ippGetName (attr) ? ippGetName (attr) : "");

      if (!largs)
	goto fail_add;

      lkwlist = Py_BuildValue ("{}");
      if (!lkwlist)
	goto fail_add;

      attribute = (IPPAttribute *) PyType_GenericNew (&cups_IPPAttributeType,
						      largs, lkwlist);
      if (!attribute)
	goto fail_add;

      if (IPPAttribute_init (attribute, largs, lkwlist) != 0)
	goto fail_add;

      if (PyList_Append (attrs, (PyObject *) attribute) != 0)
	goto fail_add;

      Py_DECREF (largs);
      Py_DECREF (lkwlist);
      Py_DECREF (attribute);
      continue;

    fail_add:
      if (values)
	Py_DECREF (values);

      if (largs)
	Py_DECREF (largs);

      if (lkwlist)
	Py_DECREF (lkwlist);

      if (attribute)
	Py_DECREF (attribute);

      debugprintf ("\nException creating new object\n");
      goto fail_out;
    }

  return attrs;

 fail_out:
  Py_DECREF (attrs);
  return NULL;
}

PyGetSetDef IPPRequest_getseters[] =
  {
    { "attributes",
      (getter) IPPRequest_getAttributes, (setter) NULL,
      "IPP request attributes", NULL },

    { NULL } /* Sentinel */
  };

PyMethodDef IPPRequest_methods[] =
  {
    { "readIO",
      (PyCFunction) IPPRequest_readIO, METH_VARARGS | METH_KEYWORDS,
      "readIO(read_fn, blocking=True) -> IPP state\n\n"
      "@type read_fn: Callable function\n"
      "@param read_fn: A callback, taking a single integer argument for\n"
      "'size', for reading IPP data\n"
      "@type blocking: Boolean\n"
      "@param blocking: whether to continue reading until a complete\n"
      "request is read\n"
      "@return: IPP state value" },

    { NULL } /* Sentinel */
  };

PyTypeObject cups_IPPRequestType =
  {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "cups.IPPRequest",         /*tp_name*/
    sizeof(IPPRequest),        /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)IPPRequest_dealloc, /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,        /*tp_flags*/
    "IPP Request\n"
    "===========\n"
    "  An IPP request.  The constructor takes an optional argument of the\n"
    "operation code.\n\n"
    "",                        /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    IPPRequest_methods,        /* tp_methods */
    0,                         /* tp_members */
    IPPRequest_getseters,      /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)IPPRequest_init, /* tp_init */
    0,                         /* tp_alloc */
    IPPRequest_new,          /* tp_new */
  };
