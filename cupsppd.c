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

#include "cupsppd.h"
#include "cupsmodule.h"

typedef struct
{
  PyObject_HEAD
  ppd_option_t *option;
  PPD *ppd;
} Option;

typedef struct
{
  PyObject_HEAD
  ppd_const_t *constraint;
  PPD *ppd;
} Constraint;

typedef struct
{
  PyObject_HEAD
  ppd_group_t *group;
  PPD *ppd;
} Group;

/////////
// PPD //
/////////

static PyObject *
PPD_new (PyTypeObject *type, PyObject *args, PyObject *kwds)
{
  PPD *self;
  self = (PPD *) type->tp_alloc (type, 0);
  if (self != NULL) {
    self->ppd = NULL;
    self->file = NULL;
  }

  return (PyObject *) self;
}

static int
PPD_init (PPD *self, PyObject *args, PyObject *kwds)
{
  const char *filename;

  if (!PyArg_ParseTuple (args, "s", &filename))
    return -1;

  self->file = fopen (filename, "r");
  self->ppd = ppdOpenFile (filename);
  if (!self->ppd) {
    fclose (self->file);
    self->file = NULL;
    PyErr_SetString (PyExc_RuntimeError, "ppdOpenFile failed");
    return -1;
  }

  return 0;
}

static void
PPD_dealloc (PPD *self)
{
  if (self->file)
    fclose (self->file);
  if (self->ppd)
    ppdClose (self->ppd);

  self->ob_type->tp_free ((PyObject *) self);
}

/////////
// PPD // METHODS
/////////

static PyObject *
PPD_markDefaults (PPD *self)
{
  ppdMarkDefaults (self->ppd);
  Py_INCREF (Py_None);
  return Py_None;
}

static PyObject *
PPD_markOption (PPD *self, PyObject *args)
{
  PyObject *ret;
  int conflicts;
  const char *name, *value;
  if (!PyArg_ParseTuple (args, "ss", &name, &value))
    return NULL;

  conflicts = ppdMarkOption (self->ppd, name, value);
  ret = Py_BuildValue ("i", conflicts);
  return ret;
}

static PyObject *
PPD_conflicts (PPD *self)
{
  return PyInt_FromLong (ppdConflicts (self->ppd));
}

PyObject *
PPD_writeFd (PPD *self, PyObject *args)
{
  char *line = NULL;
  size_t linelen = 0;
  FILE *out;
  int fd;
  int dfd;
  if (!PyArg_ParseTuple (args, "i", &fd))
    return NULL;

  dfd = dup (fd);
  if (!dfd) {
    PyErr_SetFromErrno (PyExc_RuntimeError);
    return NULL;
  }

  out = fdopen (dfd, "w");
  if (!out) {
    PyErr_SetFromErrno (PyExc_RuntimeError);
    return NULL;
  }

  rewind (self->file);
  while (!feof (self->file)) {
    int written = 0;
    ssize_t got = getline (&line, &linelen, self->file);
    if (got == -1)
      break;

    if (!strncmp (line, "*Default", 8)) {
      char *keyword;
      char *start = line + 8;
      char *end;
      ppd_choice_t *choice;
      for (end = start; *end; end++)
	if (isspace (*end) || *end == ':')
	  break;
      keyword = strndup (start, end-start);
      choice = ppdFindMarkedChoice (self->ppd, keyword);
      if (choice) {
	fprintf (out, "*Default%s: %s", keyword, choice->choice);
	if (strchr (end, '\r'))
	  fputs ("\r", out);
	fputs ("\n", out);
	written = 1;
      }
    }

    if (!written)
      fputs (line, out);
  }

  fclose (out);
  if (line)
    free (line);

  Py_INCREF (Py_None);
  return Py_None;
}

/////////
// PPD // ATTRIBUTES
/////////

