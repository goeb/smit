Smit
====

Small Issue Tracker

Smit is a free and open-source issue tracker designed to handle projects with speed and efficiency.

Smit is easy to install and customize and has a tiny footprint with lightning fast performance.

Smit will help you:

- organize issues
- assign work
- follow team activity
- capitalize knowledge 

Issues are to be understood in the broad sense:
- tasks
- ideas
- requests
- experiments 
- bugs


Features
--------
Some of the main features of Smit are:

- Easy customization of properties
- Advanced searching (including full text searching) and sorting
- Exporting contents to HTML, text, CSV, PDF (via the browser's capability)
- Multiple projects support
- Self-Contained: Smit is a single stand-alone executable


Getting Started
---------------
    
### Online demo:

    http://smit.herokuapp.com

    
### For the ones who want to experiment quickly:

    git clone https://github.com/goeb/smit.git
    cd smit
    make
    ./smit serve demo &

    <web-browser> http://127.0.0.1:8090/things_to_do/issues


### Install Smit:
    
    git clone https://github.com/goeb/smit.git
    cd smit
    make
    cp smit /usr/bin/.
    # OR    
    cp smit /somewhere/in/your/PATH


### Create a new project:
    
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

- v1.1
  - SSL, TLS
  - support Windows platform

- v2.0
  - i18n (gettext)
  - branch and merge repositories


Customize the HTML pages
------------------------

### How to customize the HTML pages?

Feel free to customize the HTM and CSS. These files are in $REPO/public:

    public/newIssue.html
    public/view.html
    public/project.html
    public/issues.html
    public/issue.html
    public/views.html
    public/projects.html
    public/signin.html
    public/style.css

### What are the SM_DIV_... variables ?
These are variables dynamically replaced on the HTML pages.

The following dynamic variables are defined:

    SM_DIV_NAVIGATION_GLOBAL
    SM_DIV_NAVIGATION_ISSUES
    SM_URL_PROJECT_NAME
    SM_HTML_PROJECT_NAME
    SM_RAW_ISSUE_ID
    SM_SCRIPT_PROJECT_CONFIG_UPDATE
    SM_DIV_PREDEFINED_VIEWS
    SM_DIV_PROJECTS
    SM_DIV_ISSUES
    SM_DIV_ISSUE_SUMMARY
    SM_DIV_ISSUE
    SM_DIV_ISSUE_FORM

Some variables make sense only in some particular context. For instance,
SM_RAW_ISSUE_ID makes sense only when a single issue is displayed.

### How to set a logo?

* Copy your logo image in $REPO/public (eg: logo.png)
* Refer this image in the HTML pages

    &lt;img src="/public/logo.png" alt="logo"&gt;

### Why cannot I create a project named 'public'?

'public' is reserved for holding HTML pages that are public, such as the signin page.

The reserve keywords that cannot be used as project names are:
    
    public
    users


### How to set up different pages for two projects?

HTML pages are first looked after in $REPO/&lt;project&gt;/html/. and, if not present, Smit looks in the $REPO/public directory.

So, for example, if you want to customize the 'issues' page for a project:

- copy $REPO/public/issues.html to $REPO/&lt;project&gt;/html/issues.html
- edit $REPO/&lt;project&gt;/html/issues.html to suit your needs
- restart Smit




REST API
--------

The requests are driven by the Query String:

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

- full=1

    This makes sense only for the 'issues' pages, and makes the page display all issues and their contents on the page.


