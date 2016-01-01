# News

## 6 November 2015

Smit v3.2.1 is released

Main changes:

- HTML interface
    * navigating through issues: one-click filter-out of a group
    * searching: new filterout logical combination
    * improved message preview: line breaks taken into account
    * list of latest entries
    * administration: on creation of a new project, copy the config of another
- Accelerated pulling (not compatible with v3.0.1)
- Fixed duplicate file upload

Source code: [git tag v3.2.1](https://github.com/goeb/smit/tree/v3.2.1)

## 29 July 2015

Smit v3.0.0 is released.

Smit v3 is not compatible with a smit-v2 database. You need to migrate using the script 'upgrade2db3' (bash-compatible shell needed).

Other major changes are:

- Clone / Pull / Push Capabilities
- Page Advanced Search: Suggested values for search filters (filters in/out)



## 1 April 2015

Smit v2.3.0 is released.

Major changes since v2.0:

- keep old values of select, multiselect, selectUser until deliberately changed by the user
- fix in-memory residual property after deleted entry
- textarea2 in rich text
- fix race condition in assignment of session ids
- support for root-URL rewriting (for using behind a reverse proxy)
- improve advanced search filtering with more usage-friendly AND/OR combination
- fix freezed login
- fix utf-8 characters in project config


## 3 August 2014

Smit 2.0.0 is released.

This version brings a major new feature: the ability to clone locally a remote Smit repository (commands `smit clone`  and `smit ui`).

Other improvements:

- tags can be managed via the web interface
- Windows installer
- fixed javascript injections
- salted passwords


## 28 May 2014

Smit 1.7.0 is released.

New features:

- associations between issues
- keyword "me" in full-text search
- full-text search also looks through authors of entries


## 3 May 2014

Smit 1.6.0 is released.

New features:

- navigation to next/previous issue
- improve filter-in/out with select lists
- hot reload of project configuration
- trigger external program on new entry
- numbering of issues global to several projects
- textarea properties
- tags
- print preview
- various bug fixes


## 27 Jan 2014

Smit v1.2 is released.

New features:

- In page project config, arrows help reorder the properties
- In page issue, show/hide entries with no contents (only properties changes)
- In page issue, tag/untag entries
- In page issue or newIssue, preview the message before posting
- Option `--ssl-cert` for HTTPS and server-side certificate (SSL/TLS)


Downloads:

- Source code: [git tag v1.2.0](https://github.com/goeb/smit/tree/v1.2.0)
- Windows build: [smit-win32-1.2.0.zip](downloads/smit-win32-1.2.0.zip)

The Windows build includes `openssl-1.0.1e` for SSL/TLS procedures.

## 7 Jan 2014

Smit v1.1.1 is released.

- Source code: [git tag v1.1.1](https://github.com/goeb/smit/tree/v1.1.1)
- Windows build: [smit-win32-1.1.1.zip](downloads/smit-win32-1.1.1.zip)

## 28 Dec 2013

Smit v1.1.0 is released.

- Source code: [git tag v1.1](https://github.com/goeb/smit/tree/v1.1)
- Windows build: [smit-win32-1.1.0.zip](downloads/smit-win32-1.1.0.zip)
