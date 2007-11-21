/*
 * cups - Python bindings for CUPS
 * Copyright (C) 2002, 2005, 2006, 2007  Tim Waugh <twaugh@redhat.com>
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

typedef struct
{
  PyObject_HEAD
  int is_default;
  char *destname;
  char *instance;

  // Options
  int num_options;
  char **name;
  char **value;
} Dest;

static void
set_http_error (http_status_t status)
{
  PyObject *v = Py_BuildValue ("i", status);
  debugprintf("set_http_error: %d\n", (int) status);
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

  debugprintf("set_ipp_error: %d, %s\n", (int) status, last_error);
  PyObject *v = Py_BuildValue ("(is)", status, last_error);
  if (v != NULL) {
    PyErr_SetObject (IPPError, v);
    Py_DECREF (v);
  }
}

static PyObject *
PyObj_from_UTF8 (const char *utf8)
{
  PyObject *val = PyUnicode_Decode (utf8, strlen (utf8), "utf-8", NULL);
  if (!val) {
    // CUPS 1.2 always gives us UTF-8.  Before CUPS 1.2, the
    // ppd-* strings come straight from the PPD with no
    // transcoding, but the attributes-charset is still 'utf-8'
    // so we've no way of knowing the real encoding.
    // In that case, detect the error and force it to ASCII.
    const char *orig = utf8;
    char *ascii;
    int i;
    PyErr_Clear ();
    ascii = malloc (1 + strlen (orig));
    for (i = 0; orig[i]; i++)
      ascii[i] = orig[i] & 0x7f;
    ascii[i] = '\0';
    val = PyString_FromString (ascii);
    free (ascii);
  }

  return val;
}

static const char *
UTF8_from_PyObj (char **const utf8, PyObject *obj)
{
  if (PyUnicode_Check (obj)) {
    PyObject *stringobj = PyUnicode_AsUTF8String (obj);
    if (stringobj == NULL)
      return NULL;

    *utf8 = strdup (PyString_AsString (stringobj));
    Py_DECREF (stringobj);
    return *utf8;
  }
  else if (PyString_Check (obj)) {
    const char *ret;
    PyObject *unicodeobj = PyUnicode_FromEncodedObject (obj, NULL, NULL);
    ret = UTF8_from_PyObj (utf8, unicodeobj);
    Py_DECREF (unicodeobj);
    return ret;
  }

  PyErr_SetString (PyExc_TypeError, "string or unicode object required");
  return NULL;
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

  debugprintf ("-> Connection_init()\n");

  Py_BEGIN_ALLOW_THREADS;
  debugprintf ("httpConnectEncrypt(...)\n");
  self->http = httpConnectEncrypt (cupsServer (),
				   ippPort (),
				   cupsEncryption ());
  Py_END_ALLOW_THREADS;

  if (!self->http) {
    PyErr_SetString (PyExc_RuntimeError, "httpConnectionEncrypt failed");
    debugprintf ("<- Connection_init() = -1\n");
    return -1;
  }

  debugprintf ("<- Connection_init() = 0\n");
  return 0;
}

static void
Connection_dealloc (Connection *self)
{
  if (self->http) {  
    debugprintf ("httpClose()\n");
    httpClose (self->http);
  }

  self->ob_type->tp_free ((PyObject *) self);
}

////////////////
// Connection // METHODS
////////////////

static PyObject *
do_printer_request (Connection *self, PyObject *args, PyObject *kwds,
		    ipp_op_t op)
{
  PyObject *nameobj;
  PyObject *reasonobj = NULL;
  char *name;
  char *reason = NULL;
  char uri[HTTP_MAX_URI];
  ipp_t *request, *answer;

  switch (op) {
  case IPP_PAUSE_PRINTER:
  case CUPS_REJECT_JOBS:
    {
      static char *kwlist[] = { "name", "reason", NULL };
      if (!PyArg_ParseTupleAndKeywords (args, kwds, "O|O", kwlist,
					&nameobj, &reasonobj))
	return NULL;
      debugprintf ("-> do_printer_request(op:%d, name:%s, reason:%s)\n",
		   (int) op, name, reason ? reason : "(null)");
      break;
    }

  default:
    if (!PyArg_ParseTuple (args, "O", &nameobj))
      return NULL;
    debugprintf ("-> do_printer_request(op:%d, name:%s)\n", (int) op, name);
    break;
  }

  if (UTF8_from_PyObj (&name, nameobj) == NULL)
    return NULL;

  request = ippNewRequest (op);
  snprintf (uri, sizeof (uri), "ipp://localhost/printers/%s", name);
  free (name);

  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, uri);

  if (reasonobj) {
    char *reason;
    if (UTF8_from_PyObj (&reason, reasonobj) == NULL) {
      ippDelete (request);
      return NULL;
    }

    ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_TEXT,
		  "printer-state-message", NULL, reason);
    free (reason);
  }

  debugprintf ("cupsDoRequest(\"/admin/\")\n");
  answer = cupsDoRequest (self->http, request, "/admin/");
  if (PyErr_Occurred ()) {
    if (answer)
      ippDelete (answer);
    debugprintf("<- do_printer_request (error)\n");
    return NULL;
  }

  if (!answer || answer->request.status.status_code > IPP_OK_CONFLICT) {
    set_ipp_error (answer ?
		   answer->request.status.status_code :
		   cupsLastError ());
    if (answer)
      ippDelete (answer);
    debugprintf("<- do_printer_request (error)\n");
    return NULL;
  }

  ippDelete (answer);
  Py_INCREF (Py_None);
  debugprintf("<- do_printer_request (None)\n");
  return Py_None;
}

static PyObject *
Connection_getDests (Connection *self)
{
  cups_dest_t *dests;
  int num_dests;
  PyObject *pydests = PyDict_New ();
  int i;

  debugprintf ("-> Connection_getDests()\n");
  debugprintf ("cupsGetDests2()\n");
  num_dests = cupsGetDests2 (self->http, &dests);

  // Create a dict indexed by (name,instance)
  for (i = 0; i <= num_dests; i++) {
    PyObject *largs = Py_BuildValue ("()");
    PyObject *lkwlist = Py_BuildValue ("{}");
    Dest *destobj = (Dest *) PyType_GenericNew (&cups_DestType,
						largs, lkwlist);
    Py_DECREF (largs);
    Py_DECREF (lkwlist);

    cups_dest_t *dest;
    PyObject *nameinstance;
    if (i == num_dests)
      {
	// Add a (None,None) entry for the default printer.
	dest = cupsGetDest (NULL, NULL, num_dests, dests);
	if (dest == NULL) {
	  /* No default printer. */
	  Py_DECREF ((PyObject *) destobj);
	  break;
	}

	nameinstance = Py_BuildValue ("(ss)", NULL, NULL);
      }
    else
      {
	dest = dests + i;
	nameinstance = Py_BuildValue ("(ss)", dest->name, dest->instance);
      }

    destobj->is_default = dest->is_default;
    destobj->destname = strdup (dest->name);
    destobj->instance = (dest->instance ? strdup (dest->instance) : NULL );
    destobj->num_options = dest->num_options;
    destobj->name = malloc (dest->num_options * sizeof (char *));
    destobj->value = malloc (dest->num_options * sizeof (char *));
    int j;
    for (j = 0; j < dest->num_options; j++) {
      destobj->name[j] = strdup (dest->options[j].name);
      destobj->value[j] = strdup (dest->options[j].value);
    }

    PyDict_SetItem (pydests, nameinstance, (PyObject *) destobj);
    Py_DECREF ((PyObject *) destobj);
  }

  debugprintf ("cupsFreeDests()\n");
  cupsFreeDests (num_dests, dests);
  debugprintf ("<- Connection_getDests()\n");
  return pydests;
}

static PyObject *
build_list_from_attribute_strings (ipp_attribute_t *attr)
{
  PyObject *list = PyList_New (0);
  int i;
  debugprintf ("-> build_list_from_attribute_strings()\n");
  for (i = 0; i < attr->num_values; i++) {
    PyObject *val = PyObj_from_UTF8 (attr->values[i].string.text);
    PyList_Append (list, val);
    Py_DECREF (val);
    debugprintf ("%s\n", attr->values[i].string.text);
  }
  debugprintf ("<- build_list_from_attribute_strings()\n");
  return list;
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
    "printer-state-message",
    "printer-state-reasons",
    "printer-uri-supported",
    "device-uri",
    "printer-is-shared",
  };

  debugprintf ("-> Connection_getPrinters()\n");

  ippAddStrings (request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
		 "requested-attributes",
		 sizeof (attributes) / sizeof (attributes[0]),
		 NULL, attributes);
  debugprintf ("cupsDoRequest(\"/\")\n");
  answer = cupsDoRequest (self->http, request, "/");
  if (!answer || answer->request.status.status_code > IPP_OK_CONFLICT) {
    if (answer && answer->request.status.status_code == IPP_NOT_FOUND) {
      // No printers.
      debugprintf ("<- Connection_getPrinters() = {} (no printers)\n");
      ippDelete (answer);
      return PyDict_New ();
    }

    set_ipp_error (answer ?
		   answer->request.status.status_code :
		   cupsLastError ());
    if (answer)
      ippDelete (answer);
    debugprintf ("<- Connection_getPrinters() (error)\n");
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

      debugprintf ("Attribute: %s\n", attr->name);
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
	val = PyObj_from_UTF8 (attr->values[0].string.text);
      }
      else if (!strcmp (attr->name,
			"printer-state-reasons") &&
	       attr->value_tag == IPP_TAG_KEYWORD) {
	val = build_list_from_attribute_strings (attr);
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
	val = PyObj_from_UTF8 (attr->values[0].string.text);
      }
      else if (!strcmp (attr->name, "printer-is-shared") &&
	       attr->value_tag == IPP_TAG_BOOLEAN) {
	val = PyBool_FromLong (attr->values[0].boolean);
      }

      if (val) {
	debugprintf ("Added %s to dict\n", attr->name);
	PyDict_SetItemString (dict, attr->name, val);
	Py_DECREF (val);
      }
    }

    if (printer) {
      PyObject *key = PyObj_from_UTF8 (printer);
      PyDict_SetItem (result, key, dict);
      Py_DECREF (key);
    }

    Py_DECREF (dict);
    if (!attr)
      break;
  }

  ippDelete (answer);
  debugprintf ("<- Connection_getPrinters() = dict\n");
  return result;
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

  debugprintf ("-> Connection_getClasses()\n");
  ippAddStrings (request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
		 "requested-attributes",
		 sizeof (attributes) / sizeof (attributes[0]),
		 NULL, attributes);
  debugprintf ("cupsDoRequest(\"/\")\n");
  answer = cupsDoRequest (self->http, request, "/");
  if (!answer || answer->request.status.status_code > IPP_OK_CONFLICT) {
    if (answer && answer->request.status.status_code == IPP_NOT_FOUND) {
      // No classes.
      debugprintf ("<- Connection_getClasses() = {} (no classes)\n");
      ippDelete (answer);
      return PyDict_New ();
    }

    set_ipp_error (answer ?
		   answer->request.status.status_code :
		   cupsLastError ());
    if (answer)
      ippDelete (answer);
    debugprintf ("<- Connection_getClasses() (error)\n");
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
      debugprintf ("Attribute: %s\n", attr->name);
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
      members = PyObj_from_UTF8 (printer_uri);
    }

    if (!members)
      members = PyList_New (0);

    if (classname) {
      PyObject *key = PyObj_from_UTF8 (classname);
      debugprintf ("Added class %s\n", classname);
      PyDict_SetItem (result, key, members);
      Py_DECREF (key);
    }

    Py_DECREF (members);
    if (!attr)
      break;
  }

  ippDelete (answer);
  debugprintf ("<- Connection_getClasses() = dict\n");
  return result;
}

