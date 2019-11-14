#!/usr/bin/env python3

import json
import uuid
from sys import argv
import tkinter as tk
from tkinter import ttk
from tkinter import PhotoImage

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

if __name__ == "__main__" :

    # load json file
    if len(argv) < 1:
        print("Please provide a json file")
        exit(1)
        
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
