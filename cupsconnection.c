/*
 * cups - Python bindings for CUPS
 * Copyright (C) 2002, 2005, 2006  Tim Waugh <twaugh@redhat.com>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "cupsconnection.h"
#include "cupsmodule.h"

PyObject *HTTPError;
PyObject *IPPError;

typedef struct
{
  PyObject_HEAD
  http_t *http;
} Connection;

static void
set_http_error (http_status_t status)
{
  PyObject *v = Py_BuildValue ("i", status);
  if (v != NULL) {
    PyErr_SetObject (HTTPError, v);
    Py_DECREF (v);
  }
}

static void
set_ipp_error (ipp_status_t status)
{
  const char *last_error;

#ifdef HAVE_CUPS_1_2
  last_error = cupsLastErrorString ();
#else
  last_error = "(not built against cups-1.2.x so no description)";
#endif

  PyObject *v = Py_BuildValue ("(is)", status, last_error);
  if (v != NULL) {
    PyErr_SetObject (IPPError, v);
    Py_DECREF (v);
  }
}

////////////////
// Connection //
////////////////

static PyObject *
Connection_new (PyTypeObject *type, PyObject *args, PyObject *kwds)
{
  Connection *self;
  self = (Connection *) type->tp_alloc (type, 0);
  if (self != NULL)
    self->http = NULL;

  return (PyObject *) self;
}

static int
Connection_init (Connection *self, PyObject *args, PyObject *kwds)
{
  static char *kwlist[] = { NULL };
  if (!PyArg_ParseTupleAndKeywords (args, kwds, "", kwlist))
    return -1;

  self->http = httpConnectEncrypt (cupsServer (),
				   ippPort (),
				   cupsEncryption ());
  if (!self->http) {
    PyErr_SetString (PyExc_RuntimeError, "httpConnectionEncrypt failed");
    return -1;
  }

  return 0;
}

static void
Connection_dealloc (Connection *self)
{
  if (self->http)
    httpClose (self->http);

  self->ob_type->tp_free ((PyObject *) self);
}

////////////////
// Connection // METHODS
////////////////

static PyObject *
do_printer_request (Connection *self, PyObject *args, ipp_op_t op)
{
  const char *name;
  char uri[HTTP_MAX_URI];
  cups_lang_t *language;
  ipp_t *request, *answer;

  if (!PyArg_ParseTuple (args, "s", &name))
    return NULL;

  request = ippNew ();
  snprintf (uri, sizeof (uri), "ipp://localhost/printers/%s", name);
  request->request.op.operation_id = op;
  request->request.op.request_id = 1;
  language = cupsLangDefault ();
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_CHARSET,
		"attributes-charset", NULL, cupsLangEncoding (language));
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE,
		"attributes-natural-language", NULL, language->language);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, uri);
  answer = cupsDoRequest (self->http, request, "/admin/");
  if (!answer || answer->request.status.status_code > IPP_OK_CONFLICT) {
    set_ipp_error (answer ?
		   answer->request.status.status_code :
		   cupsLastError ());
    if (answer)
      ippDelete (answer);
    return NULL;
  }

  ippDelete (answer);
  Py_INCREF (Py_None);
  return Py_None;
}

static PyObject *
Connection_getPrinters (Connection *self)
{
  PyObject *result;
  ipp_t *request = ippNew(), *answer;
  ipp_attribute_t *attr;
  const char *attributes[] = {
    "printer-name",
    "printer-type",
    "printer-location",
    "printer-info",
    "printer-make-and-model",
    "printer-state",
    "printer-uri",
    "device-uri",
  };
  char *lang = setlocale (LC_MESSAGES, NULL);

  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_CHARSET,
		"attributes-charset", NULL, "utf-8");
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE,
		"attributes-natural-language", NULL, lang);
  ippAddStrings (request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
		 "requested-attributes",
		 sizeof (attributes) / sizeof (attributes[0]),
		 NULL, attributes);
  request->request.op.operation_id = CUPS_GET_PRINTERS;
  request->request.op.request_id = 1;
  answer = cupsDoRequest (self->http, request, "/");
  if (!answer || answer->request.status.status_code > IPP_OK_CONFLICT) {
    set_ipp_error (answer ?
		   answer->request.status.status_code :
		   cupsLastError ());
    if (answer)
      ippDelete (answer);
    return NULL;
  }

  result = PyDict_New ();
  for (attr = answer->attrs; attr; attr = attr->next) {
    PyObject *dict;
    char *printer = NULL;

    while (attr && attr->group_tag != IPP_TAG_PRINTER)
      attr = attr->next;

    if (!attr)
      break;

    dict = PyDict_New ();
    for (; attr && attr->group_tag == IPP_TAG_PRINTER;
	 attr = attr->next) {
      PyObject *val = NULL;

      if (!strcmp (attr->name, "printer-name") &&
	  attr->value_tag == IPP_TAG_NAME)
	printer = attr->values[0].string.text;
      else if ((!strcmp (attr->name, "printer-type") ||
		!strcmp (attr->name, "printer-state")) &&
	       attr->value_tag == IPP_TAG_ENUM) {
	int ptype = attr->values[0].integer;
	val = PyInt_FromLong (ptype);
      }
      else if ((!strcmp (attr->name,
			 "printer-make-and-model") ||
		!strcmp (attr->name, "printer-info") ||
		!strcmp (attr->name, "printer-location") ||
		!strcmp (attr->name, "printer-state-message")) &&
	       attr->value_tag == IPP_TAG_TEXT) {
	val = PyString_FromString (attr->values[0].string.text);
      }
      else if (!strcmp (attr->name,
			"printer-state-reasons") &&
	       attr->value_tag == IPP_TAG_KEYWORD) {
	val = PyString_FromString (attr->values[0].string.text);
      }
      else if (!strcmp (attr->name,
			"printer-is-accepting-jobs") &&
	       attr->value_tag == IPP_TAG_BOOLEAN) {
	int b = attr->values[0].boolean;
	val = PyInt_FromLong (b);
      }
      else if ((!strcmp (attr->name,
			 "printer-up-time") ||
		!strcmp (attr->name,
			 "queued-job-count")) &&
	       attr->value_tag == IPP_TAG_INTEGER) {
	int u = attr->values[0].integer;
	val = PyInt_FromLong (u);
      }
      else if (!strcmp (attr->name, "device-uri") &&
	       attr->value_tag == IPP_TAG_URI) {
	val = PyString_FromString (attr->values[0].string.text);
      }

      if (val) {
	PyDict_SetItemString (dict, attr->name, val);
	Py_DECREF (val);
      }
    }

    if (printer) {
      PyDict_SetItemString (result, printer, dict);
    } else
      Py_DECREF (dict);

    if (!attr)
      break;
  }

  ippDelete (answer);
  return result;
}

static PyObject *
Connection_getPPDs (Connection *self)
{
  PyObject *result;
  ipp_t *request = ippNew(), *answer;
  ipp_attribute_t *attr;
  char *lang = setlocale (LC_MESSAGES, NULL);

  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_CHARSET,
		"attributes-charset", NULL, "utf-8");
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE,
		"attributes-natural-language", NULL, lang);
  request->request.op.operation_id = CUPS_GET_PPDS;
  request->request.op.request_id = 1;
  answer = cupsDoRequest (self->http, request, "/");
  if (!answer || answer->request.status.status_code > IPP_OK_CONFLICT) {
    set_ipp_error (answer ?
		   answer->request.status.status_code :
		   cupsLastError ());
    if (answer)
      ippDelete (answer);
    return NULL;
  }

  result = PyDict_New ();
  for (attr = answer->attrs; attr; attr = attr->next) {
    PyObject *dict;
    char *ppdname = NULL;

    while (attr && attr->group_tag != IPP_TAG_PRINTER)
      attr = attr->next;

    if (!attr)
      break;

    dict = PyDict_New ();
    for (; attr && attr->group_tag == IPP_TAG_PRINTER;
	 attr = attr->next) {
      PyObject *val = NULL;

      if (!strcmp (attr->name, "ppd-name") &&
	  attr->value_tag == IPP_TAG_NAME)
	ppdname = attr->values[0].string.text;
      else if ((!strcmp (attr->name, "ppd-natural-language") &&
		attr->value_tag == IPP_TAG_LANGUAGE) ||
	       (!strcmp (attr->name, "ppd-make-and-model") &&
		attr->value_tag == IPP_TAG_TEXT) ||
	       (!strcmp (attr->name, "ppd-device-id") &&
		attr->value_tag == IPP_TAG_TEXT))
	val = PyString_FromString (attr->values[0].string.text);

      if (val) {
	PyDict_SetItemString (dict, attr->name, val);
	Py_DECREF (val);
      }
    }

    if (ppdname) {
      PyDict_SetItemString (result, ppdname, dict);
    } else
      Py_DECREF (dict);

    if (!attr)
      break;
  }

  ippDelete (answer);
  return result;
}

static PyObject *
Connection_getDevices (Connection *self)
{
  PyObject *result;
  ipp_t *request = ippNew(), *answer;
  ipp_attribute_t *attr;
  char *lang = setlocale (LC_MESSAGES, NULL);

  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_CHARSET,
		"attributes-charset", NULL, "utf-8");
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE,
		"attributes-natural-language", NULL, lang);
  request->request.op.operation_id = CUPS_GET_DEVICES;
  request->request.op.request_id = 1;
  answer = cupsDoRequest (self->http, request, "/");
  if (!answer || answer->request.status.status_code > IPP_OK_CONFLICT) {
    set_ipp_error (answer ?
		   answer->request.status.status_code :
		   cupsLastError ());
    if (answer)
      ippDelete (answer);
    return NULL;
  }

  result = PyDict_New ();
  for (attr = answer->attrs; attr; attr = attr->next) {
    PyObject *dict;
    char *device_uri = NULL;

    while (attr && attr->group_tag != IPP_TAG_PRINTER)
      attr = attr->next;

    if (!attr)
      break;

    dict = PyDict_New ();
    for (; attr && attr->group_tag == IPP_TAG_PRINTER;
	 attr = attr->next) {
      PyObject *val = NULL;

      if (!strcmp (attr->name, "device-uri") &&
	  attr->value_tag == IPP_TAG_URI)
	device_uri = attr->values[0].string.text;
      else if ((!strcmp (attr->name, "device-class") &&
		attr->value_tag == IPP_TAG_KEYWORD) ||
	       (!strcmp (attr->name, "device-make-and-model") &&
		attr->value_tag == IPP_TAG_TEXT) ||
	       (!strcmp (attr->name, "device-info") &&
		attr->value_tag == IPP_TAG_TEXT) ||
	       (!strcmp (attr->name, "device-id") &&
		attr->value_tag == IPP_TAG_TEXT))
	val = PyString_FromString (attr->values[0].string.text);

      if (val) {
	PyDict_SetItemString (dict, attr->name, val);
	Py_DECREF (val);
      }
    }

    if (device_uri) {
      PyDict_SetItemString (result, device_uri, dict);
    } else
      Py_DECREF (dict);

    if (!attr)
      break;
  }

  ippDelete (answer);
  return result;
}

static PyObject *
Connection_getFile (Connection *self, PyObject *args)
{
  const char *resource, *filename;
  http_status_t status;

  if (!PyArg_ParseTuple (args, "ss", &resource, &filename))
    return NULL;

  status = cupsGetFile (self->http, resource, filename);
  if (status != HTTP_OK) {
    set_http_error (status);
    return NULL;
  }

  Py_INCREF (Py_None);
  return Py_None;
}

static PyObject *
Connection_putFile (Connection *self, PyObject *args)
{
  const char *resource, *filename;
  http_status_t status;

  if (!PyArg_ParseTuple (args, "ss", &resource, &filename))
    return NULL;

  status = cupsPutFile (self->http, resource, filename);
  if (status != HTTP_OK && status != HTTP_CREATED) {
    set_http_error (status);
    return NULL;
  }

  Py_INCREF (Py_None);
  return Py_None;
}

static PyObject *
Connection_addPrinter (Connection *self, PyObject *args, PyObject *kwds)
{
  const char *name;
  const char *ppdfile = NULL;
  const char *ppdname = NULL;
  const char *info = NULL;
  const char *location = NULL;
  char uri[HTTP_MAX_URI];
  cups_lang_t *language;
  ipp_t *request, *answer;
  static char *kwlist[] = { "name", "filename", "ppdname", "info",
			    "location", NULL };

  if (!PyArg_ParseTupleAndKeywords (args, kwds, "s|ssss", kwlist,
				    &name, &ppdfile, &ppdname, &info,
				    &location))
    return NULL;

  if (ppdfile && ppdname) {
    PyErr_SetString (PyExc_RuntimeError,
		     "Only filename or ppdname can be specified, not both");
    return NULL;
  }

  request = ippNew ();
  snprintf (uri, sizeof (uri), "ipp://localhost/printers/%s", name);
  request->request.op.operation_id = CUPS_ADD_MODIFY_PRINTER;
  request->request.op.request_id = 1;
  language = cupsLangDefault ();
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_CHARSET,
		"attributes-charset", NULL, cupsLangEncoding (language));
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE,
		"attributes-natural-language", NULL, language->language);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, uri);

  if (ppdname)
    ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		  "ppd-name", NULL, ppdname);

  if (info)
    ippAddString (request, IPP_TAG_PRINTER, IPP_TAG_TEXT,
		  "printer-info", NULL, info);

  if (location)
    ippAddString (request, IPP_TAG_PRINTER, IPP_TAG_TEXT,
		  "printer-location", NULL, location);
  
  if (ppdfile)
    answer = cupsDoFileRequest (self->http, request, "/admin/", ppdfile);
  else
    answer = cupsDoRequest (self->http, request, "/admin/");

  if (!answer || answer->request.status.status_code > IPP_OK_CONFLICT) {
    set_ipp_error (answer ?
		   answer->request.status.status_code :
		   cupsLastError ());
    if (answer)
      ippDelete (answer);
    return NULL;
  }

  ippDelete (answer);
  Py_INCREF (Py_None);
  return Py_None;
}

static PyObject *
Connection_setPrinterDevice (Connection *self, PyObject *args)
{
  const char *name;
  const char *device_uri;
  char uri[HTTP_MAX_URI];
  cups_lang_t *language;
  ipp_t *request, *answer;

  if (!PyArg_ParseTuple (args, "ss", &name, &device_uri))
    return NULL;

  request = ippNew ();
  snprintf (uri, sizeof (uri), "ipp://localhost/printers/%s", name);
  request->request.op.operation_id = CUPS_ADD_PRINTER;
  request->request.op.request_id = 1;
  language = cupsLangDefault ();
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_CHARSET,
		"attributes-charset", NULL, cupsLangEncoding (language));
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE,
		"attributes-natural-language", NULL, language->language);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, uri);
  ippAddString (request, IPP_TAG_PRINTER, IPP_TAG_URI,
		"device-uri", NULL, device_uri);
  answer = cupsDoRequest (self->http, request, "/admin/");
  if (!answer || answer->request.status.status_code > IPP_OK_CONFLICT) {
    set_ipp_error (answer ?
		   answer->request.status.status_code :
		   cupsLastError ());
    if (answer)
      ippDelete (answer);
    return NULL;
  }

  ippDelete (answer);
  Py_INCREF (Py_None);
  return Py_None;
}

static PyObject *
Connection_deletePrinter (Connection *self, PyObject *args)
{
  return do_printer_request (self, args, CUPS_DELETE_PRINTER);
}

static PyObject *
Connection_addPrinterToClass (Connection *self, PyObject *args)
{
  const char *printername;
  const char *classname;
  char classuri[HTTP_MAX_URI];
  char printeruri[HTTP_MAX_URI];
  cups_lang_t *language;
  ipp_t *request, *answer;

  if (!PyArg_ParseTuple (args, "ss", &printername, &classname))
    return NULL;

  // Does the class exist, and is the printer already in it?
  request = ippNew ();
  snprintf (classuri, sizeof (classuri),
	    "ipp://localhost/classes/%s", classname);
  request->request.op.operation_id = IPP_GET_PRINTER_ATTRIBUTES;
  request->request.op.request_id = 1;
  language = cupsLangDefault ();
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_CHARSET,
		"attributes-charset", NULL, cupsLangEncoding (language));
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE,
		"attributes-natural-language", NULL, language->language);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, classuri);
  answer = cupsDoRequest (self->http, request, "/");
  if (answer) {
    ipp_attribute_t *printers;
    printers = ippFindAttribute (answer, "member-names", IPP_TAG_NAME);
    if (printers) {
      int i;
      for (i = 0; i < printers->num_values; i++) {
	if (!strcasecmp (printers->values[i].string.text, printername)) {
	  ippDelete (answer);
	  PyErr_SetString (PyExc_RuntimeError, "Printer already in class");
	  return NULL;
	}
      }
    }
  }

  request = ippNew ();
  request->request.op.operation_id = CUPS_ADD_CLASS;
  request->request.op.request_id = 1;
  language = cupsLangDefault ();
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_CHARSET,
		"attributes-charset", NULL, cupsLangEncoding (language));
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE,
		"attributes-natural-language", NULL, language->language);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, classuri);
  snprintf (printeruri, sizeof (printeruri),
	    "ipp://localhost/printers/%s", printername);
  if (answer) {
    ipp_attribute_t *printers;
    printers = ippFindAttribute (answer, "member-uris", IPP_TAG_URI);
    if (printers) {
      ipp_attribute_t *attr;
      int i;
      attr = ippAddStrings (request, IPP_TAG_PRINTER, IPP_TAG_URI,
			    "member-uris", printers->num_values + 1,
			    NULL, NULL);
      for (i = 0; i < printers->num_values; i++)
	attr->values[i].string.text = strdup (printers->values[i].string.text);
      attr->values[printers->num_values].string.text = strdup (printeruri);
    }

    ippDelete (answer);
  }

  // If the class didn't exist, create a new one.
  if (!ippFindAttribute (request, "member-uris", IPP_TAG_URI))
    ippAddString (request, IPP_TAG_PRINTER, IPP_TAG_URI,
		  "member-uris", NULL, printeruri);

  answer = cupsDoRequest (self->http, request, "/admin/");
  if (!answer || answer->request.status.status_code > IPP_OK_CONFLICT) {
    set_ipp_error (answer ?
		   answer->request.status.status_code :
		   cupsLastError ());
    if (answer)
      ippDelete (answer);
    return NULL;
  }

  ippDelete (answer);
  Py_INCREF (Py_None);
  return Py_None;
}

static PyObject *
Connection_deletePrinterFromClass (Connection *self, PyObject *args)
{
  const char *printername;
  const char *classname;
  char classuri[HTTP_MAX_URI];
  cups_lang_t *language;
  ipp_t *request, *answer;
  ipp_attribute_t *printers;
  int i;

  if (!PyArg_ParseTuple (args, "ss", &printername, &classname))
    return NULL;

  // Does the class exist, and is the printer in it?
  request = ippNew ();
  snprintf (classuri, sizeof (classuri),
	    "ipp://localhost/classes/%s", classname);
  request->request.op.operation_id = IPP_GET_PRINTER_ATTRIBUTES;
  request->request.op.request_id = 1;
  language = cupsLangDefault ();
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_CHARSET,
		"attributes-charset", NULL, cupsLangEncoding (language));
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE,
		"attributes-natural-language", NULL, language->language);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, classuri);
  answer = cupsDoRequest (self->http, request, "/");
  if (!answer) {
    set_ipp_error (cupsLastError ());
    return NULL;
  }

  printers = ippFindAttribute (answer, "member-names", IPP_TAG_NAME);
  for (i = 0; printers && i < printers->num_values; i++)
    if (!strcasecmp (printers->values[i].string.text, printername))
      break;

  if (!printers || i == printers->num_values) {
    ippDelete (answer);
    PyErr_SetString (PyExc_RuntimeError, "Printer not in class");
    return NULL;
  }

  request = ippNew ();
  request->request.op.request_id = 1;
  language = cupsLangDefault ();
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_CHARSET,
		"attributes-charset", NULL, cupsLangEncoding (language));
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE,
		"attributes-natural-language", NULL, language->language);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, classuri);

  // Only printer in class?  Delete the class.
  if (printers->num_values == 1)
    request->request.op.operation_id = CUPS_DELETE_CLASS;
  else {
    // Trim the printer from the list.
    ipp_attribute_t *newlist;
    int j;
    request->request.op.operation_id = CUPS_ADD_CLASS;
    printers = ippFindAttribute (answer, "member-uris", IPP_TAG_URI);
    newlist = ippAddStrings (request, IPP_TAG_PRINTER, IPP_TAG_URI,
			     "member-uris", printers->num_values - 1,
			     NULL, NULL);
    for (j = 0; j < i; j++)
      newlist->values[j].string.text =
	strdup (printers->values[j].string.text);
    for (j = i; j < newlist->num_values; j++)
      newlist->values[j].string.text =
	strdup (printers->values[j + 1].string.text);
  }

  ippDelete (answer);
  answer = cupsDoRequest (self->http, request, "/admin/");
  if (!answer || answer->request.status.status_code > IPP_OK_CONFLICT) {
    set_ipp_error (answer ?
		   answer->request.status.status_code :
		   cupsLastError ());
    if (answer)
      ippDelete (answer);
    return NULL;
  }

  ippDelete (answer);
  Py_INCREF (Py_None);
  return Py_None;
}

static PyObject *
Connection_enablePrinter (Connection *self, PyObject *args)
{
  return do_printer_request (self, args, IPP_ENABLE_PRINTER);
}

static PyObject *
Connection_disablePrinter (Connection *self, PyObject *args)
{
  return do_printer_request (self, args, IPP_DISABLE_PRINTER);
}

static PyObject *
Connection_acceptJobs (Connection *self, PyObject *args)
{
  return do_printer_request (self, args, CUPS_ACCEPT_JOBS);
}

static PyObject *
Connection_rejectJobs (Connection *self, PyObject *args)
{
  return do_printer_request (self, args, CUPS_REJECT_JOBS);
}

static PyObject *
Connection_setDefault (Connection *self, PyObject *args)
{
  return do_printer_request (self, args, CUPS_SET_DEFAULT);
}

static PyObject *
Connection_getPPD (Connection *self, PyObject *args)
{
  PyObject *ret;
  const char *printer;
  const char *ppdfile;

  if (!PyArg_ParseTuple (args, "s", &printer))
    return NULL;

  ppdfile = cupsGetPPD2 (self->http, printer);
  if (!ppdfile) {
    set_ipp_error (cupsLastError ());
    return NULL;
  }

  ret = PyString_FromString (ppdfile);
  return ret;
}

PyMethodDef Connection_methods[] =
  {
    { "getPrinters",
      (PyCFunction) Connection_getPrinters, METH_NOARGS,
      "Returns a dict, indexed by name, of dicts representing\n"
      "queues, indexed by attribute." },

    { "getPPDs",
      (PyCFunction) Connection_getPPDs, METH_NOARGS,
      "Returns a dict, indexed by PPD name, of dicts representing\n"
      "PPDs, indexed by attribute." },

    { "getDevices",
      (PyCFunction) Connection_getDevices, METH_NOARGS,
      "Returns a dict, indexed by device URI, of dicts representing\n"
      "devices, indexed by attribute." },

    { "getFile",
      (PyCFunction) Connection_getFile, METH_VARARGS,
      "getFile(resource, filename) -> None" },

    { "putFile",
      (PyCFunction) Connection_putFile, METH_VARARGS,
      "putFile(resource, filename) -> None" },

    { "addPrinter",
      (PyCFunction) Connection_addPrinter, METH_VARARGS | METH_KEYWORDS,
      "addPrinter(name, filename=None, ppdname=None, info=None,\n"
      "location=None) -> None" },

    { "deletePrinter",
      (PyCFunction) Connection_deletePrinter, METH_VARARGS,
      "deletePrinter(name, ppdfile, device_uri) -> None" },

    { "addPrinterToClass",
      (PyCFunction) Connection_addPrinterToClass, METH_VARARGS,
      "addPrinterToClass(name, class) -> None" },

    { "deletePrinterFromClass",
      (PyCFunction) Connection_deletePrinterFromClass, METH_VARARGS,
      "deletePrinterFromClass(name, class) -> None" },

    { "setDefault",
      (PyCFunction) Connection_setDefault, METH_VARARGS,
      "setDefault(name) -> None" },

    { "setPrinterDevice",
      (PyCFunction) Connection_setPrinterDevice, METH_VARARGS,
      "setPrinterDevice(name, device_uri) -> None" },

    { "getPPD",
      (PyCFunction) Connection_getPPD, METH_VARARGS,
      "Returns PPD file name" },

    { "enablePrinter",
      (PyCFunction) Connection_enablePrinter, METH_VARARGS,
      "Enables named printer." },

    { "disablePrinter",
      (PyCFunction) Connection_disablePrinter, METH_VARARGS,
      "Disables named printer." },

    { "acceptJobs",
      (PyCFunction) Connection_acceptJobs, METH_VARARGS,
      "Causes named printer to accept jobs." },

    { "rejectJobs",
      (PyCFunction) Connection_rejectJobs, METH_VARARGS,
      "Causes named printer to reject jobs." },

    { NULL } /* Sentinel */
  };

PyTypeObject cups_ConnectionType =
  {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "cups.Connection",         /*tp_name*/
    sizeof(Connection),        /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)Connection_dealloc, /*tp_dealloc*/
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
    "CUPS Connection",         /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    Connection_methods,        /* tp_methods */
    0,                         /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)Connection_init, /* tp_init */
    0,                         /* tp_alloc */
    Connection_new,            /* tp_new */
  };
