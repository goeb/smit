smit
====

Small Issue Tracker


Features
--------

- Generalist issue tracker, for all needs (customer support, team issues, etc.)
- Ability to serve several unrelated projects, with different structures
- Easy to install locally or on a server
- Easy to customize (no programming skills, only editing text, CSS, HTML)
- Fast and light
- Simple database on disk, with text files
- Do only issue tracking (no wiki, ...)
- Short issue identifiers in base 34 (using letters - eg: 3EF, RGJ, SD5)


REST API
--------

Query string
    
    colspec=status+release+assignee
    sort=a-b+c
    search=hello world
    filter=status:open,release!v1.0,assignee:John Smith
        possible conflict if value of property contains the separator (,)
    format=text | html



Technical constraints
---------------------
Project name must not contain these characters: /
Charset encoding UTF-8
