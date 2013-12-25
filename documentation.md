# Documentation

## Install

### Windows Platform

- Download [smit-win32-1.1.0.zip](https://github.com/goeb/smit/blob/master/downloads/smit-win32-1.1.0.zip?raw=true)
- Unzip `smit-win32-1.1.0.zip`
- `smit.exe` is ready to use

### Linux

Requirements:

- libcrypto (provided by OpenSSL for instance)

Installation instructions:

    git clone http://github.com/goeb/smit
    cd smit
    make

And copy the compiled executable `smit` to somewhere in your PATH (for example $HOME/bin).

## Directories Layout

Each instance of Smit serve one repository (also refered below as `$REPO`).

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

Note: do not modify the javascript files `project.js`, `smit.js`, `user.js`, `view.js`, as they work together with the `smit` executable.


## Customize the logo

Modifiy the `$REPO/public/logo.png` according to yours needs.
    
## The SM variables

In order to let a maximum customization freedom, Smit let the user define the global structure of the HTML pages, and inserts the dynamic contents at users's defined places, indicated by SM variables:

    SM_DIV_NAVIGATION_GLOBAL
    SM_DIV_NAVIGATION_ISSUES
    SM_URL_PROJECT_NAME
    SM_HTML_PROJECT_NAME
    SM_RAW_ISSUE_ID
    SM_SCRIPT_PROJECT_CONFIG_UPDATE
    SM_DIV_PREDEFINED_VIEWS
    SM_DIV_PROJECTS
    SM_DIV_USERS
    SM_DIV_ISSUES
    SM_DIV_ISSUE_SUMMARY
    SM_DIV_ISSUE
    SM_DIV_ISSUE_FORM
 
Some variables make sense only in some particular context. For instance,
SM_RAW_ISSUE_ID makes sense only when a single issue is displayed.


## FAQ

### Why cannot I create a project named 'public'?

`public` is reserved for storing HTML pages that are public, such as the signin page.

The reserve keywords that cannot be used as project names are:

    public
    users

### How to set up different pages for two projects?

HTML pages are first looked after in `$REPO/<project>/html/.` and, if not present, Smit looks in the `$REPO/public` directory.

So, for example, if you want to customize the 'issues' page for a project:

- copy `$REPO/public/issues.html` to `$REPO/<project>/html/issues.html`
- edit `$REPO/<project>/html/issues.html` to suit your needs
- restart Smit


