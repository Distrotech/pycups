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
#include "cupsppd.h"
#include "cupsmodule.h"

#include <paths.h>
#include <stdlib.h>
#include <string.h>

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

  last_error = ippErrorString (status);

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

  Py_BEGIN_ALLOW_THREADS;
  self->http = httpConnectEncrypt (cupsServer (),
				   ippPort (),
				   cupsEncryption ());
  Py_END_ALLOW_THREADS;

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
  ipp_t *request, *answer;

  if (!PyArg_ParseTuple (args, "s", &name))
    return NULL;

  request = ippNewRequest (op);
  snprintf (uri, sizeof (uri), "ipp://localhost/printers/%s", name);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, uri);
  answer = cupsDoRequest (self->http, request, "/admin/");
  if (PyErr_Occurred ()) {
    if (answer)
      ippDelete (answer);
    return NULL;
  }

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
  ipp_t *request = ippNewRequest(CUPS_GET_PRINTERS), *answer;
  ipp_attribute_t *attr;
  const char *attributes[] = {
    "printer-name",
    "printer-type",
    "printer-location",
    "printer-info",
    "printer-make-and-model",
    "printer-state",
    "printer-uri-supported",
    "device-uri",
    "printer-is-shared",
  };

  ippAddStrings (request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
		 "requested-attributes",
		 sizeof (attributes) / sizeof (attributes[0]),
		 NULL, attributes);
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
      else if ((!strcmp (attr->name, "device-uri") ||
		!strcmp (attr->name, "printer-uri-supported")) &&
	       attr->value_tag == IPP_TAG_URI) {
	val = PyString_FromString (attr->values[0].string.text);
      }
      else if (!strcmp (attr->name, "printer-is-shared") &&
	       attr->value_tag == IPP_TAG_BOOLEAN) {
	val = PyBool_FromLong (attr->values[0].boolean);
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
build_list_from_attribute_strings (ipp_attribute_t *attr)
{
  PyObject *list = PyList_New (0);
  int i;
  for (i = 0; i < attr->num_values; i++) {
    PyObject *val = PyString_FromString (attr->values[i].string.text);
    PyList_Append (list, val);
  }
  return list;
}

static PyObject *
Connection_getClasses (Connection *self)
{
  PyObject *result;
  ipp_t *request = ippNewRequest(CUPS_GET_CLASSES), *answer;
  ipp_attribute_t *attr;
  const char *attributes[] = {
    "printer-name",
    "member-names",
  };

  ippAddStrings (request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
		 "requested-attributes",
		 sizeof (attributes) / sizeof (attributes[0]),
		 NULL, attributes);
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
    PyObject *members = NULL;
    char *classname = NULL;
    char *printer_uri = NULL;

    while (attr && attr->group_tag != IPP_TAG_PRINTER)
      attr = attr->next;

    if (!attr)
      break;

     for (; attr && attr->group_tag == IPP_TAG_PRINTER;
	 attr = attr->next) {
      if (!strcmp (attr->name, "printer-name") &&
	  attr->value_tag == IPP_TAG_NAME)
	classname = attr->values[0].string.text;
      else if (!strcmp (attr->name, "printer-uri-supported") &&
	       attr->value_tag == IPP_TAG_URI)
	printer_uri = attr->values[0].string.text;
      else if (!strcmp (attr->name, "member-names") &&
	       attr->value_tag == IPP_TAG_NAME) {
	Py_XDECREF (members);
	members = build_list_from_attribute_strings (attr);
      }
    }

    if (printer_uri) {
      Py_XDECREF (members);
      members = PyString_FromString (printer_uri);
    }

    if (!members)
      members = PyList_New (0);

    if (classname) {
      PyDict_SetItemString (result, classname, members);
    } else
      Py_XDECREF (members);

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
  ipp_t *request = ippNewRequest(CUPS_GET_PPDS), *answer;
  ipp_attribute_t *attr;

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
	       (!strcmp (attr->name, "ppd-make") &&
		attr->value_tag == IPP_TAG_TEXT) ||
	       (!strcmp (attr->name, "ppd-device-id") &&
		attr->value_tag == IPP_TAG_TEXT)) {
	val = PyUnicode_DecodeUTF8 (attr->values[0].string.text,
				    strlen (attr->values[0].string.text),
				    NULL);
	if (!val) {
	  // CUPS 1.2 always gives us UTF-8.  Before CUPS 1.2, the
	  // ppd-* strings come straight from the PPD with no
	  // transcoding, but the attributes-charset is still 'utf-8'
	  // so we've no way of knowing the real encoding.
	  // In that case, detect the error and force it to ASCII.
	  char *ascii, *orig = attr->values[0].string.text;
	  int i;
	  PyErr_Clear ();
	  ascii = malloc (1 + strlen (orig));
	  for (i = 0; orig[i]; i++)
	    ascii[i] = orig[i] & 0x7f;
	  ascii[i] = '\0';
	  val = PyString_FromString (ascii);
	  free (ascii);
	}
      }

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
  ipp_t *request = ippNewRequest(CUPS_GET_DEVICES), *answer;
  ipp_attribute_t *attr;

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

static ipp_t *
add_modify_printer_request (const char *name)
{
  char uri[HTTP_MAX_URI];
  ipp_t *request = ippNewRequest (CUPS_ADD_MODIFY_PRINTER);
  snprintf (uri, sizeof (uri), "ipp://localhost/printers/%s", name);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, uri);
  return request;
}

