# Issue Tracking

Issue Tracking is the art of managing issues in order to get a clear status of unresolved issues and a history of what happened about all issues.

> An issue database is approximately a 3D matrix:
>
> - a row for each *issue*
> - a column for each *property* (eg: status, priority, ...)
> - a depth axis for the *history* of messages
> 

The main pitfalls that issue tracker software have to tackle are:

- information lost in a mass of issues
- cumbersome countless properties
- interoperability with existing tools
- responsiveness of the GUI

This page explains how Smit has been designed to overcome these pitfalls.

## A mass of issues
It is usual for an organization to have a mass of issues (thousands or tens of thousands). It is thus hard to find a particular issue by reading all.

To deal with this mass of issues Smit features:

- a "one-click" multi-property sorting interface
- full text searching, that searches through all messages and properties
- an agile property model that can be updated online when necessary

## Countless properties
Some organizations' issue trackers have a great number of properties in their model because they want to deal with all possible cases. Some properties are not needed now, but they have been added to the model just in case they may be used in the future.

But this has the following disadvantages: issues are encumbered with many properties, and that slows down the reading of the issues, and also the creation of a new issue as the author spends some time to wonder how to fulfill all these properties.

With Smit it is easy to modify the issue model: add, modify or delete properties at any time, online. Therefore there is no point in adding properties for future use.

Note that modifying a property in Smit does not threaten the integrity of existing issues.


## Interoperability

Organizations that care for sustainable activity do not want to be bound to a software. They want to be able to export or migrate the data from a software to another.

Organizations that care for business or industrial efficiency want to have their databases and software cooperate (talk together).

Smit provides:

- CSV format, for exportation to Excel or Calc
- a full-contents view, suitable for printing or PDF export

## High Responsiveness

Workers do not want to be slowed down by software. Nobody wants to wait for 10 seconds for the page to be displayed.

Smit is really fast and does not use heavy images and javascript.