static PyObject *
PPD_getConstraints (PPD *self, void *closure)
{
  PyObject *ret = PyList_New (0);
  ppd_const_t *c;
  int i;
  for (i = 0, c = self->ppd->consts;
       i < self->ppd->num_consts;
       i++, c++) {
    PyObject *args = Py_BuildValue ("()");
    PyObject *kwlist = Py_BuildValue ("{}");
    Constraint *cns = (Constraint *) PyType_GenericNew (&cups_ConstraintType,
							args, kwlist);
    Py_DECREF (args);
    Py_DECREF (kwlist);
    cns->constraint = c;
    cns->ppd = self;
    Py_INCREF (self);
    PyList_Append (ret, (PyObject *) cns);
  }

  return ret;
}

static PyObject *
PPD_getOptionGroups (PPD *self, void *closure)
{
  PyObject *ret = PyList_New (0);
  ppd_group_t *group;
  int i;

  for (i = 0, group = self->ppd->groups;
       i < self->ppd->num_groups;
       i++, group++) {
    PyObject *args = Py_BuildValue ("()");
    PyObject *kwlist = Py_BuildValue ("{}");
    Group *grp = (Group *) PyType_GenericNew (&cups_GroupType,
					      args, kwlist);
    Py_DECREF (args);
    Py_DECREF (kwlist);
    grp->group = group;
    grp->ppd = self;
    Py_INCREF (self);
    PyList_Append (ret, (PyObject *) grp);
  }

  return ret;
}

PyGetSetDef PPD_getseters[] =
  {
    { "constraints",
      (getter) PPD_getConstraints, (setter) NULL,
      "List of constraints", NULL },

    { "optionGroups",
      (getter) PPD_getOptionGroups, (setter) NULL,
      "List of PPD option groups" },

    { NULL }, /* Sentinel */
  };

PyMethodDef PPD_methods[] =
  {
    { "markDefaults",
      (PyCFunction) PPD_markDefaults, METH_NOARGS,
      "Mark default options.  Returns number of conflicts." },

    { "markOption",
      (PyCFunction) PPD_markOption, METH_VARARGS,
      "Mark an option." },

    { "conflicts",
      (PyCFunction) PPD_conflicts, METH_NOARGS,
      "Returns number of conflicts." },

    { "writeFd",
      (PyCFunction) PPD_writeFd, METH_VARARGS,
      "writeFd (fd) -> None\n"
      "Write PPD file, with marked choices as defaults, to file\n"
      "descriptor." },

    { NULL } /* Sentinel */
  };

PyTypeObject cups_PPDType =
  {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "cups.PPD",                /*tp_name*/
    sizeof(PPD),               /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)PPD_dealloc,   /*tp_dealloc*/
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
    "PPD file",                /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    PPD_methods,               /* tp_methods */
    0,                         /* tp_members */
    PPD_getseters,             /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)PPD_init,        /* tp_init */
    0,                         /* tp_alloc */
    PPD_new,                   /* tp_new */
  };

////////////
// Option //
////////////

static PyObject *
Option_new (PyTypeObject *type, PyObject *args, PyObject *kwds)
{
  Option *self;
  self = (Option *) type->tp_alloc (type, 0);
  return (PyObject *) self;
}

static int
Option_init (Option *self, PyObject *args, PyObject *kwds)
{    
  self->option = NULL;
  return 0;
}

static void
Option_dealloc (Option *self)
{
  Py_XDECREF (self->ppd);
  self->ob_type->tp_free ((PyObject *) self);
}

////////////
// Option // ATTRIBUTES
////////////

static PyObject *
Option_getConflicted (Option *self, void *closure)
{
  if (!self->option || self->option->conflicted)
    Py_RETURN_TRUE;

  Py_RETURN_FALSE;
}

static PyObject *
Option_getKeyword (Option *self, void *closure)
{
  if (!self->option) {
    Py_INCREF (Py_None);
    return Py_None;
  }

  return PyString_FromString (self->option->keyword);
}

static PyObject *
Option_getDefchoice (Option *self, void *closure)
{
  if (!self->option) {
    Py_INCREF (Py_None);
    return Py_None;
  }

  return PyString_FromString (self->option->defchoice);
}

static PyObject *
Option_getText (Option *self, void *closure)
{
  if (!self->option) {
    Py_INCREF (Py_None);
    return Py_None;
  }

  return PyString_FromString (self->option->text);
}