static PyObject *
Connection_getPPDs (Connection *self)
{
  PyObject *result;
  ipp_t *request = ippNewRequest(CUPS_GET_PPDS), *answer;
  ipp_attribute_t *attr;

  debugprintf ("-> Connection_getPPDs()\n");
  debugprintf ("cupsDoRequest(\"/\")\n");
  Py_BEGIN_ALLOW_THREADS;
  answer = cupsDoRequest (self->http, request, "/");
  Py_END_ALLOW_THREADS;
  if (!answer || answer->request.status.status_code > IPP_OK_CONFLICT) {
    set_ipp_error (answer ?
		   answer->request.status.status_code :
		   cupsLastError ());
    if (answer)
      ippDelete (answer);
    debugprintf ("<- Connection_getPPDs() (error)\n");
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

      debugprintf ("Attribute: %s\n", attr->name);
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
	val = PyObj_from_UTF8 (attr->values[0].string.text);
      }

      if (val) {
	debugprintf ("Adding %s to ppd dict\n", attr->name);
	PyDict_SetItemString (dict, attr->name, val);
	Py_DECREF (val);
      }
    }

    if (ppdname) {
      PyObject *key = PyObj_from_UTF8 (ppdname);
      debugprintf ("Adding %s to result dict\n", ppdname);
      PyDict_SetItem (result, key, dict);
      Py_DECREF (key);
    }

    Py_DECREF (dict);
    if (!attr)
      break;
  }

  ippDelete (answer);
  debugprintf ("<- Connection_getPPDs() = dict\n");
  return result;
}

static PyObject *
Connection_getServerPPD (Connection *self, PyObject *args)
{
#ifndef HAVE_CUPS_1_3
  PyErr_SetString (PyExc_RuntimeError,
		   "Operation not supported - recompile against CUPS 1.3");
  return NULL;
#else /* CUPS 1.3 */
  const char *ppd_name, *filename;
  if (!PyArg_ParseTuple (args, "s", &ppd_name))
    return NULL;
  debugprintf ("-> Connection_getServerPPD()\n");
  Py_BEGIN_ALLOW_THREADS;
  filename = cupsGetServerPPD (self->http, ppd_name);
  Py_END_ALLOW_THREADS;
  if (!filename) {
    set_ipp_error (cupsLastError ());
    debugprintf ("<- Connection_getServerPPD() (error)\n");
    return NULL;
  }
  debugprintf ("<- Connection_getServerPPD(\"%s\") = \"%s\"\n",
	       ppd_name, filename);
  return PyString_FromString (filename);
#endif /* CUPS 1.3 */
}

static PyObject *
Connection_getDevices (Connection *self)
{
  PyObject *result;
  ipp_t *request = ippNewRequest(CUPS_GET_DEVICES), *answer;
  ipp_attribute_t *attr;

  debugprintf ("-> Connection_getDevices()\n");
  debugprintf ("cupsDoRequest(\"/\")\n");
  Py_BEGIN_ALLOW_THREADS;
  answer = cupsDoRequest (self->http, request, "/");
  Py_END_ALLOW_THREADS;
  if (!answer || answer->request.status.status_code > IPP_OK_CONFLICT) {
    set_ipp_error (answer ?
		   answer->request.status.status_code :
		   cupsLastError ());
    if (answer)
      ippDelete (answer);
    debugprintf ("<- Connection_getDevices() (error)\n");
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

      debugprintf ("Attribute: %s\n", attr->name);
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
	val = PyObj_from_UTF8 (attr->values[0].string.text);

      if (val) {
	debugprintf ("Adding %s to device dict\n", attr->name);
	PyDict_SetItemString (dict, attr->name, val);
	Py_DECREF (val);
      }
    }

    if (device_uri) {
      PyObject *key = PyObj_from_UTF8 (device_uri);
      debugprintf ("Adding %s to result dict\n", device_uri);
      PyDict_SetItem (result, key, dict);
      Py_DECREF (key);
    }

    Py_DECREF (dict);
    if (!attr)
      break;
  }

  ippDelete (answer);
  debugprintf ("<- Connection_getDevices() = dict\n");
  return result;
}

static PyObject *
Connection_getJobs (Connection *self, PyObject *args, PyObject *kwds)
{
  PyObject *result;
  ipp_t *request = ippNewRequest(IPP_GET_JOBS), *answer;
  ipp_attribute_t *attr;
  char *which = NULL;
  int my_jobs = 0;
  static char *kwlist[] = { "which_jobs", "my_jobs", NULL };
  if (!PyArg_ParseTupleAndKeywords (args, kwds, "|si", kwlist,
				    &which, &my_jobs))
	  return NULL;

  debugprintf ("-> Connection_getJobs(%s,%d)\n",
	       which ? which : "(null)", my_jobs);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI, "job-uri",
		NULL, "ipp://localhost/jobs/");

  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "which-jobs",
		NULL, which ? which : "not-completed");

  ippAddBoolean (request, IPP_TAG_OPERATION, "my-jobs", my_jobs);
  if (my_jobs)
    ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		  "requesting-user-name", NULL, cupsUser());

  debugprintf ("cupsDoRequest(\"/\")\n");
  answer = cupsDoRequest (self->http, request, "/");
  if (!answer || answer->request.status.status_code > IPP_OK_CONFLICT) {
    set_ipp_error (answer ?
		   answer->request.status.status_code :
		   cupsLastError ());
    if (answer)
      ippDelete (answer);
    debugprintf ("<- Connection_getJobs() (error)\n");
    return NULL;
  }

  result = PyDict_New ();
  for (attr = answer->attrs; attr; attr = attr->next) {
    PyObject *dict;
    int job_id = -1;

    while (attr && attr->group_tag != IPP_TAG_JOB)
      attr = attr->next;

    if (!attr)
      break;

    dict = PyDict_New ();
    for (; attr && attr->group_tag == IPP_TAG_JOB;
	 attr = attr->next) {
      PyObject *val = NULL;

      debugprintf ("Attribute: %s\n", attr->name);
      if (!strcmp (attr->name, "job-id") &&
	  attr->value_tag == IPP_TAG_INTEGER)
	job_id = attr->values[0].integer;
      else if (((!strcmp (attr->name, "job-k-octets") ||
		 !strcmp (attr->name, "job-priority") ||
		 !strcmp (attr->name, "time-at-creation") ||
		 !strcmp (attr->name, "time-at-processing") ||
		 !strcmp (attr->name, "time-at-completed") ||
		 !strcmp (attr->name, "job-media-sheets") ||
		 !strcmp (attr->name, "job-media-sheets-completed")) &&
		attr->value_tag == IPP_TAG_INTEGER) ||
	       (!strcmp (attr->name, "job-state") &&
		attr->value_tag == IPP_TAG_ENUM))
	val = PyInt_FromLong (attr->values[0].integer);
      else if ((!strcmp (attr->name, "job-name") &&
		attr->value_tag == IPP_TAG_NAME) ||
	       (!strcmp (attr->name, "job-originating-user-name") &&
		attr->value_tag == IPP_TAG_NAME) ||
	       (!strcmp (attr->name, "job-printer-uri") &&
		attr->value_tag == IPP_TAG_URI))
	val = PyObj_from_UTF8 (attr->values[0].string.text);
      else if (!strcmp (attr->name, "job-preserved") &&
	       attr->value_tag == IPP_TAG_BOOLEAN)
	val = PyBool_FromLong (attr->values[0].integer);

      if (val) {
	debugprintf ("Adding %s to job dict\n", attr->name);
	PyDict_SetItemString (dict, attr->name, val);
	Py_DECREF (val);
      }
    }

    if (job_id != -1) {
      debugprintf ("Adding %d to result dict\n", job_id);
      PyObject *job_obj = PyInt_FromLong (job_id);
      PyDict_SetItem (result, job_obj, dict);
      Py_DECREF (job_obj);
    }

    Py_DECREF (dict);

    if (!attr)
      break;
  }

  ippDelete (answer);
  debugprintf ("<- Connection_getJobs() = dict\n");
  return result;
}

static PyObject *
Connection_cancelJob (Connection *self, PyObject *args)
{
  ipp_t *request, *answer;
  int job_id;
  char uri[1024];
  if (!PyArg_ParseTuple (args, "i", &job_id))
    return NULL;

  debugprintf ("-> Connection_cancelJob(%d)\n", job_id);
  request = ippNewRequest(IPP_CANCEL_JOB);
  snprintf (uri, sizeof (uri), "ipp://localhost/jobs/%d", job_id);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI, "job-uri", NULL, uri);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		"requesting-user-name", NULL, cupsUser ());
  debugprintf ("cupsDoRequest(\"/jobs/\")\n");
  answer = cupsDoRequest (self->http, request, "/jobs/");
  if (!answer || answer->request.status.status_code > IPP_OK_CONFLICT) {
    set_ipp_error (answer ?
		   answer->request.status.status_code :
		   cupsLastError ());
    if (answer)
      ippDelete (answer);
    debugprintf ("<- Connection_cancelJob() (error)\n");
    return NULL;
  }

  Py_INCREF (Py_None);
  debugprintf ("<- Connection_cancelJob() = None\n");
  return Py_None;
}

static PyObject *
Connection_setJobHoldUntil (Connection *self, PyObject *args)
{
  ipp_t *request, *answer;
  int job_id;
  PyObject *job_hold_until_obj;
  char *job_hold_until;
  char uri[1024];
  cups_option_t *options = NULL;
  int num_options = 0;
  if (!PyArg_ParseTuple (args, "io", &job_id, &job_hold_until_obj))
    return NULL;

  if (UTF8_from_PyObj (&job_hold_until, job_hold_until_obj) == NULL)
    return NULL;

  debugprintf ("-> Connection_setJobHoldUntil(%d,%s)\n",
	       job_id, job_hold_until);
  request = ippNewRequest(IPP_SET_JOB_ATTRIBUTES);
  snprintf (uri, sizeof (uri), "ipp://localhost/jobs/%d", job_id);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI, "job-uri", NULL, uri);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		"requesting-user-name", NULL, cupsUser ());
  num_options = cupsAddOption ("job-hold-until", job_hold_until,
			       num_options, &options);
  cupsEncodeOptions (request, num_options, options);
  free (job_hold_until);

  debugprintf ("cupsDoRequest(\"/jobs/\")\n");
  answer = cupsDoRequest (self->http, request, "/jobs/");
  if (!answer || answer->request.status.status_code > IPP_OK_CONFLICT) {
    set_ipp_error (answer ?
		   answer->request.status.status_code :
		   cupsLastError ());
    if (answer)
      ippDelete (answer);
    debugprintf ("<- Connection_setJobHoldUntil() (error)\n");
    return NULL;
  }

  Py_INCREF (Py_None);
  debugprintf ("<- Connection_setJobHoldUntil() = None\n");
  return Py_None;
}

