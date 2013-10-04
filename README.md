smit
====

Small Issue Tracker


Features
--------

- Generalist issue tracker, for all needs (customer support, team issues, etc.)
- Full text searching
- Ability to serve several unrelated projects, with different structures
- Easy to install locally or on a server
- Easy to customize (no programming skills, only editing text, CSS, HTML, and in the future a web tools will help for this)
- Fast and light
- Simple database on disk, with text files
- Short issue identifiers in base 34 (using letters - eg: 3EF, RGJ, SD5)


Limitations
-----------
- no wiki, no revision control
- suitable for small projects (less than 10000 issues per project)

Getting Started
---------------
    
For the ones who want to experiment quickly:

    git clone https://github.com/goeb/smit.git
    cd smit
    make
    ./smit repositories &

    <web-browser> http://127.0.0.1:8080/myproject/issues


In the future (that is "not yet"):

    # initialize a repository
    mkdir smitRepo && cd smitRepo
    smit init

    # add itialize a project
    smit add-project MySampleProject

    # add a user
    smit add-user "John Smith" MySampleProject:rw
    Enter password:
    
    smit serve .



Why should I prefer Smit over Redmine, Bugzilla, RequestTracker, etc. ?
---

Smit is far simpler to start:

    - get a copy of the smit program
    - create a new repository and project (or copy from an archive)
    - start smit

Smit is far simpler to configure:

    1. modify properties in the text file 'project'
    2. restart smit
    3. experiment, and start again at step 1 if you feel like

Smit can be used offline (read-only):

    - get a local copy of the smit program
    - get a local copy of the repository
    - start smit on your machine


REST API
--------

Query string
    
- colspec=status+release+assignee

    The properties specified by colspec shall be displayed in the table.

- sort=aaa-bbb+ccc

    The specified properties shall drive the sorting order (+ for ascending order, and - for descending order).

- search=hello world

    Full text search on all properties and messages.

- filterin=status:open
  filterin=assignee:John Smith
  filterout=release:undefined

    filterin and filterout may be specified several times on the query string.

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
    │   └── project
    ├── project B
    ├── etc. (other projects)
    ├── public
    │   ├── signin.html
    │   ├── smit.js
    │   ├── logo.png
    │   └── style.css
    └── users


HTML pages are built as follows:

    a header
    the dynamic contents (list of issues or contents of an issue)
    a footer
    some javascript that updates some values in the page


Headers:

    <template>_global_header.html
    <template>_global_footer.html
    <template>_project_header.html
    <template>_project_footer.html
    <template>_style.css



Technical constraints
---------------------
Project name must not contain these characters: /

Charset encoding UTF-8
