# Documentation

## Create a new repository and a new project

    mkdir myrepo
    smit init myrepo
    smit addproject -d myrepo myproject1
    smit adduser homer -d myrepo --passwd homer --project myproject1 admin
    smit serve myrepo --listen-port 9090

And with your browser, go to: [http://localhost:9090](http://localhost:9090)

## Customize the HTML pages

Feel free to customize the HTM and CSS. These files are in `$REPO/public`:

    public/newIssue.html
    public/view.html
    public/project.html
    public/issues.html
    public/issue.html
    public/views.html
    public/projects.html
    public/signin.html
    public/style.css


    
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

### How to set a logo?

* Copy your logo image in `$REPO/public` (eg: `logo.png`)
* Refer this image in the HTML pages

    &lt;img src="/public/logo.png" alt="logo"&gt;


### Why cannot I create a project named 'public'?

'public' is reserved for holding HTML pages that are public, such as the signin page.

The reserve keywords that cannot be used as project names are:

    public
    users

### How to set up different pages for two projects?

HTML pages are first looked after in `$REPO/<project>/html/.` and, if not present, Smit looks in the `$REPO/public` directory.

So, for example, if you want to customize the 'issues' page for a project:

- copy `$REPO/public/issues.html` to `$REPO/<project>/html/issues.html`
- edit `$REPO/<project>/html/issues.html` to suit your needs
- restart Smit


