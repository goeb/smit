# Administration

## Initiate a Repository

```
REPO=/path/to/some/dir
mkdir $REPO
smit init $REPO
```

## Create a Project

```
cd $REPO
smit project -c <project-name>
smit user <user-name> --passwd <pass>
smit user <user-name> --project <project-name> admin
```

## Start a smit web server

```
smit serve
```


## Command Line Usage

```
Usage: smit <command> [<args>]

The smit commands are:

  init        Initialise a smit repository
  project     List, create, or update a smit project
  user        List, create, or update a smit user
  serve       Start a smit web server
  version     Print the version
  help

See 'smit help <command>' for more information on a specific command.
```

### init

```
Usage: smit init [<directory>]

  Initialize a repository, where the smit projects are to be stored.

  If the directory exists, it must be empty.
  If the directory does not exist, it is created.
  If the directory is not given, . is used by default.
```

### project

```
Usage: project [<project-name>] [options]

  List, create, or update a smit project.

Options:
  -c         Create a project, with a default structure. The structure
             may be modified online by an admin user.
  -d <repo>  select a repository by its path (by default . is used)
```

### user

```
Usage: 1. smit user
       2. smit user <name> [options] [global-options]

  1. List all users and their configuration.
  2. With no option, print the configuration of a user.
     With options, create or update a user.

Options:
  --passwd <pw>     set the password
  --no-passwd       delete the password (leading to impossible login)
  --project <project-name> <role>
                    set a role (ref, ro, rw, admin) on a project
  --superadmin      set the superadmin priviledge (ability to create
                    projects and manage users via the web interface)
  --no-superadmin   remove the superadmin priviledge
  -d <repo>         select a repository by its path (by default . is used)

Roles:
    admin       able to modify an existing project
    rw          able to create and modify issues
    ro          able to read issues
    ref         may not access the project, but may be referenced
```


## Project Configuration

At the moment some configuration parameters are not driven by the web interface.
Thus this paragraph explains how to set them.

The project configuration defines:

- the properties of the issues
- the tags
- the numbering scheme of the issues (local or global to several projects)

The configuration may be modified in two ways:

- via the web interface
    - only the properties may be managed this way
    - hot reload 
- via editing directly the configuration file, and performing a hot reload via the web interface

The configuration of a project is given by the file `project` in the directory of the project. 

### addProperty
```
addProperty <id> [-label <label>] [-help <help>] <type> [values ...]
```

`addProperty` defines a property.

- `<id>` is an identifier (only characters a-z, A-Z, 0-9, -, _)
- `<label>` is the text that will be displayed in the HTML pages (optional)
- `<help>` is a tooltip for properties inputs, displayed above properties labels (optional)
- `<type>` is one of text, select, multiselect, selectUser, textarea, textarea2

    * `text`: free text
    * `select`: selection among a list if given values
    * `multiselect`: same as select, but several may be selected at the same time
    * `selectUser`: selection among the users of the project
    * `textarea`: free text, multi-lines
    * `textarea2`: same as textarea, but spanned on 2 columns in the HTML
    
- `value` indicates the allowed values for types select and multiselect.

### setPropertyLabel

```
setPropertyLabel <propety-id> <label>
```

`setPropertyLabel` defines the label for a property. This is used for mandatory properties that are not defined by `addProperty`: id, ctime, mtime, summary.

### numberIssues
(smit version >= 1.4, no web interface)

```
numberIssues global
```

`numberIssues` defines the numbering policy of the issues.

If not defined, the issues are numbered reletively to their project: 1, 2, 3,...

If `global` is set, then the numbering is shared by all the projects that have this policy.


### tag
(smit version >= 1.3, no web interface)

Entries may be tagged.

```
tag <id> -label <text> [-display]
```

- `<id>` may contain only letters, digits and underscores.
- `<text>` is the text to display in the HTML page
- `-display` requires the display of a box in the headers of issues, that indicates if at least one entry of the current issue is tagged (this may be seen as a checkbox, to quickly verify that some quality criterion is met).


### Full example 

```
setPropertyLabel id "The ID"

addProperty status -label "The Status" select open closed
addProperty owner -label "The owner" selectUser

numberIssues global

tag test -label "Test Proof" -display

trigger public/sendEmail.sh
```

## Set up a trigger

