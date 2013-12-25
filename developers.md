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



## Roadmap

- v1.2
    - support SSL, TLS

- v2.0
    - i18n (gettext)
    - branch and merge repositories