static PyObject *
Option_getUI (Option *self, void *closure)
{
  if (!self->option) {
    Py_INCREF (Py_None);
    return Py_None;
  }

  return PyInt_FromLong (self->option->ui);
}

static PyObject *
Option_getChoices (Option *self, void *closure)
{
  PyObject *choices = PyList_New (0);
  ppd_choice_t *choice;
  int i;

  if (!self->option)
    return choices;

  for (i = 0, choice = self->option->choices;
       i < self->option->num_choices;
       i++, choice++) {
    PyObject *choice_dict = PyDict_New ();
    PyDict_SetItemString (choice_dict, "choice",
			  PyString_FromString (choice->choice));
    PyDict_SetItemString (choice_dict, "text",
			  PyString_FromString (choice->text));
    PyList_Append (choices, choice_dict);
  }

  return choices;
}

PyGetSetDef Option_getseters[] =
  {
    { "conflicted",
      (getter) Option_getConflicted, (setter) NULL,
      "conflicted status", NULL },
  
    { "keyword",
      (getter) Option_getKeyword, (setter) NULL,
      "keyword", NULL },
  
    { "defchoice",
      (getter) Option_getDefchoice, (setter) NULL,
      "defchoice", NULL },
  
    { "text",
      (getter) Option_getText, (setter) NULL,
      "text", NULL },
  
    { "ui",
      (getter) Option_getUI, (setter) NULL,
      "ui", NULL },
  
    { "choices",
      (getter) Option_getChoices, (setter) NULL,
      "choices", NULL },
  
    { NULL }
  };

PyMethodDef Option_methods[] =
  {
    { NULL } /* Sentinel */
  };

PyTypeObject cups_OptionType =
  {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "cups.Option",             /*tp_name*/
    sizeof(Option),            /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)Option_dealloc, /*tp_dealloc*/
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
    "PPD option",              /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    0,                         /* tp_methods */
    0,                         /* tp_members */
    Option_getseters,          /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)Option_init,     /* tp_init */
    0,                         /* tp_alloc */
    Option_new,                /* tp_new */
  };

///////////
// Group //
///////////

static PyObject *
Group_new (PyTypeObject *type, PyObject *args, PyObject *kwds)
{
  Group *self;
  self = (Group *) type->tp_alloc (type, 0);
  return (PyObject *) self;
}

static int
Group_init (Group *self, PyObject *args, PyObject *kwds)
{    
  self->group = NULL;
  return 0;
}

static void
Group_dealloc (Group *self)
{
  Py_XDECREF (self->ppd);
  self->ob_type->tp_free ((PyObject *) self);
}

///////////
// Group // ATTRIBUTES
///////////

static PyObject *
Group_getText (Group *self, void *closure)
{
  if (!self->group) {
    Py_INCREF (Py_None);
    return Py_None;
  }

  return PyString_FromString (self->group->text);
}

static PyObject *
Group_getName (Group *self, void *closure)
{
  if (!self->group) {
    Py_INCREF (Py_None);
    return Py_None;
  }

  return PyString_FromString (self->group->name);
}

static PyObject *
Group_getOptions (Group *self, void *closure)
{
  PyObject *options = PyList_New (0);
  ppd_option_t *option;
  int i;

  if (!self->group)
    return options;

  for (i = 0, option = self->group->options;
       i < self->group->num_options;
       i++, option++) {
    PyObject *args = Py_BuildValue ("()");
    PyObject *kwlist = Py_BuildValue ("{}");
    Option *opt = (Option *) PyType_GenericNew (&cups_OptionType,
						args, kwlist);
    Py_DECREF (args);
    Py_DECREF (kwlist);
    opt->option = option;
    opt->ppd = self->ppd;
    Py_INCREF (self->ppd);
    PyList_Append (options, (PyObject *) opt);
  }

  return options;
}

