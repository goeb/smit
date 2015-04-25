# Smit API

Smit exposes some of its data through a REST API, and others through the trigger API.

## Signing in
REST API:

```
POST /signin
```

*Parameters:*

- `username`: the user name
- `password`: the password

Syntax: application/x-www-form-urlencoded

*Response:*

- `sessid`: the session id




## Listing Issues
REST API:

```
GET /<project>/issues
```

Returns a list of issues. Without any parameter, it returns all the issues.

*Parameters*

- `filterin`: get issues that have a specific property
    * Syntax: property`:`value
    * Example: filterin=status:open&filterin=status:in_progress

- `filterout`: get issues that do not have a specific property
    * Syntax: same as `filterin`
    * Example: filterout=status:closed
- `search`: search some text through all messages and properties
    * Example: search=my tailor is rich
- `colspec`: select which columns should be returned
    * Syntax: properties names separated by spaces
    * Example: colspec=id+summary
- `sort`: sort against speficic columns. Several columns are allowed.
    * Syntax: columns, prefixed by `+` (ascending order) or `-` (descending order)
    * Example: sort=+target_version-mtime
- `full`: display issues with their full contents. Value must be `1`
    * Syntax: full=1
- `format`: select format of response: `html` (the default), `text`, `csv`

*Examples:*

```
GET /things_to_do/issues?search=john&sort=-mtime&filterin=status:open
GET /things_to_do/issues?search=john&sort=-mtime&filterin=status:open?format=csv
```

## Showing an issue
REST API:

```
GET /<project>/issues/<id>
```

*Parameters:*

- `format`: select format of response: `html` (the default), `text`

*Examples:*

```
GET /things_to_do/issues/543
GET /things_to_do/issues/89?format=text
```


## Creating an issue
REST API:

```
POST /<project>/issues/new
```

*Parameters:*

- A hash of the issue properties
    * Syntax: multipart/form-data

Files can be attached.



## Updating an issue
REST API:

```
POST /<project>/issues/<id>
```

*Parameters:*

- the issue properties
    * Syntax: multipart/form-data

Files can be attached.


## Deleting an issue
REST API:

work in progress...


