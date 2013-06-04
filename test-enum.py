#!/usr/bin/python
import cups, os

def f(user_data, flags, dest):
    user_data.append (dest)
    print flags, dest
    return 1

l=[]
cups.enumDests(f, user_data=l)

for i in l:
    uri = i.options['printer-uri-supported']
    print uri
    c = cups.connectDest (i, lambda x, y, z: 1)
    n = c.getPPD(i.name)
    os.unlink (n)
