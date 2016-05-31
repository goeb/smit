# Customize HTML

## Introduction

This page deals with the look and feel of the web interface, and how the HTML pages may be customized.

To customize the HTML look and feel, the administrator needs:

- some knowledge about HTML and CSS
- write access to the files of the Smit repository

*Note:*

This page documents Smit version >= 3.3.

## Overview of the Web Pages

The files by Smit are of 3 types:

- dynamic page (eg: list of issues)
- static file (eg: image of a logo, style.css)
- static embedded file (eg: smit.js)

### Dynamic Page

Smit serves dynamic contents through templates:

Page                       Template                Example URL
------                     ----------              -------------
Signin                     `signin.html`           
List of projects           `projects.html`         /
Create an issue            `newIssue.html`         /proj/issues/new
View an issue              `issue.html`            /proj/issues/1234
List of issue              `issues.html`           /proj/issues/
Configuration of a project `project.html`          /proj/config
Configuration of a view    `view.html`             /proj/views/Open issues
List of predefined views   `views.html`            /proj/views/
List of entries            `entries.html`          /proj/entries/
Statistics                 `stat.html`             /proj/stat
Issues accross projects    `issuesAccross.html`    /*/issues/
Profile of a user          `user.html`             /users/John Smith
List of users              `users.html`            /users/

Templates are located in either directories:

- `$REPO/.smit/templates/`
- `$REPO/$PROJECT/.smip/templates/`

### Static File

Static files are served as is. They may be anywhere in the repository, however see file access below.

These are typically the logos, uploaded files,...

*Example:*

```
/public/logo.png
/public/style.css
```

Note that uploaded files (those attached to issues) are available through the `files` virtual directory.  Example:

`/proj/files/2a0dfb888becb7ca697c3470c41a86cf6c69c3bf/screenshot.png`

### Static Embedded File

Static embedded files are embedded in the `smit` executable. They are served as is, and cannot be customized by the administrator.

There URL start by `/sm/`.

*Example:*

```
/sm/version
/sm/smit.js
```

## File Access

Read access to files in folder `$REPO/public` is granted to everybody. Do not put confidential data here.

Read access to files in a project folder is granted to the users of the project that have read access to the project.

Other files cannot be read via the web interface.

## CSS Customization

*Files:*

```
/public/style.css
/public/print.css
```

These files define styles that are used in the dynamic pages.
You may customize them, provided that you keep the same names of the styles.

## HTML Templates Customization

By default the templates of the dynamic HTML pages are the same for all projects. It is possible to have templates dedicated to a project (see below).

When modifying a template, be sure to keep the following items, as Smit needs them:

- inclusion of `/sm/smit.js`: used by Smit to update dynamic contents
- the SM variables (see description below)
- the `id="sm_..."`
- the `class="sm_..."`

*Example of customizing the logo:*

Modify the `$REPO/public/logo.png`, or modify the HTML templates to point to another image.

    
## SM variables

In order to give a maximum customization freedom, Smit lets the administrator define the global structure of the HTML pages, and inserts the dynamic contents at specific locations within the pages, indicated by SM variables.

Example of a template text:

```
<span>Logged in as: SM_HTML_USER</span>
```

Example of the resulting generated HTML:

```
<span>Logged in as: John Smith</span>
```



### Basic SM variables

SM variable                Description                                     Example
-------------              -------------                                   ---------
SM_URL_ROOT                Root URL                                        /tracker
SM_HTML_PROJECT            Name of current project (HTML display)          My Project
SM_URL_PROJECT             URL to current project (includes SM_URL_ROOT)   /tracker/My Project
SM_URL_USER                Name of the signed-in user (URL format)         John%20Smith
SM_HTML_USER               Name of the signed-in user (HTML display)       John Smith
SM_RAW_ISSUE_ID            Id of the current issue                         421
SM_HTML_ISSUE_SUMMARY      Summary of the current issue                    
SM_DATALIST_PROJECTS       `<datalist>` of projects names                    


### Whole blocks of dynamic contents in the scope of a project

SM variable                Description
-------------              -------------
SM_SPAN_VIEWS_MENU         Menu of the views
SM_DIV_PREDEFINED_VIEWS    List of the views
SM_DIV_PROJECTS            List of the projects
SM_DIV_ISSUES              List of issues
SM_DIV_ISSUE               Contents of an issue
SM_DIV_ISSUE_FORM          Form for editing an issue
SM_DIV_ISSUE_MSG_PREVIEW   Message preview
SM_DIV_ENTRIES             List of entries


### Whole blocks of dynamic contents not related to any specific project

SM variable                Description
-------------              -------------
SM_DIV_USERS               List of users
SM_TABLE_USER_PERMISSIONS  Table of computed permissions


### Technical SM variables

SM variable                Description
-------------              -------------
SM_INCLUDE                 Include another HTML template file
SM_SCRIPT                  Include some contextual script

### Obsolete SM variables

These SM variables may be removed in a future release.

SM variable                Description
-------------              -------------
SM_DIV_NAVIGATION_GLOBAL   Global menu bar
SM_DIV_NAVIGATION_ISSUES   Project specific menu bar



## Project with dedicated HTML pages

This may be useful to customize the HTML pages of a specific project.

The dynamic HTML templates are first looked after in `$REPO/<project>/.smip/templates` and, if not present, Smit looks in the `$REPO/.smit/templates` directory.

Therefore, if you want to customize - for example - the 'issues' page for a project:

- copy `$REPO/.smit/templates/issues.html` to `$REPO/<project>/.smip/templates/issues.html`
- customize `$REPO/<project>/.smip/templates/issues.html`