static ipp_t *
add_modify_class_request (const char *name)
{
  char uri[HTTP_MAX_URI];
  ipp_t *request = ippNewRequest (CUPS_ADD_MODIFY_CLASS);
  snprintf (uri, sizeof (uri), "ipp://localhost/classes/%s", name);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, uri);
  return request;
}

static PyObject *
Connection_addPrinter (Connection *self, PyObject *args, PyObject *kwds)
{
  const char *name;
  char *ppdfile = NULL;
  const char *ppdname = NULL;
  const char *info = NULL;
  const char *location = NULL;
  const char *device = NULL;
  PyObject *ppd = NULL;
  ipp_t *request, *answer;
  int ppds_specified = 0;
  static char *kwlist[] = { "name", "filename", "ppdname", "info",
			    "location", "device", "ppd", NULL };

  if (!PyArg_ParseTupleAndKeywords (args, kwds, "s|sssssO", kwlist,
				    &name, &ppdfile, &ppdname, &info,
				    &location, &device, &ppd))
    return NULL;

  if (ppdfile)
    ppds_specified++;
  if (ppdname)
    ppds_specified++;
  if (ppd) {
    if (!PyObject_TypeCheck (ppd, &cups_PPDType)) {
      PyErr_SetString (PyExc_TypeError, "Expecting cups.PPD");
      return NULL;
    }

    ppds_specified++;
  }
  if (ppds_specified > 1) {
    PyErr_SetString (PyExc_RuntimeError,
		     "Only one PPD may be given");
    return NULL;
  }

  if (ppd) {
    // We've been given a cups.PPD object.  Construct a PPD file.
    const char *template = "scp-ppd-XXXXXX";
    size_t len = strlen (_PATH_TMP) + strlen (template) + 1;
    char *p;
    int fd;
    PyObject *args, *result;
    ppdfile = malloc (len);
    p = stpcpy (ppdfile, _PATH_TMP);
    strcpy (p, template);
    fd = mkstemp (ppdfile);
    if (fd < 0) {
      free (ppdfile);
      PyErr_SetFromErrno (PyExc_RuntimeError);
      return NULL;
    }

    args = Py_BuildValue ("(i)", fd);
    result = PPD_writeFd ((PPD *) ppd, args);
    Py_DECREF (args);
    close (fd);

    if (result == NULL) {
      unlink (ppdfile);
      free (ppdfile);
      return NULL;
    }
  }

  request = add_modify_printer_request (name);
  if (ppdname)
    ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		  "ppd-name", NULL, ppdname);
  if (info)
    ippAddString (request, IPP_TAG_PRINTER, IPP_TAG_TEXT,
		  "printer-info", NULL, info);
  if (location)
    ippAddString (request, IPP_TAG_PRINTER, IPP_TAG_TEXT,
		  "printer-location", NULL, location);
  if (device)
    ippAddString (request, IPP_TAG_PRINTER, IPP_TAG_URI,
		  "device-uri", NULL, device);
  
  if (ppdfile)
    answer = cupsDoFileRequest (self->http, request, "/admin/", ppdfile);
  else
    answer = cupsDoRequest (self->http, request, "/admin/");

  if (ppd) {
    unlink (ppdfile);
    free (ppdfile);
  }

  if (PyErr_Occurred ()) {
    if (answer)
      ippDelete (answer);
    return NULL;
  }

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
  ipp_t *request, *answer;

  if (!PyArg_ParseTuple (args, "ss", &name, &device_uri))
    return NULL;

  request = add_modify_printer_request (name);
  ippAddString (request, IPP_TAG_PRINTER, IPP_TAG_URI,
		"device-uri", NULL, device_uri);
  answer = cupsDoRequest (self->http, request, "/admin/");
  if (PyErr_Occurred ()) {
    if (answer)
      ippDelete (answer);
    return NULL;
  }

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
Connection_setPrinterInfo (Connection *self, PyObject *args)
{
  const char *name;
  const char *info;
  ipp_t *request, *answer;
  int i;

  if (!PyArg_ParseTuple (args, "ss", &name, &info))
    return NULL;

  request = add_modify_printer_request (name);
  for (i = 0; i < 2; i++) {
    ippAddString (request, IPP_TAG_PRINTER, IPP_TAG_TEXT,
		  "printer-info", NULL, info);
    answer = cupsDoRequest (self->http, request, "/admin/");
    if (PyErr_Occurred ()) {
      if (answer)
	ippDelete (answer);
      return NULL;
    }

    if (answer && answer->request.status.status_code == IPP_NOT_POSSIBLE) {
      // Perhaps it's a class, not a printer.
      ippDelete (answer);
      request = add_modify_class_request (name);
    } else break;
  }

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
Connection_setPrinterLocation (Connection *self, PyObject *args)
{
  const char *name;
  const char *location;
  ipp_t *request, *answer;
  int i;

  if (!PyArg_ParseTuple (args, "ss", &name, &location))
    return NULL;

  request = add_modify_printer_request (name);
  for (i = 0; i < 2; i++) {
    ippAddString (request, IPP_TAG_PRINTER, IPP_TAG_TEXT,
		  "printer-location", NULL, location);
    answer = cupsDoRequest (self->http, request, "/admin/");
    if (PyErr_Occurred ()) {
      if (answer)
	ippDelete (answer);
      return NULL;
    }

    if (answer && answer->request.status.status_code == IPP_NOT_POSSIBLE) {
      // Perhaps it's a class, not a printer.
      ippDelete (answer);
      request = add_modify_class_request (name);
    } else break;
  }

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
Connection_setPrinterShared (Connection *self, PyObject *args)
{
  const char *name;
  int sharing;
  ipp_t *request, *answer;
  int i;

  if (!PyArg_ParseTuple (args, "si", &name, &sharing))
    return NULL;

  request = add_modify_printer_request (name);
  for (i = 0; i < 2; i++) {
    ippAddBoolean (request, IPP_TAG_OPERATION, "printer-is-shared", sharing);
    answer = cupsDoRequest (self->http, request, "/admin/");
    if (PyErr_Occurred ()) {
      if (answer)
	ippDelete (answer);
      return NULL;
    }

    if (answer && answer->request.status.status_code == IPP_NOT_POSSIBLE) {
      // Perhaps it's a class, not a printer.
      ippDelete (answer);
      request = add_modify_class_request (name);
    } else break;
  }

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
Connection_setPrinterJobSheets (Connection *self, PyObject *args)
{
  const char *name;
  const char *start;
  const char *end;
  ipp_t *request, *answer;
  ipp_attribute_t *a;
  int i;

  if (!PyArg_ParseTuple (args, "sss", &name, &start, &end))
    return NULL;

  request = add_modify_printer_request (name);
  for (i = 0; i < 2; i++) {
    a = ippAddStrings (request, IPP_TAG_PRINTER, IPP_TAG_NAME,
		       "job-sheets-default", 2, NULL, NULL);
    a->values[0].string.text = strdup (start);
    a->values[1].string.text = strdup (end);
    answer = cupsDoRequest (self->http, request, "/admin/");
    if (PyErr_Occurred ()) {
      if (answer)
	ippDelete (answer);
      return NULL;
    }

    if (answer && answer->request.status.status_code == IPP_NOT_POSSIBLE) {
      ippDelete (answer);
      // Perhaps it's a class, not a printer.
      request = add_modify_class_request (name);
    } else break;
  }

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
Connection_setPrinterErrorPolicy (Connection *self, PyObject *args)
{
  const char *name;
  const char *policy;
  ipp_t *request, *answer;
  int i;

  if (!PyArg_ParseTuple (args, "ss", &name, &policy))
    return NULL;

  request = add_modify_printer_request (name);
  for (i = 0; i < 2; i++) {
    ippAddString (request, IPP_TAG_PRINTER, IPP_TAG_NAME,
		  "printer-error-policy", NULL, policy);
    answer = cupsDoRequest (self->http, request, "/admin/");
    if (PyErr_Occurred ()) {
      if (answer)
	ippDelete (answer);
      return NULL;
    }

    if (answer && answer->request.status.status_code == IPP_NOT_POSSIBLE) {
      ippDelete (answer);
      // Perhaps it's a class, not a printer.
      request = add_modify_class_request (name);
    } else break;
  }

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
Connection_setPrinterOpPolicy (Connection *self, PyObject *args)
{
  const char *name;
  const char *policy;
  ipp_t *request, *answer;
  int i;

  if (!PyArg_ParseTuple (args, "ss", &name, &policy))
    return NULL;

  request = add_modify_printer_request (name);
  for (i = 0; i < 2; i++) {
    ippAddString (request, IPP_TAG_PRINTER, IPP_TAG_NAME,
		  "printer-op-policy", NULL, policy);
    answer = cupsDoRequest (self->http, request, "/admin/");
    if (PyErr_Occurred ()) {
      if (answer)
	ippDelete (answer);
      return NULL;
    }

    if (answer && answer->request.status.status_code == IPP_NOT_POSSIBLE) {
      ippDelete (answer);
      // Perhaps it's a class, not a printer.
      request = add_modify_class_request (name);
    } else break;
  }

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
do_requesting_user_names (Connection *self, PyObject *args,
			  const char *requeststr)
{
  const char *name;
  PyObject *users;
  int num_users, i, j;
  ipp_t *request, *answer;
  ipp_attribute_t *attr;

  if (!PyArg_ParseTuple (args, "sO", &name, &users))
    return NULL;

  if (!PyList_Check (users)) {
    PyErr_SetString (PyExc_TypeError, "List required");
    return NULL;
  }
  request = add_modify_printer_request (name);
  for (i = 0; i < 2; i++) {
    num_users = PyList_Size (users);
    if (num_users) {
      attr = ippAddStrings (request, IPP_TAG_PRINTER, IPP_TAG_NAME,
			    requeststr, num_users, NULL, NULL);
      for (j = 0; j < num_users; j++) {
	PyObject *username = PyList_GetItem (users, j);
	if (!PyString_Check (username)) {
	  int k;
	  PyErr_SetString (PyExc_TypeError, "String required");
	  for (k = 0; k < j; k++) {
	    free (attr->values[k].string.text);
	    attr->values[k].string.text = NULL;
	  }
	  ippDelete (request);
	  return NULL;
	}
	attr->values[j].string.text = strdup (PyString_AsString (username));
      }
    } else {
      attr = ippAddStrings (request, IPP_TAG_PRINTER, IPP_TAG_NAME,
			    requeststr, 1, NULL, NULL);
      if (strstr (requeststr, "denied"))
	attr->values[0].string.text = strdup ("none");
      else
	attr->values[0].string.text = strdup ("all");
    }
    answer = cupsDoRequest (self->http, request, "/admin/");
    if (PyErr_Occurred ()) {
      if (answer)
	ippDelete (answer);
      return NULL;
    }

    if (answer && answer->request.status.status_code == IPP_NOT_POSSIBLE) {
      ippDelete (answer);
      // Perhaps it's a class, not a printer.
      request = add_modify_class_request (name);
    } else break;
  }

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
Connection_setPrinterUsersAllowed (Connection *self, PyObject *args)
{
  return do_requesting_user_names (self, args, "requesting-user-name-allowed");
}

static PyObject *
Connection_setPrinterUsersDenied (Connection *self, PyObject *args)
{
  return do_requesting_user_names (self, args, "requesting-user-name-denied");
}

static PyObject *
Connection_setPrinterOptionDefault (Connection *self, PyObject *args)
{
  const char *name;
  const char *option;
  const char *value;
  const char *const suffix = "-default";
  char *opt;
  ipp_t *request, *answer;
  int i;
  size_t optionlen;

  if (!PyArg_ParseTuple (args, "sss", &name, &option, &value))
    return NULL;

  optionlen = strlen (option);
  opt = malloc (optionlen + sizeof (suffix) + 1);
  memcpy (opt, option, optionlen);
  sprintf (opt + optionlen, suffix);
  request = add_modify_printer_request (name);
  for (i = 0; i < 2; i++) {
    ippAddString (request, IPP_TAG_PRINTER, IPP_TAG_NAME,
		  opt, NULL, value);
    answer = cupsDoRequest (self->http, request, "/admin/");
    if (PyErr_Occurred ()) {
      if (answer)
	ippDelete (answer);
      return NULL;
    }

    if (answer && answer->request.status.status_code == IPP_NOT_POSSIBLE) {
      ippDelete (answer);
      // Perhaps it's a class, not a printer.
      request = add_modify_class_request (name);
    } else break;
  }

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
Connection_getPrinterAttributes (Connection *self, PyObject *args)
{
  PyObject *ret;
  const char *name;
  ipp_t *request, *answer;
  ipp_attribute_t *attr;
  char uri[HTTP_MAX_URI];
  int i;

  if (!PyArg_ParseTuple (args, "s", &name))
    return NULL;

  snprintf (uri, sizeof (uri), "ipp://localhost/printers/%s", name);
  for (i = 0; i < 2; i++) {
    request = ippNewRequest (IPP_GET_PRINTER_ATTRIBUTES);
    ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
		  "printer-uri", NULL, uri);
    answer = cupsDoRequest (self->http, request, "/");
    if (answer && answer->request.status.status_code == IPP_NOT_POSSIBLE) {
      ippDelete (answer);
      // Perhaps it's a class, not a printer.
      snprintf (uri, sizeof (uri), "ipp://localhost/classes/%s", name);
    } else break;
  }

  if (!answer || answer->request.status.status_code > IPP_OK_CONFLICT) {
    set_ipp_error (answer ?
		   answer->request.status.status_code :
		   cupsLastError ());
    if (answer)
      ippDelete (answer);
    return NULL;
  }

  ret = PyDict_New ();
  attr = ippFindAttribute (answer, "job-sheets-supported", IPP_TAG_ZERO);
  if (attr) {
    PyObject *job_sheets_supported = build_list_from_attribute_strings (attr);
    PyDict_SetItemString (ret, "job-sheets-supported", job_sheets_supported);
  }

  attr = ippFindAttribute (answer, "job-sheets-default", IPP_TAG_ZERO);
  if (attr) {
    const char *start, *end;
    start = attr->values[0].string.text;
    if (attr->num_values >= 2)
      end = attr->values[1].string.text;
    else
      end = "";

    PyDict_SetItemString (ret, "job-sheets-default",
			  Py_BuildValue ("(ss)", start, end));
  }

  attr = ippFindAttribute (answer, "printer-error-policy-supported",
			   IPP_TAG_ZERO);
  if (attr) {
    PyObject *errpolicy_supported = build_list_from_attribute_strings (attr);
    PyDict_SetItemString (ret, "printer-error-policy-supported",
			  errpolicy_supported);
  }

  attr = ippFindAttribute (answer, "printer-error-policy", IPP_TAG_ZERO);
  if (attr)
    PyDict_SetItemString (ret, "printer-error-policy",
			  PyString_FromString (attr->values[0].string.text));

  attr = ippFindAttribute (answer, "printer-op-policy-supported",
			   IPP_TAG_ZERO);
  if (attr) {
    PyObject *oppolicy_supported = build_list_from_attribute_strings (attr);
    PyDict_SetItemString (ret, "printer-op-policy-supported",
			  oppolicy_supported);
  }

  attr = ippFindAttribute (answer, "printer-op-policy", IPP_TAG_ZERO);
  if (attr)
    PyDict_SetItemString (ret, "printer-op-policy",
			  PyString_FromString (attr->values[0].string.text));

  attr = ippFindAttribute (answer, "requesting-user-name-allowed",
			   IPP_TAG_ZERO);
  if (attr) {
    PyObject *allowed = build_list_from_attribute_strings (attr);
    PyDict_SetItemString (ret, "requesting-user-name-allowed",
			  allowed);
  }

  attr = ippFindAttribute (answer, "requesting-user-name-denied",
			   IPP_TAG_ZERO);
  if (attr) {
    PyObject *denied = build_list_from_attribute_strings (attr);
    PyDict_SetItemString (ret, "requesting-user-name-denied",
			  denied);
  }

  return ret;
}

static PyObject *
Connection_addPrinterToClass (Connection *self, PyObject *args)
{
  const char *printername;
  const char *classname;
  char classuri[HTTP_MAX_URI];
  char printeruri[HTTP_MAX_URI];
  ipp_t *request, *answer;

  if (!PyArg_ParseTuple (args, "ss", &printername, &classname))
    return NULL;

  // Does the class exist, and is the printer already in it?
  request = ippNewRequest (IPP_GET_PRINTER_ATTRIBUTES);
  snprintf (classuri, sizeof (classuri),
	    "ipp://localhost/classes/%s", classname);
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

  request = ippNewRequest (CUPS_ADD_CLASS);
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
  if (PyErr_Occurred ()) {
    if (answer)
      ippDelete (answer);
    return NULL;
  }

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
  ipp_t *request, *answer;
  ipp_attribute_t *printers;
  int i;

  if (!PyArg_ParseTuple (args, "ss", &printername, &classname))
    return NULL;

  // Does the class exist, and is the printer in it?
  request = ippNewRequest (IPP_GET_PRINTER_ATTRIBUTES);
  snprintf (classuri, sizeof (classuri),
	    "ipp://localhost/classes/%s", classname);
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

  request = ippNewRequest (CUPS_ADD_CLASS);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, classuri);

  // Only printer in class?  Delete the class.
  if (printers->num_values == 1)
    request->request.op.operation_id = CUPS_DELETE_CLASS;
  else {
    // Trim the printer from the list.
    ipp_attribute_t *newlist;
    int j;
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
  if (PyErr_Occurred ()) {
    if (answer)
      ippDelete (answer);
    return NULL;
  }

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
Connection_deleteClass (Connection *self, PyObject *args)
{
  const char *classname;
  char classuri[HTTP_MAX_URI];
  ipp_t *request, *answer;

  if (!PyArg_ParseTuple (args, "s", &classname))
    return NULL;

  request = ippNewRequest (CUPS_DELETE_CLASS);
  snprintf (classuri, sizeof (classuri),
	    "ipp://localhost/classes/%s", classname);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, classuri);
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
  return do_printer_request (self, args, IPP_RESUME_PRINTER);
}

static PyObject *
Connection_disablePrinter (Connection *self, PyObject *args)
{
  return do_printer_request (self, args, IPP_PAUSE_PRINTER);
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
    ipp_status_t err = cupsLastError ();
    if (err)
      set_ipp_error (err);
    else
      PyErr_SetString (PyExc_RuntimeError, "cupsGetPPD2 failed");

    return NULL;
  }

  ret = PyString_FromString (ppdfile);
  return ret;
}

static PyObject *
Connection_printTestPage (Connection *self, PyObject *args)
{
  const char *printer;
  const char *datadir;
  char filename[PATH_MAX];
  char uri[HTTP_MAX_URI];
  ipp_t *request, *answer;
  ipp_attribute_t *attr;
  char *resource;
  int jobid = 0;
  int i;

  if (!PyArg_ParseTuple (args, "s", &printer))
    return NULL;

  if ((datadir = getenv ("CUPS_DATADIR")) == NULL)
    datadir = "/usr/share/cups";

  snprintf (filename, sizeof (filename), "%s/data/testprint.ps", datadir);
  snprintf (uri, sizeof (uri), "ipp://localhost/printers/%s", printer);
  resource = uri + strlen ("ipp://localhost");
  for (i = 0; i < 2; i++) {
    request = ippNewRequest (IPP_PRINT_JOB);
    ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
		  NULL, uri);
    ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		  "requesting-user-name", NULL, "guest");
    ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_NAME, "job-name",
		  NULL, "Test Page");
    ippAddString (request, IPP_TAG_JOB, IPP_TAG_MIMETYPE, "document-format",
		  NULL, "application/postscript");
    answer = cupsDoFileRequest (self->http, request, resource, filename);
    if (answer && answer->request.status.status_code == IPP_NOT_POSSIBLE) {
      ippDelete (answer);
      // Perhaps it's a class, not a printer.
      snprintf (uri, sizeof (uri), "ipp://localhost/classes/%s", printer);
    } else break;
  }

  if (!answer || answer->request.status.status_code > IPP_OK_CONFLICT) {
    set_ipp_error (answer ?
		   answer->request.status.status_code :
		   cupsLastError ());
    if (answer)
      ippDelete (answer);
    return NULL;
  }

  attr = ippFindAttribute (answer, "job-id", IPP_TAG_INTEGER);
  if (attr)
    jobid = attr->values[0].integer;

  ippDelete (answer);
  return Py_BuildValue ("i", jobid);
}

PyMethodDef Connection_methods[] =
  {
    { "getPrinters",
      (PyCFunction) Connection_getPrinters, METH_NOARGS,
      "Returns a dict, indexed by name, of dicts representing\n"
      "queues, indexed by attribute." },

    { "getClasses",
      (PyCFunction) Connection_getClasses, METH_NOARGS,
      "Returns a dict, indexed by name, of objects representing\n"
      "classes.  Each class object is either a string, in which case it\n"
      "is for the remote class; or a list, in which case it is a list of\n"
      "queue names." },

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
      "location=None, device=None, ppd=None) -> None\n"
      "filename: local filename of PPD file\n"
      "ppdname: filename from cups.Connection.getPPDs()\n"
      "info: info string\n"
      "description: description string\n"
      "device: device URI string\n"
      "ppd: cups.PPD object" },

    { "setPrinterDevice",
      (PyCFunction) Connection_setPrinterDevice, METH_VARARGS,
      "setPrinterDevice(name, device_uri) -> None" },

    { "setPrinterInfo",
      (PyCFunction) Connection_setPrinterInfo, METH_VARARGS,
      "setPrinterInfo(name, info) -> None" },

    { "setPrinterLocation",
      (PyCFunction) Connection_setPrinterLocation, METH_VARARGS,
      "setPrinterLocation(name, info) -> None" },

    { "setPrinterShared",
      (PyCFunction) Connection_setPrinterShared, METH_VARARGS,
      "setPrinterShared(name, bool) -> None\n\nCUPS 1.2 only" },

    { "setPrinterJobSheets",
      (PyCFunction) Connection_setPrinterJobSheets, METH_VARARGS,
      "setPrinterJobSheets(name, start, end) -> None" },

    { "setPrinterErrorPolicy",
      (PyCFunction) Connection_setPrinterErrorPolicy, METH_VARARGS,
      "setPrinterErrorPolicy(name, policy) -> None" },

    { "setPrinterOpPolicy",
      (PyCFunction) Connection_setPrinterOpPolicy, METH_VARARGS,
      "setPrinterOpPolicy(name, policy) -> None" },

    { "setPrinterUsersAllowed",
      (PyCFunction) Connection_setPrinterUsersAllowed, METH_VARARGS,
      "setPrinterUsersAllowed(name, string list) -> None\n\n"
      "CUPS 1.2.\n"
      "string list is a list of user-names.\n"
      "The special string 'all' means that there will be no user-name\n"
      "restriction."},

    { "setPrinterUsersDenied",
      (PyCFunction) Connection_setPrinterUsersDenied, METH_VARARGS,
      "setPrinterUsersDenied(name, string list) -> None\n\n"
      "CUPS 1.2.\n"
      "string list is a list of user-names.\n"
      "The special string 'none' means that there will be no user-name\n"
      "restriction."},

    { "setPrinterOptionDefault",
      (PyCFunction) Connection_setPrinterOptionDefault, METH_VARARGS,
      "setPrinterOptionDefault(name, option, value) -> None\n\n"
      "CUPS 1.2.\n" },

    { "deletePrinter",
      (PyCFunction) Connection_deletePrinter, METH_VARARGS,
      "deletePrinter(name, ppdfile, device_uri) -> None" },

    { "getPrinterAttributes",
      (PyCFunction) Connection_getPrinterAttributes, METH_VARARGS,
      "getPrinterAttributes(name) -> dict\n"
      "Returns a dict, indexed by attribute, of printer attributes\n"
      "for the printer 'name'.\n\n"
      "Attributes:\n"
      "'job-sheets-supported': list of strings\n"
      "'job-sheets-default': tuple of strings (start, end)\n"
      "'printer-error-policy-supported': if present, list of strings\n"
      "'printer-error-policy': if present, string\n"
      "'printer-op-policy-supported': if present, list of strings\n"
      "'printer-op-policy': if present, string" },

    { "addPrinterToClass",
      (PyCFunction) Connection_addPrinterToClass, METH_VARARGS,
      "addPrinterToClass(name, class) -> None\n\n"
      "If the class does not yet exist, it is created."},

    { "deletePrinterFromClass",
      (PyCFunction) Connection_deletePrinterFromClass, METH_VARARGS,
      "deletePrinterFromClass(name, class) -> None\n\n"
      "If the class would be left empty, it is removed." },

    { "deleteClass",
      (PyCFunction) Connection_deleteClass, METH_VARARGS,
      "deleteClass(class) -> None" },

    { "setDefault",
      (PyCFunction) Connection_setDefault, METH_VARARGS,
      "setDefault(name) -> None" },

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

    { "printTestPage",
      (PyCFunction) Connection_printTestPage, METH_VARARGS,
      "printTestPage(printer) -> job ID\nPrint a test page." },

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
