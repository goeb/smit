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

    TODO 
    possible conflict if value of property contains the separator (,)

- format=text | html

    text PARTIALLY IMPLEMENTED



Technical constraints
---------------------
Project name must not contain these characters: /

Charset encoding UTF-8