static PyObject *
Connection_restartJob (Connection *self, PyObject *args)
{
  ipp_t *request, *answer;
  int job_id;
  char uri[1024];
  if (!PyArg_ParseTuple (args, "i", &job_id))
    return NULL;

  debugprintf ("-> Connection_restartJob(%d)\n", job_id);
  request = ippNewRequest(IPP_RESTART_JOB);
  snprintf (uri, sizeof (uri), "ipp://localhost/jobs/%d", job_id);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI, "job-uri", NULL, uri);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		"requesting-user-name", NULL, cupsUser ());
  debugprintf ("cupsDoRequest(\"/jobs/\")\n");
  answer = cupsDoRequest (self->http, request, "/jobs/");
  if (!answer || answer->request.status.status_code > IPP_OK_CONFLICT) {
    set_ipp_error (answer ?
		   answer->request.status.status_code :
		   cupsLastError ());
    if (answer)
      ippDelete (answer);
    debugprintf ("<- Connection_restartJob() (error)\n");
    return NULL;
  }

  Py_INCREF (Py_None);
  debugprintf ("<- Connection_restartJob() = None\n");
  return Py_None;
}

static PyObject *
Connection_getFile (Connection *self, PyObject *args)
{
  const char *resource, *filename;
  http_status_t status;

  if (!PyArg_ParseTuple (args, "ss", &resource, &filename))
    return NULL;

  debugprintf ("-> Connection_getFile(%s, %s)\n", resource, filename);
  debugprintf ("cupsGetFile()\n");
  status = cupsGetFile (self->http, resource, filename);
  if (status != HTTP_OK) {
    set_http_error (status);
    debugprintf ("<- Connection_getFile() (error)\n");
    return NULL;
  }

  Py_INCREF (Py_None);
  debugprintf ("<- Connection_getFile() = None\n");
  return Py_None;
}

static PyObject *
Connection_putFile (Connection *self, PyObject *args)
{
  const char *resource, *filename;
  http_status_t status;

  if (!PyArg_ParseTuple (args, "ss", &resource, &filename))
    return NULL;

  debugprintf ("-> Connection_putFile(%s, %s)\n", resource, filename);
  debugprintf ("cupsPutFile()\n");
  status = cupsPutFile (self->http, resource, filename);
  if (status != HTTP_OK && status != HTTP_CREATED) {
    set_http_error (status);
    debugprintf ("<- Connection_putFile() (error)\n");
    return NULL;
  }

  Py_INCREF (Py_None);
  debugprintf ("<- Connection_putFile() = None\n");
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
  PyObject *nameobj = NULL;
  char *name = NULL;
  PyObject *ppdfileobj = NULL;
  char *ppdfile = NULL;
  PyObject *ppdnameobj = NULL;
  char *ppdname = NULL;
  PyObject *infoobj = NULL;
  char *info = NULL;
  PyObject *locationobj = NULL;
  char *location = NULL;
  PyObject *deviceobj = NULL;
  char *device = NULL;
  PyObject *ppd = NULL;
  ipp_t *request, *answer;
  int ppds_specified = 0;
  static char *kwlist[] = { "name", "filename", "ppdname", "info",
			    "location", "device", "ppd", NULL };

  if (!PyArg_ParseTupleAndKeywords (args, kwds, "O|OOOOOO", kwlist,
				    &nameobj, &ppdfileobj, &ppdnameobj,
				    &infoobj, &locationobj, &deviceobj, &ppd))
    return NULL;

  if (UTF8_from_PyObj (&name, nameobj) == NULL ||
      (ppdfileobj && UTF8_from_PyObj (&ppdfile, ppdfileobj) == NULL) ||
      (ppdnameobj && UTF8_from_PyObj (&ppdname, ppdnameobj) == NULL) ||
      (infoobj && UTF8_from_PyObj (&info, infoobj) == NULL) ||
      (locationobj && UTF8_from_PyObj (&location, locationobj) == NULL) ||
      (deviceobj && UTF8_from_PyObj (&device, deviceobj) == NULL)) {
    free (name);
    free (ppdfile);
    free (ppdname);
    free (info);
    free (location);
    free (device);
    return NULL;
  }

  debugprintf ("-> Connection_addPrinter(%s,%s,%s,%s,%s,%s,%s)\n",
	       name, ppdfile ? ppdfile: "", ppdname ? ppdname: "",
	       info ? info: "", location ? location: "",
	       device ? device: "", ppd ? "(PPD object)": "");

  if (ppdfile)
    ppds_specified++;
  if (ppdname)
    ppds_specified++;
  if (ppd) {
    if (!PyObject_TypeCheck (ppd, &cups_PPDType)) {
      PyErr_SetString (PyExc_TypeError, "Expecting cups.PPD");
      debugprintf ("<- Connection_addPrinter() EXCEPTION\n");
      free (name);
      free (ppdfile);
      free (ppdname);
      free (info);
      free (location);
      free (device);
      return NULL;
    }

    ppds_specified++;
  }
  if (ppds_specified > 1) {
    PyErr_SetString (PyExc_RuntimeError,
		     "Only one PPD may be given");
    debugprintf ("<- Connection_addPrinter() EXCEPTION\n");
    free (name);
    free (ppdfile);
    free (ppdname);
    free (info);
    free (location);
    free (device);
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
      PyErr_SetFromErrno (PyExc_RuntimeError);
      debugprintf ("<- Connection_addPrinter() EXCEPTION\n");
      free (name);
      free (ppdfile);
      free (ppdname);
      free (info);
      free (location);
      free (device);
      return NULL;
    }

    args = Py_BuildValue ("(i)", fd);
    result = PPD_writeFd ((PPD *) ppd, args);
    Py_DECREF (args);
    close (fd);

    if (result == NULL) {
      unlink (ppdfile);
      debugprintf ("<- Connection_addPrinter() EXCEPTION\n");
      free (name);
      free (ppdfile);
      free (ppdname);
      free (info);
      free (location);
      free (device);
      return NULL;
    }
  }

  request = add_modify_printer_request (name);
  free (name);
  if (ppdname) {
    ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		  "ppd-name", NULL, ppdname);
    free (ppdname);
    Py_DECREF (ppdnameobj);
  }
  if (info) {
    ippAddString (request, IPP_TAG_PRINTER, IPP_TAG_TEXT,
		  "printer-info", NULL, info);
    free (info);
    Py_DECREF (infoobj);
  }
  if (location) {
    ippAddString (request, IPP_TAG_PRINTER, IPP_TAG_TEXT,
		  "printer-location", NULL, location);
    free (location);
    Py_DECREF (locationobj);
  }
  if (device) {
    ippAddString (request, IPP_TAG_PRINTER, IPP_TAG_URI,
		  "device-uri", NULL, device);
    free (device);
    Py_DECREF (deviceobj);
  }
  
  if (ppdfile) {
    answer = cupsDoFileRequest (self->http, request, "/admin/", ppdfile);
  } else
    answer = cupsDoRequest (self->http, request, "/admin/");

  if (ppd) {
    unlink (ppdfile);
    free (ppdfile);
  } else if (ppdfile)
    free (ppdfile);

  if (PyErr_Occurred ()) {
    if (answer)
      ippDelete (answer);
    debugprintf ("<- Connection_addPrinter() EXCEPTION\n");
    return NULL;
  }

  if (!answer || answer->request.status.status_code > IPP_OK_CONFLICT) {
    set_ipp_error (answer ?
		   answer->request.status.status_code :
		   cupsLastError ());
    if (answer)
      ippDelete (answer);

    debugprintf ("<- Connection_addPrinter() EXCEPTION\n");
    return NULL;
  }

  ippDelete (answer);
  Py_INCREF (Py_None);
  debugprintf ("<- Connection_addPrinter() = None\n");
  return Py_None;
}

