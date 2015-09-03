# Advanced Admin

## Command Line

```
smit help
```

## Project Configuration

The project configuration defines:

- the properties of the issues
- the tags
- the numbering scheme of the issues (local or global to several projects)

The configuration may be modified in two ways:

- via the web interface
    - only the properties may be managed this way
    - hot reload 
- via editing directly the configuration file, and performing a hot reload via the web interface

The configuration of a project is stored in the object referenced by the file `<p>/.smip/refs/project`.

It can be modified with a text editor, and hot-reloaded from the `Project Config` web page.

### View the project configuration



It can be displayed like this:

```
smit project -l demo-v3/things_to_do
+smv 3.0.0
+parent null
+ctime 1432927321
+author upgrade2db3
setPropertyLabel id "#"
setPropertyLabel ctime Created
setPropertyLabel mtime Modified
setPropertyLabel summary Description
addProperty status -label Status select open closed deleted
addProperty in_charge -label "Person in charge" selectUser
addProperty due_date -label "Due date" select "" "asap" "next week" "next month"
addProperty parent -label "Parent Issue" association -reverseLabel Children
tag tested -label Tested -display
numberIssues global

1 project(s)
```


### addProperty
```
addProperty <id> [-label <label>] [-help <help>] <type> [values ...]
```

`addProperty` defines a property.

- `<id>` is an identifier (only characters a-z, A-Z, 0-9, -, _)
- `<label>` is the text that will be displayed in the HTML pages (optional)
- `<type>` is one of:

    * `text`: free text
    * `select`: selection among a list if given values
    * `multiselect`: same as select, but several may be selected at the same time
    * `selectUser`: selection among the users of the project
    * `textarea`: free text, multi-lines
    * `textarea2`: same as textarea, but spanned on 2 columns in the HTML
    * `association`: references to one or several other issues
    
- `value` indicates the allowed values for types select and multiselect.

### setPropertyLabel

```
setPropertyLabel <propety-id> <label>
```

`setPropertyLabel` defines the label for a property. This is used for mandatory properties that are not defined by `addProperty`: id, ctime, mtime, summary.

### numberIssues

```
numberIssues global
```

`numberIssues` defines the numbering policy of the issues.

If not defined, the issues are numbered reletively to their project: 1, 2, 3,...

If `global` is set, then the numbering is shared by all the projects that have this policy.


### tag

Entries may be tagged.

```
tag <id> -label <text> [-display]
```

- `<id>`: identifier of the tag, may contain only letters, digits and underscores.
- `<text>`: text to display in the HTML page
- `-display`: set the display of a box in the headers of the issues, that indicates if at least one entry of the current issue is tagged

Tags can be used to tag entries with special importance or meaning. Examples :

- "Analysis" to indicate that an entry is a relevant analysis of the issue
- "Action" to indicate that an entry describes the actions to be taken
- ...



### Full example 

```
setPropertyLabel id "The ID"

addProperty status -label "The Status" select open closed
addProperty owner -label "The owner" selectUser

numberIssues global

tag test -label "Test Proof" -display

trigger public/sendEmail.sh
```

## Configuration of Users

The users are configured in the Smit repository in the files:

- `.smit/users/auth`: authentication parameters
- `.smit/users/permissions`: roles on projects

These files can be modified with a text editor, and hot-reloaded from the `Users` web page.


