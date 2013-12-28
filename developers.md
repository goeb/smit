# Developers' Corner


## REST API
The requests are driven by the Query String:

Examples:

- `colspec=status+release+assignee`

    The properties specified by colspec shall be displayed in the table.

- `sort=aaa-bbb+ccc`

    The specified properties shall drive the sorting order (`+` for ascending order, and `-` for descending order).

- `search=hello world`

    Full text search on all properties and messages.

- `filterin=status:open&filterin=assignee:John Smith&filterout=release:undefined`

    'filterin' and 'filterout' may be specified several times on the query string.

- `format= text | html | csv`

    The default format is HTML.

- `full=1`

    This makes sense only for the `issues` pages, and makes the page display all issues and their contents on the page.


## Directories and files

The database is made of directories and files.
The layout is as follows:

    $REPO
     ├── users
     │
     ├── project-A
     │   ├── files   : uploaded files
     │   ├── issues  : issues
     │   ├── html    : cutomized HTML pages
     │   ├── project : plain text project description
     │   ├── tmp     : temporary dir for uploaded files
     │   └── views   : plain text description of predefined views
     │
     ├── project-B
     │
     ├── project-C
     │
     └── public
         ├── javascript files
         ├── css files
         ├── HTML files
         └── logo
 


The issues are organized as follow:

    issues
    ├── 1
    │   ├── lEf-tTqa7wWoX-sJVnu-1U_4ADI
    │   ├── 4M6CY0RqvL4L7ZrDe1r0FQY-tE8
    │   ...
    │   ├── lEf-tTqa7wWoX-sJVnu-1U_4ADI
    │   └── _del : directory where deleted entries are moved
    │
    ├── 2
    ├── 3
    ...


An issue is made up of successive immutable entries.

The first entry is the one that has a null parent. The last entry is the one that is referenced as the parent of no other entry.

### Sample entry of an issue

Entries are text files with straightforward key-value syntax.

    +parent null
    +author Fred
    +ctime 1379878590
    +message < -----------endofmsg---
    This page will allow the user to select the search parameters.
    The output will use the parameters of the querystring: 
    search, filterin, filterout, sort
    -----------endofmsg---
    status open
    target_version v1.0
    summary "HTML page (form) for advanced search"

Keys with a `+` sign are specific to the entry and are not taken for the consolidation of the issue properties.

### Sample `project` file

This file describes the structure of the properties.

    setPropertyLabel id "#"
    setPropertyLabel ctime Created
    setPropertyLabel mtime Modified
    setPropertyLabel summary Description
    addProperty status -label "The status" select open closed deleted
    addProperty target_version select v1.1 v1.2 v2.0 other
    addProperty owner selectUser

### Sample `users` file

This file describes the users and their priviledges.

    addUser "John Smith" sha1 e61a3587b3f7a142b8c7b9263c82f8119398ecb7 \
        project things_to_do rw 

    addUser alice sha1 522b276a356bdf39013dfabea2cd43e141ecc9e8 \
        project things_to_do ro 


## Roadmap

- v1.2
    - support of SSL, TLS

- v2.0
    - i18n (gettext)
    - branch and merge repositories