static PyObject *
Group_getSubgroups (Group *self, void *closure)
{
  PyObject *subgroups = PyList_New (0);
  ppd_group_t *subgroup;
  int i;

  if (!self->group)
    return subgroups;

  for (i = 0, subgroup = self->group->subgroups;
       i < self->group->num_subgroups;
       i++, subgroup++) {
    PyObject *args = Py_BuildValue ("()");
    PyObject *kwlist = Py_BuildValue ("{}");
    Group *grp = (Group *) PyType_GenericNew (&cups_GroupType,
					      args, kwlist);
    Py_DECREF (args);
    Py_DECREF (kwlist);
    grp->group = subgroup;
    grp->ppd = self->ppd;
    Py_INCREF (self->ppd);
    PyList_Append (subgroups, (PyObject *) grp);
  }

  return subgroups;
}

PyGetSetDef Group_getseters[] =
  {
    { "text",
      (getter) Group_getText, (setter) NULL,
      "text", NULL },
  
    { "name",
      (getter) Group_getName, (setter) NULL,
      "name", NULL },
  
    { "options",
      (getter) Group_getOptions, (setter) NULL,
      "options", NULL },
  
    { "subgroups",
      (getter) Group_getSubgroups, (setter) NULL,
      "subgroups", NULL },
  
    { NULL }
  };

PyMethodDef Group_methods[] =
  {
    { NULL } /* Sentinel */
  };

PyTypeObject cups_GroupType =
  {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "cups.Group",              /*tp_name*/
    sizeof(Group),             /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)Group_dealloc, /*tp_dealloc*/
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
    "PPD option group",        /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    0,                         /* tp_methods */
    0,                         /* tp_members */
    Group_getseters,           /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)Group_init,      /* tp_init */
    0,                         /* tp_alloc */
    Group_new,                 /* tp_new */
  };

////////////////
// Constraint //
////////////////

static PyObject *
Constraint_new (PyTypeObject *type, PyObject *args, PyObject *kwds)
{
  Constraint *self;
  self = (Constraint *) type->tp_alloc (type, 0);
  return (PyObject *) self;
}

static int
Constraint_init (Constraint *self, PyObject *args, PyObject *kwds)
{    
  self->constraint = NULL;
  return 0;
}

static void
Constraint_dealloc (Constraint *self)
{
  Py_XDECREF (self->ppd);
  self->ob_type->tp_free ((PyObject *) self);
}

////////////////
// Constraint // ATTRIBUTES
////////////////

static PyObject *
Constraint_getOption1 (Constraint *self, void *closure)
{
  if (!self->constraint) {
    Py_INCREF (Py_None);
    return Py_None;
  }

  return PyString_FromString (self->constraint->option1);
}

static PyObject *
Constraint_getChoice1 (Constraint *self, void *closure)
{
  if (!self->constraint) {
    Py_INCREF (Py_None);
    return Py_None;
  }

  return PyString_FromString (self->constraint->choice1);
}

static PyObject *
Constraint_getOption2 (Constraint *self, void *closure)
{
  if (!self->constraint) {
    Py_INCREF (Py_None);
    return Py_None;
  }

  return PyString_FromString (self->constraint->option2);
}

static PyObject *
Constraint_getChoice2 (Constraint *self, void *closure)
{
  if (!self->constraint) {
    Py_INCREF (Py_None);
    return Py_None;
  }

  return PyString_FromString (self->constraint->choice2);
}

PyGetSetDef Constraint_getseters[] =
  {
    { "option1",
      (getter) Constraint_getOption1, (setter) NULL,
      "option1", NULL },
  
    { "choice1",
      (getter) Constraint_getChoice1, (setter) NULL,
      "choice1", NULL },
  
    { "option2",
      (getter) Constraint_getOption2, (setter) NULL,
      "option2", NULL },
  
    { "choice2",
      (getter) Constraint_getChoice2, (setter) NULL,
      "choice2", NULL },
  
    { NULL }
  };

PyTypeObject cups_ConstraintType =
  {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "cups.Constraint",         /*tp_name*/
    sizeof(Constraint),        /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)Constraint_dealloc, /*tp_dealloc*/
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
    "PPD constraint",          /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    0,                         /* tp_methods */
    0,                         /* tp_members */
    Constraint_getseters,      /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)Constraint_init, /* tp_init */
    0,                         /* tp_alloc */
    Constraint_new,            /* tp_new */
  };
