Smit
====

Small Issue Tracker

Smit is a generalist issue tracker, suitable for customer support, team issues, bug tracking, etc.


Features
--------

- Full text searching
- Easy configuration of properties
- Manage several projects
- Easy to install locally, or on a server
- Fast and light
- UTF-8
- Offline issue tracking


Getting Started
---------------
    
Online demo:

    http://smit.herokuapp.com

    
For the ones who want to experiment quickly:

    git clone https://github.com/goeb/smit.git
    cd smit
    make
    ./smit demo &

    <web-browser> http://127.0.0.1:8080/things_to_do/issues



Roadmap
---

v1.0
    command-line interface
    web interface

v1.1
    file upload
    make it also run on Windows

v2.0
    i18n (gettext)
    branch and merge repositories


Command-Line
---
This is a preview of what the command-line interface will look like:

    # initialize a repository
    mkdir repo && cd repo
    smit init .

    # add itialize a project
    smit project create MySampleProject

    # add a user
    smit user add "John Smith" MySampleProject rw
    Enter password:
    
    smit serve .




REST API
--------

Query string

Examples:
    
- colspec=status+release+assignee

    The properties specified by colspec shall be displayed in the table.

- sort=aaa-bbb+ccc

    The specified properties shall drive the sorting order (+ for ascending order, and - for descending order).

- search=hello world

    Full text search on all properties and messages.

- filterin=status:open&filterin=assignee:John Smith&filterout=release:undefined

    'filterin' and 'filterout' may be specified several times on the query string.

- format=text | html

    text PARTIALLY IMPLEMENTED


Repository structure
--------------------

A typical file tree is:

    SMIT_REPO
    ├── project A
    │   ├── entries
    │   │   ├── 4eg
    │   │   ├── drl6
    │   │   └── etc. (other entries)
    │   ├── html
    │   │   ├── footer.html
    │   │   └── header.html
    │   ├── project
    │   └── views
    ├── project B
    ├── etc. (other projects)
    ├── public
    │   ├── signin.html
    │   ├── smit.js
    │   ├── logo.png
    │   ├── style.css
    │   └── etc.
    └── users


HTML pages are built as follows:

    To Be Completed