static PyObject *
Connection_setPrinterDevice (Connection *self, PyObject *args)
{
  PyObject *nameobj;
  PyObject *device_uriobj;
  char *name;
  char *device_uri;
  ipp_t *request, *answer;

  if (!PyArg_ParseTuple (args, "OO", &nameobj, &device_uriobj))
    return NULL;

  if (UTF8_from_PyObj (&name, nameobj) == NULL)
    return NULL;

  if (UTF8_from_PyObj (&device_uri, device_uriobj) == NULL) {
    free (name);
    return NULL;
  }

  request = add_modify_printer_request (name);
  free (name);
  ippAddString (request, IPP_TAG_PRINTER, IPP_TAG_URI,
		"device-uri", NULL, device_uri);
  free (device_uri);
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
  PyObject *nameobj;
  PyObject *infoobj;
  char *name;
  char *info;
  ipp_t *request, *answer;
  int i;

  if (!PyArg_ParseTuple (args, "OO", &nameobj, &infoobj))
    return NULL;

  if (UTF8_from_PyObj (&name, nameobj) == NULL)
    return NULL;

  if (UTF8_from_PyObj (&info, infoobj) == NULL) {
    free (name);
    return NULL;
  }

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

  free (name);
  free (info);
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
  PyObject *nameobj;
  PyObject *locationobj;
  char *name;
  char *location;
  ipp_t *request, *answer;
  int i;

  if (!PyArg_ParseTuple (args, "OO", &nameobj, &locationobj))
    return NULL;

  if (UTF8_from_PyObj (&name, nameobj) == NULL)
    return NULL;

  if (UTF8_from_PyObj (&location, locationobj) == NULL) {
    free (name);
    return NULL;
  }

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

  free (name);
  free (location);
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
  PyObject *nameobj;
  char *name;
  int sharing;
  ipp_t *request, *answer;
  int i;

  if (!PyArg_ParseTuple (args, "Oi", &nameobj, &sharing))
    return NULL;

  if (UTF8_from_PyObj (&name, nameobj) == NULL)
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

  free (name);
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
  PyObject *nameobj;
  PyObject *startobj;
  PyObject *endobj;
  char *name;
  char *start;
  char *end;
  ipp_t *request, *answer;
  ipp_attribute_t *a;
  int i;

  if (!PyArg_ParseTuple (args, "OOO", &nameobj, &startobj, &endobj))
    return NULL;

  if (UTF8_from_PyObj (&name, nameobj) == NULL)
    return NULL;

  if (UTF8_from_PyObj (&start, startobj) == NULL) {
    free (name);
    return NULL;
  }

  if (UTF8_from_PyObj (&end, endobj) == NULL) {
    free (name);
    free (start);
    return NULL;
  }

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

  free (name);
  free (start);
  free (end);
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
  PyObject *nameobj;
  char *name;
  PyObject *policyobj;
  char *policy;
  ipp_t *request, *answer;
  int i;

  if (!PyArg_ParseTuple (args, "OO", &nameobj, &policyobj))
    return NULL;

  if (UTF8_from_PyObj (&name, nameobj) == NULL)
    return NULL;

  if (UTF8_from_PyObj (&policy, policyobj) == NULL) {
    free (name);
    return NULL;
  }

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

  free (name);
  free (policy);
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
  PyObject *nameobj;
  char *name;
  PyObject *policyobj;
  char *policy;
  ipp_t *request, *answer;
  int i;

  if (!PyArg_ParseTuple (args, "OO", &nameobj, &policyobj))
    return NULL;

  if (UTF8_from_PyObj (&name, nameobj) == NULL)
    return NULL;

  if (UTF8_from_PyObj (&policy, policyobj) == NULL) {
    free (name);
    return NULL;
  }

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

  free (name);
  free (policy);
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
  PyObject *nameobj;
  char *name;
  PyObject *users;
  int num_users, i, j;
  ipp_t *request, *answer;
  ipp_attribute_t *attr;

  if (!PyArg_ParseTuple (args, "OO", &nameobj, &users))
    return NULL;

  if (UTF8_from_PyObj (&name, nameobj) == NULL)
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
	PyObject *username = PyList_GetItem (users, j); // borrowed ref
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

  free (name);
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

static char *
PyObject_to_string (PyObject *pyvalue)
{
  char *value = "{unknown type}";
  if (PyString_Check (pyvalue) ||
      PyUnicode_Check (pyvalue)) {
    value = PyString_AsString (pyvalue);
  } else if (PyBool_Check (pyvalue)) {
    value = (pyvalue == Py_True) ? "true" : "false";
  } else if (PyInt_Check (pyvalue)) {
    long v = PyInt_AsLong (pyvalue);
    value = alloca (20);
    snprintf (value, 20, "%ld", v);
  } else if (PyFloat_Check (pyvalue)) {
    double v = PyFloat_AsDouble (pyvalue);
    value = alloca (100);
    snprintf (value, 100, "%f", v);
  }

  return strdup (value);
}

static PyObject *
Connection_addPrinterOptionDefault (Connection *self, PyObject *args)
{
  PyObject *nameobj;
  char *name;
  PyObject *optionobj;
  char *option;
  PyObject *pyvalue;
  const char const suffix[] = "-default";
  char *opt;
  ipp_t *request, *answer;
  int i;
  size_t optionlen;

  if (!PyArg_ParseTuple (args, "OOO", &nameobj, &optionobj, &pyvalue))
    return NULL;

  if (UTF8_from_PyObj (&name, nameobj) == NULL)
    return NULL;

  if (UTF8_from_PyObj (&option, optionobj) == NULL) {
    free (name);
    return NULL;
  }

  optionlen = strlen (option);
  opt = malloc (optionlen + sizeof (suffix) + 1);
  memcpy (opt, option, optionlen);
  sprintf (opt + optionlen, suffix);
  request = add_modify_printer_request (name);
  for (i = 0; i < 2; i++) {
    if (!PyString_Check (pyvalue) && !PyUnicode_Check (pyvalue) &&
	PySequence_Check (pyvalue)) {
      ipp_attribute_t *attr;
      int len = PySequence_Size (pyvalue);
      int j;
      attr = ippAddStrings (request, IPP_TAG_PRINTER, IPP_TAG_NAME,
			    opt, len, NULL, NULL);
      for (j = 0; j < len; j++) {
	PyObject *each = PySequence_GetItem (pyvalue, j);
	attr->values[j].string.text = PyObject_to_string (each);
      }
    } else
      ippAddString (request, IPP_TAG_PRINTER, IPP_TAG_NAME,
		    opt, NULL, PyObject_to_string (pyvalue));

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

  free (name);
  free (option);
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
Connection_deletePrinterOptionDefault (Connection *self, PyObject *args)
{
  PyObject *nameobj;
  char *name;
  PyObject *optionobj;
  char *option;
  const char *const suffix = "-default";
  char *opt;
  ipp_t *request, *answer;
  int i;
  size_t optionlen;

  if (!PyArg_ParseTuple (args, "OO", &nameobj, &optionobj))
    return NULL;

  if (UTF8_from_PyObj (&name, nameobj) == NULL)
    return NULL;

  if (UTF8_from_PyObj (&option, optionobj) == NULL) {
    free (name);
    return NULL;
  }

  optionlen = strlen (option);
  opt = malloc (optionlen + sizeof (suffix) + 1);
  memcpy (opt, option, optionlen);
  sprintf (opt + optionlen, suffix);
  request = add_modify_printer_request (name);
  for (i = 0; i < 2; i++) {
    ippAddString (request, IPP_TAG_PRINTER, IPP_TAG_DELETEATTR,
		  opt, NULL, NULL);
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

  free (name);
  free (option);
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
Connection_deletePrinter (Connection *self, PyObject *args, PyObject *kwds)
{
  return do_printer_request (self, args, kwds, CUPS_DELETE_PRINTER);
}

static PyObject *
PyObject_from_attr_value (ipp_attribute_t *attr, int i)
{
  PyObject *val = NULL;
  switch (attr->value_tag) {
  case IPP_TAG_NAME:
  case IPP_TAG_TEXT:
  case IPP_TAG_KEYWORD:
  case IPP_TAG_URI:
  case IPP_TAG_CHARSET:
  case IPP_TAG_MIMETYPE:
  case IPP_TAG_LANGUAGE:
    val = PyObj_from_UTF8 (attr->values[i].string.text);
    break;
  case IPP_TAG_INTEGER:
  case IPP_TAG_ENUM:
    val = PyInt_FromLong (attr->values[i].integer);
    break;
  case IPP_TAG_BOOLEAN:
    val = PyBool_FromLong (attr->values[i].integer);
    break;
  case IPP_TAG_RANGE:
    val = Py_BuildValue ("(ii)",
			 attr->values[i].range.lower,
			 attr->values[i].range.upper);
    break;
  case IPP_TAG_NOVALUE:
    Py_INCREF (Py_None);
    val = Py_None;
    break;

    // TODO:
  case IPP_TAG_DATE:
    val = PyString_FromString ("(IPP_TAG_DATE)");
    break;
  default:
    val = PyString_FromString ("(unknown IPP tag)");
    break;
  }

  return val;
}

static PyObject *
Connection_getPrinterAttributes (Connection *self, PyObject *args)
{
  PyObject *ret;
  PyObject *nameobj;
  char *name;
  ipp_t *request, *answer;
  ipp_attribute_t *attr;
  char uri[HTTP_MAX_URI];
  int i;

  if (!PyArg_ParseTuple (args, "O", &nameobj))
    return NULL;

  if (UTF8_from_PyObj (&name, nameobj) == NULL)
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

  free (name);
  if (!answer || answer->request.status.status_code > IPP_OK_CONFLICT) {
    set_ipp_error (answer ?
		   answer->request.status.status_code :
		   cupsLastError ());
    if (answer)
      ippDelete (answer);
    return NULL;
  }

  ret = PyDict_New ();
  for (attr = answer->attrs; attr; attr = attr->next) {
    while (attr && attr->group_tag != IPP_TAG_PRINTER)
      attr = attr->next;

    if (!attr)
      break;

    for (; attr && attr->group_tag == IPP_TAG_PRINTER;
	 attr = attr->next) {
      size_t namelen = strlen (attr->name);
      int is_list = attr->num_values > 1;

      // job-sheets-default is special, since it is always two values.
      // Make it a tuple.
      if (!strcmp (attr->name, "job-sheets-default") &&
	  attr->value_tag == IPP_TAG_NAME) {
	PyObject *startobj, *endobj;
	const char *start, *end;
	start = attr->values[0].string.text;
	if (attr->num_values >= 2)
	  end = attr->values[1].string.text;
	else
	  end = "";

	startobj = PyObj_from_UTF8 (start);
	endobj = PyObj_from_UTF8 (end);
	PyDict_SetItemString (ret, "job-sheets-default",
			      Py_BuildValue ("(ss)", startobj, endobj));
	Py_DECREF (startobj);
	Py_DECREF (endobj);
	continue;
      }

      // Check for '-supported' suffix.  Any xxx-supported attribute
      // that is a text type must be a list.
      //
      // Also check for attributes that are known to allow multiple
      // string values, and make them lists.
      if (!is_list && namelen > 10) {
	const char *multivalue_options[] =
	  {
	    "notify-events-default",
	    "requesting-user-name-allowed",
	    "requesting-user-name-denied",
	    "finishings-supported",
	    NULL
	  };

	switch (attr->value_tag) {
	case IPP_TAG_NAME:
	case IPP_TAG_TEXT:
	case IPP_TAG_KEYWORD:
	case IPP_TAG_URI:
	case IPP_TAG_CHARSET:
	case IPP_TAG_MIMETYPE:
	case IPP_TAG_LANGUAGE:
	  is_list = !strcmp (attr->name + namelen - 10, "-supported");

	  if (!is_list) {
	    const char **opt;
	    for (opt = multivalue_options; !is_list && *opt; opt++)
	      is_list = !strcmp (attr->name, *opt);
	  }

	default:
	  break;
	}
      }

      if (is_list) {
	PyObject *list = PyList_New (0);
	int i;
	for (i = 0; i < attr->num_values; i++) {
	  PyObject *val = PyObject_from_attr_value (attr, i);
	  PyList_Append (list, val);
	}
	PyDict_SetItemString (ret, attr->name, list);
      } else {
	PyObject *val = PyObject_from_attr_value (attr, i);
	PyDict_SetItemString (ret, attr->name, val);
      }
    }

    if (!attr)
      break;
  }

  return ret;
}

static PyObject *
Connection_addPrinterToClass (Connection *self, PyObject *args)
{
  PyObject *printernameobj;
  char *printername;
  PyObject *classnameobj;
  char *classname;
  char classuri[HTTP_MAX_URI];
  char printeruri[HTTP_MAX_URI];
  ipp_t *request, *answer;

  if (!PyArg_ParseTuple (args, "OO", &printernameobj, &classnameobj))
    return NULL;

  if (UTF8_from_PyObj (&printername, printernameobj) == NULL)
    return NULL;

  if (UTF8_from_PyObj (&classname, classnameobj) == NULL) {
    free (printername);
    return NULL;
  }

  // Does the class exist, and is the printer already in it?
  request = ippNewRequest (IPP_GET_PRINTER_ATTRIBUTES);
  snprintf (classuri, sizeof (classuri),
	    "ipp://localhost/classes/%s", classname);
  free (classname);
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
	  free (printername);
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
  free (printername);
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
  PyObject *printernameobj;
  char *printername;
  PyObject *classnameobj;
  char *classname;
  char classuri[HTTP_MAX_URI];
  ipp_t *request, *answer;
  ipp_attribute_t *printers;
  int i;

  if (!PyArg_ParseTuple (args, "OO", &printernameobj, &classnameobj))
    return NULL;

  if (UTF8_from_PyObj (&printername, printernameobj) == NULL)
    return NULL;

  if (UTF8_from_PyObj (&classname, classnameobj) == NULL) {
    free (printername);
    return NULL;
  }

  // Does the class exist, and is the printer in it?
  request = ippNewRequest (IPP_GET_PRINTER_ATTRIBUTES);
  snprintf (classuri, sizeof (classuri),
	    "ipp://localhost/classes/%s", classname);
  free (classname);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, classuri);
  answer = cupsDoRequest (self->http, request, "/");
  if (!answer) {
    set_ipp_error (cupsLastError ());
    free (printername);
    return NULL;
  }

  printers = ippFindAttribute (answer, "member-names", IPP_TAG_NAME);
  for (i = 0; printers && i < printers->num_values; i++)
    if (!strcasecmp (printers->values[i].string.text, printername))
      break;

  free (printername);
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
  PyObject *classnameobj;
  char *classname;
  char classuri[HTTP_MAX_URI];
  ipp_t *request, *answer;

  if (!PyArg_ParseTuple (args, "O", &classnameobj))
    return NULL;

  if (UTF8_from_PyObj (&classname, classnameobj) == NULL)
    return NULL;

  request = ippNewRequest (CUPS_DELETE_CLASS);
  snprintf (classuri, sizeof (classuri),
	    "ipp://localhost/classes/%s", classname);
  free (classname);
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
Connection_enablePrinter (Connection *self, PyObject *args, PyObject *kwds)
{
  return do_printer_request (self, args, kwds, IPP_RESUME_PRINTER);
}

static PyObject *
Connection_disablePrinter (Connection *self, PyObject *args, PyObject *kwds)
{
  return do_printer_request (self, args, kwds, IPP_PAUSE_PRINTER);
}

static PyObject *
Connection_acceptJobs (Connection *self, PyObject *args, PyObject *kwds)
{
  return do_printer_request (self, args, kwds, CUPS_ACCEPT_JOBS);
}

static PyObject *
Connection_rejectJobs (Connection *self, PyObject *args, PyObject *kwds)
{
  return do_printer_request (self, args, kwds, CUPS_REJECT_JOBS);
}

static PyObject *
Connection_getDefault (Connection *self, PyObject *args)
{
  const char *def;
  PyObject *ret;
  debugprintf ("-> Connection_getDefault()\n");
  def = cupsGetDefault2 (self->http);
  if (def == NULL) {
    debugprintf ("<- Connection_getDefault() = None\n");
    ret = Py_None;
    Py_INCREF (Py_None);
  } else {
    debugprintf ("<- Connection_getDefault() = \"%s\"\n", def);
    ret = PyString_FromString (def);
  }

  return ret;
}

static PyObject *
Connection_setDefault (Connection *self, PyObject *args, PyObject *kwds)
{
  return do_printer_request (self, args, kwds, CUPS_SET_DEFAULT);
}

static PyObject *
Connection_getPPD (Connection *self, PyObject *args)
{
  PyObject *ret;
  PyObject *printerobj;
  char *printer;
  const char *ppdfile;

  if (!PyArg_ParseTuple (args, "O", &printerobj))
    return NULL;

  if (UTF8_from_PyObj (&printer, printerobj) == NULL)
    return NULL;

  ppdfile = cupsGetPPD2 (self->http, printer);
  free (printer);
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
Connection_printTestPage (Connection *self, PyObject *args, PyObject *kwds)
{
  PyObject *printerobj;
  char *printer;
  PyObject *fileobj = NULL;
  char *file;
  PyObject *titleobj = NULL;
  char *title;
  PyObject *formatobj = NULL;
  char *format;
  PyObject *userobj = NULL;
  char *user;
  const char *datadir;
  char filename[PATH_MAX];
  char uri[HTTP_MAX_URI];
  ipp_t *request, *answer;
  ipp_attribute_t *attr;
  char *resource;
  int jobid = 0;
  int i;
  static char *kwlist[] = { "name", "file", "title", "format", "user", NULL };

  if (!PyArg_ParseTupleAndKeywords (args, kwds, "O|OOOO", kwlist,
				    &printerobj, &fileobj, &titleobj,
				    &formatobj, &userobj))
    return NULL;

  if (UTF8_from_PyObj (&printer, printerobj) == NULL)
    return NULL;

  if ((fileobj && UTF8_from_PyObj (&file, fileobj) == NULL) ||
      (titleobj && UTF8_from_PyObj (&title, titleobj) == NULL) ||
      (formatobj && UTF8_from_PyObj (&format, formatobj) == NULL) ||
      (userobj && UTF8_from_PyObj (&user, userobj) == NULL)) {
    free (printer);
    free (file);
    free (title);
    free (format);
    free (user);
    return NULL;
  }
    
  if (!fileobj) {
    if ((datadir = getenv ("CUPS_DATADIR")) == NULL)
      datadir = "/usr/share/cups";

    snprintf (filename, sizeof (filename), "%s/data/testprint.ps", datadir);
    file = filename;
  }

  if (!titleobj)
    title = "Test Page";

  if (!formatobj)
    format = "application/postscript";

  if (!userobj)
    user = "guest";

  snprintf (uri, sizeof (uri), "ipp://localhost/printers/%s", printer);
  resource = uri + strlen ("ipp://localhost");
  for (i = 0; i < 2; i++) {
    request = ippNewRequest (IPP_PRINT_JOB);
    ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
		  NULL, uri);
    ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		  "requesting-user-name", NULL, user);
    ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_NAME, "job-name",
		  NULL, title);
    ippAddString (request, IPP_TAG_JOB, IPP_TAG_MIMETYPE, "document-format",
		  NULL, format);
    answer = cupsDoFileRequest (self->http, request, resource, file);
    if (answer && answer->request.status.status_code == IPP_NOT_POSSIBLE) {
      ippDelete (answer);
      // Perhaps it's a class, not a printer.
      snprintf (uri, sizeof (uri), "ipp://localhost/classes/%s", printer);
    } else break;
  }

  free (printer);
  if (fileobj)
    free (file);
  if (titleobj)
    free (title);
  if (formatobj)
    free (format);
  if (userobj)
    free (user);

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

static PyObject *
Connection_adminGetServerSettings (Connection *self)
{
  PyObject *ret = PyDict_New ();
  int num_settings, i;
  cups_option_t *settings;
  cupsAdminGetServerSettings (self->http, &num_settings, &settings);
  for (i = 0; i < num_settings; i++) {
    PyObject *string = PyString_FromString (settings[i].value);
    PyDict_SetItemString (ret, settings[i].name, string);
    Py_DECREF (string);
  }

  cupsFreeOptions (num_settings, settings);
  return ret;
}

static PyObject *
Connection_adminSetServerSettings (Connection *self, PyObject *args)
{
#if PY_VERSION_HEX < 0x02050000
#define DICT_POS_TYPE int
#else
#define DICT_POS_TYPE Py_ssize_t
#endif

  PyObject *dict, *key, *val;
  int num_settings = 0;
  DICT_POS_TYPE pos = 0;
  cups_option_t *settings = NULL;
  if (!PyArg_ParseTuple (args, "O", &dict))
    return NULL;
  if (!PyDict_Check (dict)) {
    PyErr_SetString (PyExc_TypeError, "Expecting dict");
    return NULL;
  }

  debugprintf ("-> Connection_adminSetServerSettings()\n");
  while (PyDict_Next (dict, &pos, &key, &val)) {
    char *name, *value;
    if (!PyString_Check (key) ||
	!PyString_Check (val)) {
      cupsFreeOptions (num_settings, settings);
      PyErr_SetString (PyExc_TypeError, "Keys and values must be strings");
      debugprintf ("<- Connection_adminSetServerSettings() EXCEPTION\n");
      return NULL;
    }

    name = PyString_AsString (key);
    value = PyString_AsString (val);
    debugprintf ("%s: %s\n", name, value);
    num_settings = cupsAddOption (name,
				  value,
				  num_settings,
				  &settings);
  }

  debugprintf ("num_settings=%d, settings=%p\n", num_settings, settings);
  if (!cupsAdminSetServerSettings (self->http, num_settings, settings)) {
    cupsFreeOptions (num_settings, settings);
    PyErr_SetString (PyExc_RuntimeError, "Failed to set settings");
    debugprintf ("<- Connection_adminSetServerSettings() EXCEPTION\n");
    return NULL;
  }

  cupsFreeOptions (num_settings, settings);
  Py_INCREF (Py_None);
  debugprintf ("<- Connection_adminSetServerSettings()\n");
  return Py_None;
}

static PyObject *
Connection_getSubscriptions (Connection *self, PyObject *args, PyObject *kwds)
{
  PyObject *uriobj;
  char *uri;
  PyObject *my_subscriptions = Py_False;
  int job_id = -1;
  ipp_t *request, *answer;
  ipp_attribute_t *attr;
  PyObject *result, *subscription;
  int i;
  static char *kwlist[] = { "uri", "my_subscriptions", "job_id", NULL };

  if (!PyArg_ParseTupleAndKeywords (args, kwds, "O|Oi", kwlist,
				    &uriobj, &my_subscriptions, &job_id))
    return NULL;

  if (UTF8_from_PyObj (&uri, uriobj) == NULL)
    return NULL;

  if (my_subscriptions && !PyBool_Check (my_subscriptions)) {
    PyErr_SetString (PyExc_TypeError, "my_subscriptions must be a bool");
    return NULL;
  }

  debugprintf ("-> Connection_getSubscriptions()\n");
  request = ippNewRequest (IPP_GET_SUBSCRIPTIONS);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, uri);
  free (uri);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		"requesting-user-name", NULL, cupsUser ());

  if (my_subscriptions == Py_True)
    ippAddBoolean (request, IPP_TAG_OPERATION, "my-subscriptions", 1);

  if (job_id != -1)
    ippAddInteger (request, IPP_TAG_OPERATION, IPP_TAG_INTEGER,
		   "job-id", job_id);

  answer = cupsDoRequest (self->http, request, "/");
  if (!answer || answer->request.status.status_code > IPP_OK_CONFLICT) {
    set_ipp_error (answer ? answer->request.status.status_code :
		   cupsLastError ());
    if (answer)
      ippDelete (answer);
    debugprintf ("<- Connection_getSubscriptions() EXCEPTION\n");
    return NULL;
  }

  result = PyList_New (0);
  for (attr = answer->attrs; attr; attr = attr->next)
    if (attr->group_tag == IPP_TAG_SUBSCRIPTION)
      break;

  subscription = NULL;
  for (; attr; attr = attr->next) {
    PyObject *obj;
    if (attr->group_tag == IPP_TAG_ZERO) {
      // End of subscription.
      if (subscription) {
	PyList_Append (result, subscription);
	Py_DECREF (subscription);
      }

      subscription = NULL;
      continue;
    }

    if (attr->num_values > 1 || !strcmp (attr->name, "notify-events")) {
      obj = PyList_New (0);
      for (i = 0; i < attr->num_values; i++) {
	PyObject *item = PyObject_from_attr_value (attr, i);
	if (item)
	  PyList_Append (obj, item);
      }
    }
    else
      obj = PyObject_from_attr_value (attr, 0);

    if (!obj)
      // Can't represent this.
      continue;

    if (!subscription)
      subscription = PyDict_New ();

    PyDict_SetItemString (subscription, attr->name, obj);
    Py_DECREF (obj);
  }

  if (subscription) {
    PyList_Append (result, subscription);
    Py_DECREF (subscription);
  }

  ippDelete (answer);
  debugprintf ("<- Connection_getSubscriptions()\n");
  return result;
}

static PyObject *
Connection_createSubscription (Connection *self, PyObject *args,
			       PyObject *kwds)
{
  PyObject *resource_uriobj;
  char *resource_uri;
  PyObject *events = NULL;
  int job_id = -1, lease_duration = -1, time_interval = -1;
  PyObject *recipient_uriobj = NULL, *user_dataobj = NULL;
  char *recipient_uri = NULL, *user_data = NULL;
  ipp_t *request, *answer;
  int i, n = 0;
  ipp_attribute_t *attr;
  static char *kwlist[] = { "uri", "events", "job_id", "recipient_uri",
			    "lease_duration", "time_interval", "user_data",
			    NULL };

  if (!PyArg_ParseTupleAndKeywords (args, kwds, "O|OiOiiO", kwlist,
				    &resource_uriobj, &events, &job_id,
				    &recipient_uriobj, &lease_duration,
				    &time_interval, &user_dataobj))
    return NULL;

  if (UTF8_from_PyObj (&resource_uri, resource_uriobj) == NULL)
    return NULL;

  if (recipient_uriobj &&
      UTF8_from_PyObj (&recipient_uri, recipient_uriobj) == NULL) {
    free (resource_uri);
    return NULL;
  }

  if (user_dataobj && UTF8_from_PyObj (&user_data, user_dataobj) == NULL) {
    free (resource_uri);
    if (recipient_uriobj)
      free (recipient_uri);
    return NULL;
  }

  if (events) {
    if (!PyList_Check (events)) {
      PyErr_SetString (PyExc_TypeError, "events must be a list");
      return NULL;
    }

    n = PyList_Size (events);
    for (i = 0; i < n; i++) {
      PyObject *event = PyList_GetItem (events, i);
      if (!PyString_Check (event)) {
	PyErr_SetString (PyExc_TypeError, "events must be a list of strings");
	return NULL;
      }
    }
  }

  debugprintf ("-> Connection_createSubscription(%s)\n", resource_uri);
  request = ippNewRequest (IPP_CREATE_PRINTER_SUBSCRIPTION);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, resource_uri);
  ippAddString (request, IPP_TAG_SUBSCRIPTION, IPP_TAG_KEYWORD,
		"notify-pull-method", NULL, "ippget");
  ippAddString (request, IPP_TAG_SUBSCRIPTION, IPP_TAG_CHARSET,
		"notify-charset", NULL, "utf-8");
  ippAddString (request, IPP_TAG_SUBSCRIPTION, IPP_TAG_NAME,
		"requesting-user-name", NULL, cupsUser ());

  if (recipient_uriobj) {
    ippAddString (request, IPP_TAG_SUBSCRIPTION, IPP_TAG_URI,
		  "notify-recipient-uri", NULL, recipient_uri);
    free (recipient_uri);
  }

  if (user_dataobj) {
    ippAddString (request, IPP_TAG_SUBSCRIPTION, IPP_TAG_STRING,
		  "notify-user-data", NULL, user_data);
    free (user_data);
  }

  if (events) {
    attr = ippAddStrings (request, IPP_TAG_SUBSCRIPTION,
			  IPP_TAG_KEYWORD, "notify-events",
			  n, NULL, NULL);
    for (i = 0; i < n; i++) {
      PyObject *event = PyList_GetItem (events, i);
      attr->values[i].string.text = strdup (PyString_AsString (event));
    }
  }

  if (lease_duration != -1)
    ippAddInteger (request, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER,
		   "notify-lease-duration", lease_duration);

  if (time_interval != -1)
    ippAddInteger (request, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER,
		   "notify-time-interval", time_interval);

  if (job_id != -1)
    ippAddInteger (request, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER,
		   "notify-job-id", job_id);

  answer = cupsDoRequest (self->http, request, "/");
  if (!answer || answer->request.status.status_code > IPP_OK_CONFLICT) {
    set_ipp_error (answer ? answer->request.status.status_code :
		   cupsLastError ());
    if (answer)
      ippDelete (answer);
    debugprintf ("<- Connection_createSubscription() EXCEPTION\n");
    return NULL;
  }

  i = -1;
  for (attr = answer->attrs; attr; attr = attr->next) {
    if (attr->group_tag == IPP_TAG_SUBSCRIPTION) {
      if (attr->value_tag == IPP_TAG_INTEGER &&
	  !strcmp (attr->name, "notify-subscription-id"))
	i = attr->values[0].integer;
      else if (attr->value_tag == IPP_TAG_ENUM &&
	       !strcmp (attr->name, "notify-status-code"))
	debugprintf ("notify-status-code = %d\n", attr->values[0].integer);
    }
  }

  ippDelete (answer);
  debugprintf ("<- Connection_createSubscription() = %d\n", i);
  return PyInt_FromLong (i);
}

static PyObject *
Connection_getNotifications (Connection *self, PyObject *args, PyObject *kwds)
{
  PyObject *subscription_ids, *sequence_numbers = NULL;
  ipp_t *request, *answer;
  int i, num_ids, num_seqs;
  ipp_attribute_t *attr;
  PyObject *result, *events, *event;
  static char *kwlist[] = { "subscription_ids", "sequence_numbers", NULL };

  if (!PyArg_ParseTupleAndKeywords (args, kwds, "O|O", kwlist,
				    &subscription_ids, &sequence_numbers))
    return NULL;

  if (!PyList_Check (subscription_ids)) {
    PyErr_SetString (PyExc_TypeError, "subscriptions_ids must be a list");
    return NULL;
  }

  num_ids = PyList_Size (subscription_ids);
  for (i = 0; i < num_ids; i++) {
    PyObject *id = PyList_GetItem (subscription_ids, i);
    if (!PyInt_Check (id)) {
      PyErr_SetString (PyExc_TypeError, "subscription_ids must be a list "
		       "of integers");
      return NULL;
    }
  }

  if (sequence_numbers) {
    if (!PyList_Check (sequence_numbers)) {
      PyErr_SetString (PyExc_TypeError, "sequence_numbers must be a list");
      return NULL;
    }

    num_seqs = PyList_Size (sequence_numbers);
    for (i = 0; i < num_seqs; i++) {
      PyObject *id = PyList_GetItem (sequence_numbers, i);
      if (!PyInt_Check (id)) {
	PyErr_SetString (PyExc_TypeError, "sequence_numbers must be a list "
			 "of integers");
	return NULL;
      }
    }
  }

  debugprintf ("-> Connection_getNotifications()\n");
  request = ippNewRequest (IPP_GET_NOTIFICATIONS);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, "/");
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		"requesting-user-name", NULL, cupsUser ());

  attr = ippAddIntegers (request, IPP_TAG_OPERATION, IPP_TAG_INTEGER,
			 "notify-subscription-ids", num_ids, NULL);
  for (i = 0; i < num_ids; i++) {
    PyObject *id = PyList_GetItem (subscription_ids, i);
    attr->values[i].integer = PyInt_AsLong (id);
  }
  
  answer = cupsDoRequest (self->http, request, "/");
  if (!answer || answer->request.status.status_code > IPP_OK_CONFLICT) {
    set_ipp_error (answer ? answer->request.status.status_code :
		   cupsLastError ());
    if (answer)
      ippDelete (answer);
    debugprintf ("<- Connection_getNotifications() EXCEPTION\n");
    return NULL;
  }

  result = PyDict_New ();

  // Result-wide attributes.
  attr = ippFindAttribute (answer, "notify-get-interval", IPP_TAG_INTEGER);
  if (attr) {
    PyObject *val = PyInt_FromLong (attr->values[0].integer);
    PyDict_SetItemString (result, attr->name, val);
    Py_DECREF (val);
  }

  attr = ippFindAttribute (answer, "printer-up-time", IPP_TAG_INTEGER);
  if (attr) {
    PyObject *val = PyInt_FromLong (attr->values[0].integer);
    PyDict_SetItemString (result, attr->name, val);
    Py_DECREF (val);
  }

  events = PyList_New (0);
  for (attr = answer->attrs; attr; attr = attr->next)
    if (attr->group_tag == IPP_TAG_EVENT_NOTIFICATION)
      break;

  event = NULL;
  for (; attr; attr = attr->next) {
    PyObject *obj;
    if (attr->group_tag == IPP_TAG_ZERO) {
      // End of event notification.
      if (event) {
	PyList_Append (events, event);
	Py_DECREF (event);
      }

      event = NULL;
      continue;
    }

    if (attr->num_values > 1 || !strcmp (attr->name, "notify-events")) {
      obj = PyList_New (0);
      for (i = 0; i < attr->num_values; i++) {
	PyObject *item = PyObject_from_attr_value (attr, i);
	if (item) {
	  PyList_Append (obj, item);
	  Py_DECREF (item);
	}
      }
    }
    else
      obj = PyObject_from_attr_value (attr, 0);

    if (!obj)
      // Can't represent this.
      continue;

    if (!event)
      event = PyDict_New ();

    PyDict_SetItemString (event, attr->name, obj);
    Py_DECREF (obj);
  }

  if (event) {
    PyList_Append (events, event);
    Py_DECREF (event);
  }

  ippDelete (answer);
  PyDict_SetItemString (result, "events", events);
  debugprintf ("<- Connection_getNotifications()\n");
  return result;
}