A trigger defines an external program to be launched after each new entry. It is typically useful for sending email notifications when some condition occur.

Triggers are not supported on Windows.

The file `trigger` in a smit project defines the path to the external program, on the first line.

Example:

```
$ cat $REPO/project-X1/trigger
notifyNewEntry.py
```


Notes:

- if the path is relative, it is considered relatively to the smit repository
- the external program must be executable

On creation or modification of an issue, the trigger will be called, and passed a JSON structure on its standard input, like this example:

```
{
"project":"myproject",
"issue":"13",
"entry":"ed3eda2976914998cf2fcd759adf71753d0aa5f8",
"author":"fred",
"users":{
  "fred":"admin",
  "not assigned":"ref",
  "xxxt":"rw"},
"modified":["a-b-c","multi-a","new-ttt","owner","test-reload","textarea2","xx"],
"properties":{
  "a-b-c":["a-b-xx66",""],
  "multi-a":["%multi-h","42"],
  "new-ttt":["new-ttt",""],
  "owner":["owner'44r%","fred"],
  "summary":["summary","fatal error x8"],
  "target_version":["target_version\"22","v0.1"],
  "test-reload":["test-reloadx",""],
  "textarea2":["textarea2",""],
  "xx":["xx(yy)__99",""]
},
"message":"..."
}
```

Example of trigger program is given: [notifyNewEntry.py](../downloads/notifyNewEntry.py)


## Customize the HTML pages

One may customize the HTML and CSS pages. Some knowledge of HTML and CSS are needed.
Each instance of Smit serves one repository (also refered below as `$REPO`).

The Directories layout is typically:

    $REPO
    ├── project-A
    │   └── html
    │
    ├── project-B
    │   └── html
    │
    └── public


Smit grants read access to files under the `$REPO/public`, without restriction. Do not put confidential data in here.

By default the core HTML files are in `$REPO/public` and shared by all the projects of the repository: 

    signin.html
    issue.html
    issues.html
    newIssue.html
    project.html
    projects.html
    user.html
    view.html
    views.html

After a page modification, there is no need to restart Smit. Just reload the page.

### Customize the logo

Modify the `$REPO/public/logo.png` according to yours needs, or modify the HTML files to point to another logo.

### Customize the CSS

Modify `$REPO/public/style.css` and `print.css` according to your needs.

    
### The SM variables

In order to let a maximum customization freedom, Smit lets the user define the global structure of the HTML pages, and inserts the dynamic contents at users' defined places, indicated by SM variables:

    SM_DIV_ISSUE
    SM_DIV_ISSUE_FORM
    SM_DIV_ISSUE_MSG_PREVIEW
    SM_DIV_ISSUES
    SM_DIV_ISSUE_SUMMARY
    SM_DIV_NAVIGATION_GLOBAL
    SM_DIV_NAVIGATION_ISSUES
    SM_DIV_PREDEFINED_VIEWS
    SM_DIV_PROJECTS
    SM_DIV_USERS
    SM_HTML_PROJECT_NAME
    SM_RAW_ISSUE_ID
    SM_SCRIPT_PROJECT_CONFIG_UPDATE
    SM_URL_PROJECT_NAME
 
Some variables make sense only in some particular context. For instance,
`SM_RAW_ISSUE_ID` makes sense only when a single issue is displayed.

### Setting different HTML pages for 2 projects in the same repository.

The core HTML pages are first looked after in `$REPO/<project>/html/.` and, if not present, Smit looks in the `$REPO/public` directory.

Therefore, if you want to customize (for example) the 'issues' page for a project:

- copy `$REPO/public/issues.html` to `$REPO/<project>/html/issues.html`
- edit `$REPO/<project>/html/issues.html` to suit your needs

### Interface constraints

Be sure to not modify the following topics in the HTML pages, as they insure proper operation of Smit:

- keep the inclusion of `/sm/smit.js`. Smit uses this to update some dynamic contents on some pages (this file is included in the Smit executable).
- keep the name of the `SM_` variables 
- keep the `id="sm_..."`
- keep the `class="sm_..."`

## FAQ

### Why cannot I create a project named 'public'?

`public` is reserved for storing HTML pages that are public, such as the signin page.

The reserve keywords that cannot be used as project names are:

    public
    sm
    users

