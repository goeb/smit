# Customize HTML

## Introduction

This page deals with the look and feel of the web interface, and how the HTML pages may be customized.

To customize the HTML look and feel, the administrator needs:

- some knowledge about HTML and CSS
- write access to the files of the smit repository


## Overview of the Web Pages

The HTML pages, Javascript and CSS served by Smit are of 3 types:

- dynamic page
- static file
- static embedded file

### Dynamic Page

Smit serves 9 types of dynamic pages, and each of these has a HTML template:

- Signin: `signin.html`
- Create an issue: `newIssue.html`
- View an issue: `issue.html`
- List issues: `issues.html`
- Configuration of a project: `project.html`
- List projects: `projects.html`
- Profile of a user: `user.html`
- Configuration of a view: `view.html`
- List of predefined views: `views.html`

*Example:*

```
/
/<project>/issues/
/<project>/issues/234
```

### Static Embedded File

Static embedded files are embedded in the smit executable. They are served as is, and cannot be customized by the administrator.

They are located in `/sm/` (in the URL).

*Example:*

```
/sm/version
/sm/smit.js
```

### Static File

Static files are served as is. They may be anywhere in the repository, however see file access below.

These are typically the logos, uploaded files,...

*Example:*

```
/public/log.png
/public/style.css
```

## File Access

Read access to files in folder `$REPO/public` is granted to everybody. Do not put confidential data here.

Read access to files in a project folder is granted to the users of the project that have read access to the project.

Other files cannot be read via the web interface.

## CSS Customization

*Files:*

```
public/style.css
public/print.css
```

These files define styles that are used in the dynamic pages.
You may customize them, provided that you keep the same names of the styles.

## HTML Customization

By default the templates for the dynamic HTML pages are the same for all projects. It is possible to have templates dedicated to a project (see below).

When modifying a template, be sure to keep the following items, as Smit needs them:

- inclusion of `/sm/smit.js`: Smit uses this to update dynamic contents
- the SM variables (see description below)
- the `id="sm_..."`
- the `class="sm_..."`

*Example of customizing the logo*

Modify the `$REPO/public/logo.png`, or modify the HTML templates to point to another image.

    
## SM variables

In order to let a maximum customization freedom, Smit lets the user define the global structure of the HTML pages, and inserts the dynamic contents at users' defined places, indicated by SM variables:

`SM_DIV_NAVIGATION_GLOBAL`

Insert a navigation bar, that gives links to project list, project configuration (when relevant), predefined views (when relevant), signing-out and user's profile.


### Inside a project

`SM_DIV_NAVIGATION_ISSUES`

Insert a navigation bar for browsing through the issues of a project.

```
SM_HTML_PROJECT_NAME
SM_URL_PROJECT_NAME
```

Insert the name of the project, either for printing on the screen, or for an hyperlink.

### Page Issue

```
SM_RAW_ISSUE_ID
SM_HTML_ISSUE_SUMMARY
SM_DIV_ISSUE
SM_DIV_ISSUE_MSG_PREVIEW
SM_DIV_ISSUE_FORM
```

### Other SM variables

    SM_DIV_ISSUES
    SM_DIV_PREDEFINED_VIEWS
    SM_DIV_PROJECTS
    SM_DIV_USERS
    SM_SCRIPT_PROJECT_CONFIG_UPDATE
 

## Project with dedicated HTML pages

The dynamic HTML templates are first looked after in `$REPO/<project>/html/` and, if not present, Smit looks in the `$REPO/public` directory.

Therefore, if you want to customize - for example - the 'issues' page for a project:

- copy `$REPO/public/issues.html` to `$REPO/<project>/html/issues.html`
- modify `$REPO/<project>/html/issues.html`


