Smit
====

Small Issue Tracker

Smit is a generalist issue tracker, suitable for customer support, team issues, bug tracking, etc.


Features
--------

- Full text searching
- Easy customization of properties
- Simple, fast and light
- Manage several projects
- Easy to install locally, or on a server
- UTF-8
- Offline capability


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


In order to create a new project:
    
    mkdir myrepo
    smit init myrepo
    smit addproject -d myrepo myproject1  
    smit adduser homer -d myrepo --passwd homer --project myproject1 admin
    smit serve myrepo --listen-port 9090

And with your browser, go to:

    http://localhost:9090

    

Roadmap
---

- v1.0
  - command-line interface
  - web interface

- v1.1
  - make it also run on Windows
  - full-contents view (all tickets with their contents on a single page)

- v2.0
  - i18n (gettext)
  - branch and merge repositories


Customize the HTML pages
------------------------

Soon coming...


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