static PyObject *
Connection_renewSubscription (Connection *self, PyObject *args)
{
  int id;
  ipp_t *request, *answer;
  ipp_attribute_t *attr;

  if (!PyArg_ParseTuple (args, "i", &id))
    return NULL;

  debugprintf ("-> Connection_renewSubscription()\n");
  request = ippNewRequest (IPP_RENEW_SUBSCRIPTION);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, "/");
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		"requesting-user-name", NULL, cupsUser ());
  attr = ippAddInteger (request, IPP_TAG_OPERATION, IPP_TAG_INTEGER,
			 "notify-subscription-id", id);

  answer = cupsDoRequest (self->http, request, "/");
  if (!answer || answer->request.status.status_code > IPP_OK_CONFLICT) {
    set_ipp_error (answer ? answer->request.status.status_code :
		   cupsLastError ());
    if (answer)
      ippDelete (answer);
    debugprintf ("<- Connection_renewSubscription() EXCEPTION\n");
    return NULL;
  }

  ippDelete (answer);
  debugprintf ("<- Connection_renewSubscription()\n");
  Py_INCREF (Py_None);
  return Py_None;
}

static PyObject *
Connection_cancelSubscription (Connection *self, PyObject *args)
{
  int id;
  ipp_t *request, *answer;
  ipp_attribute_t *attr;

  if (!PyArg_ParseTuple (args, "i", &id))
    return NULL;

  debugprintf ("-> Connection_cancelSubscription()\n");
  request = ippNewRequest (IPP_CANCEL_SUBSCRIPTION);
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, "/");
  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		"requesting-user-name", NULL, cupsUser ());
  attr = ippAddInteger (request, IPP_TAG_OPERATION, IPP_TAG_INTEGER,
			 "notify-subscription-id", id);

  answer = cupsDoRequest (self->http, request, "/");
  if (!answer || answer->request.status.status_code > IPP_OK_CONFLICT) {
    set_ipp_error (answer ? answer->request.status.status_code :
		   cupsLastError ());
    if (answer)
      ippDelete (answer);
    debugprintf ("<- Connection_cancelSubscription() EXCEPTION\n");
    return NULL;
  }

  ippDelete (answer);
  debugprintf ("<- Connection_cancelSubscription()\n");
  Py_INCREF (Py_None);
  return Py_None;
}

