# Documentation

## Install

### Windows Platform

- Download [smit-win32-1.1.1.zip](downloads/smit-win32-1.1.1.zip)
- Unzip `smit-win32-1.1.1.zip`
- `smit.exe` is ready to use

### Linux

Requirements:

- libcrypto (provided by OpenSSL for instance)

Installation instructions:

    git clone http://github.com/goeb/smit
    cd smit
    make

And copy the compiled executable `smit` to somewhere in your PATH (for example $HOME/bin).

## Usage

    Usage: smit [--version] [--help]
                <command> [<args>]

    Commands:

        init [<directory>]
            Initialize a repository in an existing empty directory. A repository
            is a directory where the projects are stored.

        addproject <project-name> [-d <repository>]
            Add a new project, with a default structure. The structure
            may be modified online by an admin user.
    
        adduser <user-name> [--passwd <password>] [--project <project-name> <role>]
                            [--superadmin] [-d <repository>]
            Add a user on one or several projects.
            The role must be one of: admin, rw, ro, ref.
            --project options are cumulative with previously defined projects roles
            for the same user.
    
        serve [<repository>] [--listen-port <port>] [--ssl-cert <certificate>]
            Default listening port is 8090.
            The --ssl-cert option forces use of HTTPS.
            <certificate> must be a PEM certificate, including public and private key.
    
        --version
        --help
    
    When a repository is not specified, the current working directory is assumed.
    
    Roles:
        superadmin  able to create projects and manage users
        admin       able to modify an existing project
        rw          able to add and modify issues
        ro          able to read issues
        ref         may not access a project, but may be referenced

## Project Configuration

This configuration can be managed through the web interface, but a few options cannot be managed this way at the moment (see below).

The configuration of a project is given by the file `project` in the directory of the project.

### addProperty
```
addProperty <id> [-label <label>] <type> [values ...]
```

`addProperty` defines a property.

- `<id>` is an identifier (only characters a-z, A-Z, 0-9, -, _)
- `<label>` is the text that will be displayed in the HTML pages (optional)
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
```

## Directories Layout

Each instance of Smit serves one repository (also refered below as `$REPO`).

The Directories layout is typically:

    $REPO
    ├── project-A
    │   └── html
    │
    ├── project-B
    │   └── html
    │
    ├── project-C
    │   └── html
    │
    └── public
    


## Create repo and project

    mkdir myrepo
    smit init myrepo
    smit addproject -d myrepo myproject1
    smit adduser homer -d myrepo --superadmin --passwd homer --project myproject1 admin
    smit serve myrepo --listen-port 9090

And with your browser, go to: [http://localhost:9090](http://localhost:9090)

Note that on Windows the scripts `bin/init.bat` and `start.bat` help perform these steps, but you still may want to customize the repository name and admin user name.




## Customize the HTML pages

Feel free to customize the HTML and CSS. These files are in `$REPO/public`:

    signin.html
    issue.html
    issues.html
    project.html
    user.html
    view.html
    views.html
    newIssue.html
    projects.html
    style.css


## Customize the logo

Modifiy the `$REPO/public/logo.png` according to yours needs.
    
## The SM variables

In order to let a maximum customization freedom, Smit let the user define the global structure of the HTML pages, and inserts the dynamic contents at users's defined places, indicated by SM variables:

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



## FAQ

### Why cannot I create a project named 'public'?

`public` is reserved for storing HTML pages that are public, such as the signin page.

The reserve keywords that cannot be used as project names are:

    public
    sm
    users

### How to set up different pages for two projects?

HTML pages are first looked after in `$REPO/<project>/html/.` and, if not present, Smit looks in the `$REPO/public` directory.

So, for example, if you want to customize the 'issues' page for a project:

- copy `$REPO/public/issues.html` to `$REPO/<project>/html/issues.html`
- edit `$REPO/<project>/html/issues.html` to suit your needs


