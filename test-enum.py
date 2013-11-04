#!/usr/bin/python
import cups, os

def f(user_data, flags, dest):
    user_data.append (dest)
    print (flags, dest)
    return 1

l=[]
cups.enumDests(f, user_data=l)

for i in l:
    uri = i.options['printer-uri-supported']
    c, resource = cups.connectDest (i, lambda x, y, z: 1)
    name = resource.split ('/')[-1]
    print (name)
    n = c.getPPD(name)
    os.unlink (n)