PyMethodDef Connection_methods[] =
  {
    { "getPrinters",
      (PyCFunction) Connection_getPrinters, METH_NOARGS,
      "getPrinters() -> dict\n\n"
      "@return: a dict, indexed by name, of dicts representing\n"
      "queues, indexed by attribute.\n"
      "@raise IPPError: IPP problem" },

    { "getDests",
      (PyCFunction) Connection_getDests, METH_NOARGS,
      "getDests() -> dict\n\n"
      "@return: a dict representing available destinations.  Each \n"
      "dictionary key is a pair of (queue, instance) strings, and the \n"
      "dictionary value is a L{cups.Dest} object.  In addition to the \n"
      "available destinations, a special dictionary key (None,None) is \n"
      "provided for looking up the default destination; this destination \n"
      "will also be available under its own key.\n"
      "@raise IPPError: IPP problem" },

    { "getClasses",
      (PyCFunction) Connection_getClasses, METH_NOARGS,
      "getClasses() -> dict\n\n"
      "@return: a dict, indexed by name, of objects representing\n"
      "classes.  Each class object is either a string, in which case it\n"
      "is for the remote class; or a list, in which case it is a list of\n"
      "queue names.\n"
      "@raise IPPError: IPP problem" },

    { "getPPDs",
      (PyCFunction) Connection_getPPDs, METH_NOARGS,
      "getPPDs() -> dict\n\n"
      "@return: a dict, indexed by PPD name, of dicts representing\n"
      "PPDs, indexed by attribute.\n"
      "@raise IPPError: IPP problem" },

    { "getServerPPD",
      (PyCFunction) Connection_getServerPPD, METH_VARARGS,
      "getServerPPD(ppd_name) -> string\n\n"
      "Fetches the named PPD and stores it in a temporary file.\n\n"
      "@type ppd_name: string\n"
      "@param ppd_name: the ppd-name of a PPD\n"
      "@return: temporary filename holding the PPD\n"
      "@raise RuntimeError: Not supported in libcups until 1.3\n"
      "@raise IPPError: IPP problem" },
    
    { "getDevices",
      (PyCFunction) Connection_getDevices, METH_NOARGS,
      "getDevices() -> dict\n\n"
      "@return: a dict, indexed by device URI, of dicts representing\n"
      "devices, indexed by attribute.\n"
      "@raise IPPError: IPP problem" },    

    { "getJobs",
      (PyCFunction) Connection_getJobs, METH_VARARGS | METH_KEYWORDS,
      "getJobs(which_jobs='not-completed', my_jobs=False) -> dict\n"
      "Fetch a list of jobs.\n"
      "@type which_jobs: string\n"
      "@param which_jobs: which jobs to fetch; possible values: \n"
      "'completed', 'not-completed', 'all'\n"
      "@type my_jobs: boolean\n"
      "@param my_jobs: whether to restrict the returned jobs to those \n"
      "owned by the current CUPS user (as set by L{cups.setUser}).\n"
      "@return: a dict, indexed by job ID, of dicts representing job\n"
      "attributes.\n"
      "@raise IPPError: IPP problem" },

    { "cancelJob",
      (PyCFunction) Connection_cancelJob, METH_VARARGS,
      "cancelJob(jobid) -> None\n\n"
      "@type jobid: integer\n"
      "@param jobid: job ID to cancel\n"
      "@raise IPPError: IPP problem" },

    { "setJobHoldUntil",
      (PyCFunction) Connection_setJobHoldUntil, METH_VARARGS,
      "setJobHoldUntil(jobid, job_hold_until) -> None\n\n"
      "Specifies when a job should be printed.\n"
      "@type jobid: integer\n"
      "@param jobid: job ID to adjust\n"
      "@type job_hold_until: string\n"
      "@param job_hold_until: when to print the job; examples: 'hold', \n"
      "'immediate', 'restart', resume'\n"
      "@raise IPPError: IPP problem"},
    
    { "restartJob",
      (PyCFunction) Connection_restartJob, METH_VARARGS,
      "restartJob(jobid) -> None\n\n"
      "Restart a job.\n\n"
      "@type jobid: integer\n"
      "@param jobid: job ID to restart\n"
      "@raise IPPError: IPP problem" },

    { "getFile",
      (PyCFunction) Connection_getFile, METH_VARARGS,
      "getFile(resource, filename) -> None\n\n"
      "Fetch a CUPS server resource to a local file.\n\n"
      "This is for obtaining CUPS server configuration files and \n"
      "log files.\n\n"
      "@type resource: string\n"
      "@param resource: resource name\n"
      "@type filename: string\n"
      "@param filename: name of local file for storage\n"
      "@raise HTTPError: HTTP problem" },

    { "putFile",
      (PyCFunction) Connection_putFile, METH_VARARGS,
      "putFile(resource, filename) -> None\n\n"
      "This is for uploading new configuration files for the CUPS \n"
      "server.  Note: L{adminSetServerSettings} is a way of \n"
      "adjusting server settings without needing to parse the \n"
      "configuration file.\n"
      "@type resource: string\n"
      "@param resource: resource name\n"
      "@type filename: string\n"
      "@param filename: name of local file to upload\n"
      "@raise HTTPError: HTTP problem"},

    { "addPrinter",
      (PyCFunction) Connection_addPrinter, METH_VARARGS | METH_KEYWORDS,
      "addPrinter(name) -> None\n\n"
      "Add or adjust a print queue.  Several parameters can select which\n"
      "PPD to use (filename, ppdname, and ppd) but only one may be\n"
      "given.\n\n"
      "@type filename: string\n"
      "@keyword filename: local filename of PPD file\n"
      "@type ppdname: string\n"
      "@keyword ppdname: filename from L{getPPDs}\n"
      "@type info: string\n"
      "@keyword info: human-readable information about the printer\n"
      "@type location: string\n"
      "@keyword location: human-readable printer location\n"
      "@type device: string\n"
      "@keyword device: device URI string\n"
      "@type ppd: L{cups.PPD} instance\n"
      "@keyword ppd: PPD object\n"
      "@raise IPPError: IPP problem" },

    { "setPrinterDevice",
      (PyCFunction) Connection_setPrinterDevice, METH_VARARGS,
      "setPrinterDevice(name, device_uri) -> None\n\n"
      "Set the device URI for a printer.\n\n"
      "@type name: string\n"
      "@param name: queue name\n"
      "@type device_uri: string\n"
      "@param device_uri: device URI\n"
      "@raise IPPError: IPP problem" },

    { "setPrinterInfo",
      (PyCFunction) Connection_setPrinterInfo, METH_VARARGS,
      "setPrinterInfo(name, info) -> None\n\n"
      "Set the human-readable information about a printer.\n\n"
      "@type name: string\n"
      "@param name: queue name\n"
      "@type info: string\n"
      "@param info: human-readable information about the printer\n"
      "@raise IPPError: IPP problem" },

    { "setPrinterLocation",
      (PyCFunction) Connection_setPrinterLocation, METH_VARARGS,
      "setPrinterLocation(name, location) -> None\n\n"
      "Set the human-readable printer location\n\n"
      "@type name: string\n"
      "@param name: queue name\n"
      "@type location: string\n"
      "@param location: human-readable printer location\n"
      "@raise IPPError: IPP problem" },

    { "setPrinterShared",
      (PyCFunction) Connection_setPrinterShared, METH_VARARGS,
      "setPrinterShared(name, shared) -> None\n\n"
      "Set whether a printer is shared with other people.  This works \n"
      "with CUPS servers of at least version 1.2, by setting the \n"
      "printer-is-shared printer attribute.\n\n"
      "@type name: string\n"
      "@param name: queue name\n"
      "@type shared: boolean\n"
      "@param shared: whether printer should be shared\n"
      "@raise IPPError: IPP problem" },

    { "setPrinterJobSheets",
      (PyCFunction) Connection_setPrinterJobSheets, METH_VARARGS,
      "setPrinterJobSheets(name, start, end) -> None\n\n"
      "Specifies job sheets for a printer.\n\n"
      "@type name: string\n"
      "@param name: queue name\n"
      "@type start: string\n"
      "@param start: name of a sheet to print before each job\n"
      "@type end: string\n"
      "@param end: name of a sheet to print after each job\n"
      "@raise IPPError: IPP problem" },

    { "setPrinterErrorPolicy",
      (PyCFunction) Connection_setPrinterErrorPolicy, METH_VARARGS,
      "setPrinterErrorPolicy(name, policy) -> None\n\n"
      "Set the printer's error policy.\n\n"
      "@type name: string\n"
      "@param name: queue name\n"
      "@type policy: string\n"
      "@param policy: policy name; supported policy names can be found \n"
      "by using the L{getPrinterAttributes} function and looking for the \n"
      "'printer-error-policy-supported' attribute\n"
      "@raise IPPError: IPP problem" },

    { "setPrinterOpPolicy",
      (PyCFunction) Connection_setPrinterOpPolicy, METH_VARARGS,
      "setPrinterOpPolicy(name, policy) -> None\n\n"
      "Set the printer's operation policy.\n\n"
      "@type name: string\n"
      "@param name: queue name\n"
      "@type policy: string\n"
      "@param policy: policy name; supported policy names can be found \n"
      "by using the L{getPrinterAttributes} function and looking for the \n"
      "'printer-op-policy-supported' attribute\n"
      "@raise IPPError: IPP problem" },

    { "setPrinterUsersAllowed",
      (PyCFunction) Connection_setPrinterUsersAllowed, METH_VARARGS,
      "setPrinterUsersAllowed(name, allowed) -> None\n\n"
      "Set the list of users allowed to use a printer.  This works \n"
      "with CUPS server of at least version 1.2, by setting the \n"
      "requesting-user-name-allowed printer attribute.\n\n"
      "@type name: string\n"
      "@param name: queue name\n"
      "@type allowed: string list\n"
      "@param allowed: list of allowed users; ['all'] \n"
      "means there will be no user-name restriction.\n"
      "@raise IPPError: IPP problem" },

    { "setPrinterUsersDenied",
      (PyCFunction) Connection_setPrinterUsersDenied, METH_VARARGS,
      "setPrinterUsersDenied(name, denied) -> None\n\n"
      "Set the list of users denied the use of a printer.  This works \n"
      "with CUPS servers of at least version 1.2, by setting the \n"
      "requesting-user-name-denied printer attribute.\n\n"
      "@type name: string\n"
      "@param name: queue name\n"
      "@type denied: string list\n"
      "@param denied: list of denied users; ['none'] \n"
      "means there will be no user-name restriction.\n"
      "@raise IPPError: IPP problem" },

    { "addPrinterOptionDefault",
      (PyCFunction) Connection_addPrinterOptionDefault, METH_VARARGS,
      "addPrinterOptionDefault(name, option, value) -> None\n\n"
      "Set a network default option.  Jobs submitted to the named queue \n"
      "will have the job option added if it is not already present in the \n"
      "job.  This works with CUPS servers of at least version 1.2.\n\n"
      "@type name: string\n"
      "@param name: queue name\n"
      "@type option: string\n"
      "@param option: option name, for example 'job-priority'\n"
      "@type value: string\n"
      "@param value: option value as a string\n"
      "@raise IPPError: IPP problem" },

    { "deletePrinterOptionDefault",
      (PyCFunction) Connection_deletePrinterOptionDefault, METH_VARARGS,
      "deletePrinterOptionDefault(name, option) -> None\n\n"
      "Removes a network default option.  See L{addPrinterOptionDefault}.\n\n"
      "@type name: string\n"
      "@param name: queue name\n"
      "@type option: string\n"
      "@param option: option name, for example 'job-priority'\n"
      "@raise IPPError: IPP problem" },

    { "deletePrinter",
      (PyCFunction) Connection_deletePrinter, METH_VARARGS | METH_KEYWORDS,
      "deletePrinter(name) -> None\n\n"
      "Delete a printer.\n\n"
      "@type name: string\n"
      "@param name: queue name\n"
      "@raise IPPError: IPP problem" },

    { "getPrinterAttributes",
      (PyCFunction) Connection_getPrinterAttributes, METH_VARARGS,
      "getPrinterAttributes(name) -> dict\n"
      "Fetch the attributes for a printer.\n\n"
      "@type name: string\n"
      "@param name: queue name\n"
      "@return: a dict, indexed by attribute, of printer attributes\n"
      "for the printer 'name'.\n\n"
      "Attributes:\n"
      "  - 'job-sheets-supported': list of strings\n"
      "  - 'job-sheets-default': tuple of strings (start, end)\n"
      "  - 'printer-error-policy-supported': if present, list of strings\n"
      "  - 'printer-error-policy': if present, string\n"
      "  - 'printer-op-policy-supported': if present, list of strings\n"
      "  - 'printer-op-policy': if present, string\n\n"
      "There are other attributes; the exact list of attributes returned \n"
      "will depend on the CUPS server.\n"
      "@raise IPPError: IPP problem"},

    { "addPrinterToClass",
      (PyCFunction) Connection_addPrinterToClass, METH_VARARGS,
      "addPrinterToClass(name, class) -> None\n\n"
      "Add a printer to a class.  If the class does not yet exist, \n"
      "it is created.\n\n"
      "@type name: string\n"
      "@param name: queue name\n"
      "@type class: string\n"
      "@param class: class name\n"
      "@raise IPPError: IPP problem" },

    { "deletePrinterFromClass",
      (PyCFunction) Connection_deletePrinterFromClass, METH_VARARGS,
      "deletePrinterFromClass(name, class) -> None\n\n"
      "Remove a printer from a class.  If the class would be left empty, \n"
      "it is removed.\n\n"
      "@type name: string\n"
      "@param name: queue name\n"
      "@type class: string\n"
      "@param class: class name\n"
      "@raise IPPError: IPP problem" },

    { "deleteClass",
      (PyCFunction) Connection_deleteClass, METH_VARARGS,
      "deleteClass(class) -> None\n\n"
      "Delete a class.\n\n"
      "@type class: string\n"
      "@param class: class name\n"
      "@raise IPPError: IPP problem" },

    { "getDefault",
      (PyCFunction) Connection_getDefault, METH_NOARGS,
      "getDefault() -> string or None\n\n"
      "Get the system default printer.\n\n"
      "@return: default printer name or None" },

    { "setDefault",
      (PyCFunction) Connection_setDefault, METH_VARARGS | METH_KEYWORDS,
      "setDefault(name) -> None\n\n"
      "Set the system default printer.  Note that this can be over-ridden \n"
      "on a per-user basis using the lpoptions command.\n\n"
      "@type name: string\n"
      "@param name: queue name\n"
      "@raise IPPError: IPP problem" },

    { "getPPD",
      (PyCFunction) Connection_getPPD, METH_VARARGS,
      "getPPD(name) -> string\n\n"
      "Fetch a printer's PPD.\n\n"
      "@type name: string\n"
      "@param name: queue name\n"
      "@return: temporary PPD file name\n"
      "@raise IPPError: IPP problem" },

    { "enablePrinter",
      (PyCFunction) Connection_enablePrinter, METH_VARARGS | METH_KEYWORDS,
      "enablePrinter(name) -> None\n\n"
      "Enable printer.  This allows the printer to process its job queue.\n\n"
      "@type name: string\n"
      "@param name: queue name\n"
      "@raise IPPError: IPP problem" },

    { "disablePrinter",
      (PyCFunction) Connection_disablePrinter, METH_VARARGS | METH_KEYWORDS,
      "disablePrinter(name) -> None\n\n"
      "Disable printer.  This prevents the printer from processing its \n"
      "job queue.\n\n"
      "@type name: string\n"
      "@param name: queue name\n"
      "@type reason: string\n"
      "@keyword reason: optional human-readable reason for disabling the \n"
      "printer\n"
      "@raise IPPError: IPP problem" },

    { "acceptJobs",
      (PyCFunction) Connection_acceptJobs, METH_VARARGS | METH_KEYWORDS,
      "acceptJobs(name) -> None\n\n"
      "Cause printer to accept jobs.\n"
      "@type name: string\n"
      "@param name: queue name\n"
      "@raise IPPError: IPP problem" },

    { "rejectJobs",
      (PyCFunction) Connection_rejectJobs, METH_VARARGS | METH_KEYWORDS,
      "rejectJobs(name)\n\n"
      "Cause printer to reject jobs.\n\n"
      "@type name: string\n"
      "@param name: queue name\n"
      "@type reason: string\n"
      "@keyword reason: optional human-readable reason for rejecting jobs\n"
      "@raise IPPError: IPP problem" },

    { "printTestPage",
      (PyCFunction) Connection_printTestPage, METH_VARARGS | METH_KEYWORDS,
      "printTestPage(name) -> job ID\n\n"
      "Print a test page.\n\n"
      "@type name: string\n"
      "@param name: queue name\n"
      "@type file: string\n"
      "@keyword file: input file (default is CUPS test page)\n"
      "@type title: string\n"
      "@keyword title: job title (default 'Test Page')\n"
      "@type format: string\n"
      "@keyword format: document format (default 'application/postscript')\n"
      "@type user: string\n"
      "@keyword user: user to submit the job as (default 'guest')\n"
      "@raise IPPError: IPP problem" },

    { "adminGetServerSettings",
      (PyCFunction) Connection_adminGetServerSettings, METH_NOARGS,
      "adminGetServerSettings() -> dict\n\n"
      "Get server settings.\n\n"
      "@return: dict representing server settings; keywords include \n"
      "L{CUPS_SERVER_DEBUG_LOGGING}, L{CUPS_SERVER_REMOTE_ADMIN}, \n"
      "L{CUPS_SERVER_REMOTE_PRINTERS}, L{CUPS_SERVER_SHARE_PRINTERS}, \n"
      "L{CUPS_SERVER_USER_CANCEL_ANY}\n"
      "@see: L{adminSetServerSettings}\n"
      "@raise IPPError: IPP problem" },

    { "adminSetServerSettings",
      (PyCFunction) Connection_adminSetServerSettings, METH_VARARGS,
      "adminSetServerSettings(settings) -> None\n\n"
      "Set server settings.\n\n"
      "@type settings: dict\n"
      "@param settings: dict of server settings\n"
      "@see: L{adminGetServerSettings}\n"
      "@raise IPPError: IPP problem" },

    { "getSubscriptions",
      (PyCFunction) Connection_getSubscriptions, METH_VARARGS | METH_KEYWORDS,
      "getSubscriptions(uri) -> integer list\n\n"
      "Get subscriptions.\n\n"
      "@type uri: string\n"
      "@param uri: URI for object\n"
      "@type my_subscriptions: boolean\n"
      "@keyword my_subscriptions: only return subscriptions belonging to \n"
      "the current user (default False)\n"
      "@type job_id: integer\n"
      "@keyword job_id: only return subscriptions relating to this job\n"
      "@return: list of subscriptions\n"
      "@raise IPPError: IPP problem" },

    { "createSubscription",
      (PyCFunction) Connection_createSubscription,
      METH_VARARGS | METH_KEYWORDS,
      "createSubscription(uri) -> integer\n\n"
      "Create a subscription.\n\n"
      "@type uri: string\n"
      "@param uri: URI for object\n"
      "@type events: string list\n"
      "@keyword events: events to receive notifications for\n"
      "@type job_id: integer\n"
      "@keyword job_id: job ID to receive notifications for\n"
      "@type recipient_uri: string\n"
      "@keyword recipient_uri: URI for notifications recipient\n"
      "@type lease_duration: integer\n"
      "@keyword lease_duration: lease duration in seconds\n"
      "@type time_interval: integer\n"
      "@keyword time_interval: time interval\n"
      "@type user_data: string\n"
      "@keyword user_data: user data to receieve with notifications\n"
      "@return: subscription ID\n"
      "@raise IPPError: IPP problem" },

    { "getNotifications",
      (PyCFunction) Connection_getNotifications, METH_VARARGS | METH_KEYWORDS,
      "getNotifications(subscription_ids) -> list\n\n"
      "Get notifications for subscribed events.\n\n"
      "@type subscription_ids: integer list\n"
      "@param subscription_ids: list of subscription IDs to receive \n"
      "notifications for\n"
      "@return: list of dicts, each representing an event\n"
      "@raise IPPError: IPP problem" },

    { "cancelSubscription",
      (PyCFunction) Connection_cancelSubscription, METH_VARARGS,
      "cancelSubscription(id) -> None\n\n"
      "Cancel a subscription.\n\n"
      "@type id: integer\n"
      "@param id: subscription ID\n"
      "@raise IPPError: IPP problem" },

    { "renewSubscription",
      (PyCFunction) Connection_renewSubscription, METH_VARARGS,
      "renewSubscription(id) -> None\n\n"
      "Renew a subscription.\n\n"
      "@type id: integer\n"
      "@param id: subscription ID\n"
      "@raise IPPError: IPP problem" },

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
    "CUPS connection\n"
    "===============\n\n"

    "  A connection to the CUPS server.  Before it is created the \n"
    "  connection server and username should be set using \n"
    "  L{cups.setServer} and L{cups.setUser}; otherwise the defaults will \n"
    "  be used.  When a Connection object is instantiated it results in a "
    "  call to the libcups function httpConnectEncrypt()."
    "",         /* tp_doc */
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

//////////
// Dest //
//////////

static PyObject *
Dest_new (PyTypeObject *type, PyObject *args, PyObject *kwds)
{
  Dest *self;
  self = (Dest *) type->tp_alloc (type, 0);
  return (PyObject *) self;
}

static int
Dest_init (Dest *self, PyObject *args, PyObject *kwds)
{    
  self->num_options = 0;
  return 0;
}

static void
Dest_dealloc (Dest *self)
{
  if (self->num_options) {
    int i;
    for (i = 0; i < self->num_options; i++) {
      free (self->name[i]);
      free (self->value[i]);
    }

    free (self->name);
    free (self->value);
    self->num_options = 0;

    free (self->destname);
    free (self->instance);
  }
  self->ob_type->tp_free ((PyObject *) self);
}

//////////
// Dest // ATTRIBUTES
//////////

static PyObject *
Dest_getName (Dest *self, void *closure)
{
  return PyString_FromString (self->destname);
}

static PyObject *
Dest_getInstance (Dest *self, void *closure)
{
  if (self->instance)
    return PyString_FromString (self->instance);

  Py_INCREF (Py_None);
  return Py_None;
}

static PyObject *
Dest_getIsDefault (Dest *self, void *closure)
{
  return PyBool_FromLong (self->is_default);
}

static PyObject *
Dest_getOptions (Dest *self, void *closure)
{
  PyObject *pyoptions = PyDict_New ();
  int i;
  for (i = 0; i < self->num_options; i++)
    PyDict_SetItemString (pyoptions, self->name[i],
			  PyString_FromString (self->value[i]));

  return pyoptions;
}

PyGetSetDef Dest_getseters[] =
  {
    { "name",
      (getter) Dest_getName, (setter) NULL,
      "name", NULL },

    { "instance",
      (getter) Dest_getInstance, (setter) NULL,
      "instance", NULL },

    { "is_default",
      (getter) Dest_getIsDefault, (setter) NULL,
      "is_default", NULL },
  
    { "options",
      (getter) Dest_getOptions, (setter) NULL,
      "options", NULL },

    { NULL }
  };

PyTypeObject cups_DestType =
  {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "cups.Dest",               /*tp_name*/
    sizeof(Dest),              /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)Dest_dealloc,  /*tp_dealloc*/
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
    "CUPS destination\n"
    "================\n\n"
    "  A destination print queue, as returned by L{Connection.getDests}.\n\n"
    "@type name: string\n"
    "@ivar name: destination queue name\n"
    "@type instance: string\n"
    "@ivar instance: destination instance name\n"
    "@type is_default: boolean\n"
    "@ivar is_default: whether this is the default destination\n"
    "@type options: dict\n"
    "@ivar options: string:string dict of default options for this \n"
    "destination, indexed by option name\n"
    "",        /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    0,                         /* tp_methods */
    0,                         /* tp_members */
    Dest_getseters,            /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)Dest_init,       /* tp_init */
    0,                         /* tp_alloc */
    Dest_new,                  /* tp_new */
  };
