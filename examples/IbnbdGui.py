#!/usr/bin/env python3

import json
import uuid
from sys import argv
import tkinter as tk
from tkinter import ttk
from tkinter import PhotoImage
import subprocess
import os

def makeIBNBDDict(key, aList):
    result = {}

    for i, anObject in enumerate(aList):

        if key == "exports" or key == "imports":
            aKey = anObject["mapping_path"]
        elif key == "incomming sessions" or key == "outgoing sessions":
            aKey = anObject["sessname"]
        elif key == "incomming paths" or key == "outgoing paths":
            aKey = anObject["pathname"]
        else:
            aKey = i

        result[aKey] = anObject

    return result
    
def JSONTree(Tree, Parent, ADictionary, TagList=[]):
    for key in ADictionary :
        uid = uuid.uuid4()
        if isinstance(ADictionary[key], dict):
            Tree.insert(Parent, 'end', uid, text=key)
            TagList.append(key)
            JSONTree(Tree, uid, ADictionary[key], TagList)
        elif isinstance(ADictionary[key], list):
            Tree.insert(Parent, 'end', uid, text=key)
            JSONTree(Tree,
                     uid,
                     makeIBNBDDict(key, ADictionary[key]))
        else:
            value = ADictionary[key]
            if isinstance(value, str):
                value = value.replace(' ', '_')
            elif value is None:
                value = "None"
            Tree.insert(Parent, 'end', uid, text=key, value=value)

def is_host(host):
    command = "ping -c 1 %s" % (host)
    proc = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=None,
                            shell=True)
    proc.communicate()[0].decode("utf-8").splitlines()
    ret = proc.wait()

    if ret != 0:
        return False
    else:
        return True

def get_ibnbd_dump(host):
    user = "root"
    tool = "/root/dkipnis/ibnbd-tool/ibnbd"
    command = "ssh %s@%s -C %s dump json all" % \
              (user, host, tool)
    proc = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=None,
                            shell=True)
    jsonstr = proc.communicate()[0].decode("utf-8")
    ret = proc.wait()

    if ret != 0 or not jsonstr:
        return ""

    return jsonstr

if __name__ == "__main__" :

    # load json file
    if len(argv) < 2:
        print("Please provide a json file or a hostname")
        exit(1)

    if not os.path.isfile(argv[1]) and is_host(argv[1]):
        jsonstr = get_ibnbd_dump(argv[1])
    if json:
        fileName = "%s.json" % (argv[1])
        jsonFile = open(fileName, "w")
        jsonFile.write(jsonstr)
        jsonFile.close()
    else:
        fileName = argv[1]

    jsonFile = open(fileName, "r")
    jsonDict = json.load(jsonFile)

    # Setup the root UI
    root = tk.Tk()
    root.title("IBNBD Viewer")
    root.tk.call('wm', 'iconphoto', root._w,
                 PhotoImage(file='ibnbd.png'))
    root.columnconfigure(0, weight=1)
    root.rowconfigure(0, weight=1)
    
    # Setup the Frames
    treeFrame = ttk.Frame(root, padding="3")
    treeFrame.pack(fill=tk.BOTH, expand=1)

    # Setup the Tree
    tree = ttk.Treeview(treeFrame, columns=('Values'))
    tree.column('Values', width=100, anchor='center')
    tree.heading('Values', text='Values')

    # Setup scrollability by a Scrollbar
    sb = ttk.Scrollbar(treeFrame,orient="vertical", command=tree.yview)
    tree.configure(yscrollcommand=sb.set)

    JSONTree(tree, '', jsonDict)
    tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=1)
    sb.pack(side=tk.RIGHT, fill=tk.Y)

    # Limit windows minimum dimensions
    root.update_idletasks()
    root.minsize(root.winfo_reqwidth(), root.winfo_reqheight())
    root.mainloop()
