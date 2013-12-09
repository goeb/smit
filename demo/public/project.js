/* Small Issue Tracker
 * Copyright (C) 2013 Frederic Hoerni
 * GNU General Public License v2
 */

function addMoreProperties(n) {
    // add 3 more properties on page
    for (var i=0; i<n; i++) addProperty('', '', '', '');
}

function addProperty(name, label, type, opts) {
    var table = document.getElementById('sm_config_properties');
    var n = table.rows.length;
    var row = table.insertRow(n);

    row.id = 'property' + n;

    // input boxes for this property
    var cell;
    cell = row.insertCell(row.cells.length);
    i = document.createElement('input');
    i.type = "hidden"; i.name = "token"; i.value = "EOL";
    cell.appendChild(i);

    var i = document.createTextNode('#' + n + ' ');
    cell.appendChild(i);

    cell.appendChild(i);

    cell = row.insertCell(row.cells.length);
    i = document.createElement('input');
    i.type = "hidden"; i.name = "token"; i.value = "addProperty";
    cell.appendChild(i);

    i = document.createElement('input');
    i.name = 'token';
    i.size = 15;
    i.value = name;
    i.pattern = "[-a-zA-Z_0-9]+"
        i.placeholder = "logical_name"
        i.title = "Allowed characters: letters, digits, underscore"
        if (type == 'reserved') i.disabled = true;
    cell.appendChild(i);

    cell = row.insertCell(row.cells.length);
    cell.id = 'type_' + n;
    if (type == 'reserved') {
        i = document.createElement('span');
        i.innerHTML = 'reserved';
        cell.appendChild(i);
    } else {
        i = document.createElement('select');
        i.name = 'token';
        i.className = "updatable";
        i.onchange = fupdateThis;
        var options = ['text', 'select', 'multiselect', 'selectUser'];
        for (index=0; index<options.length; index++) {
            opt = document.createElement('option');
            opt.innerHTML = options[index];
            opt.value = options[index];
            opt.text = options[index];
            if (type == opt.text) {
                opt.selected = 1;
		i.selectedIndex = index;
            }
            i.appendChild(opt);
        }
        cell.appendChild(i);
        fupdate(i, opts);
    }

    // label
    cell = row.insertCell(row.cells.length);
    i = document.createElement('input');
    i.type = "hidden"; i.name = "token"; i.value = "EOL";
    cell.appendChild(i);

    i = document.createElement('input');
    i.type = "hidden"; i.name = "token"; i.value = "setPropertyLabel";
    cell.appendChild(i);

    i = document.createElement('input');
    i.type = "hidden"; i.name = "token"; i.value = name;
    cell.appendChild(i);

    i = document.createElement('input');
    i.name = 'token';
    i.size = 20;
    i.value = label;
    i.placeholder = "Label that will be displayed";
    i.title = "Label that will be displayed";
    cell.appendChild(i);
}

function fupdateThis() { fupdate(this); }

function fupdate(item, value) {
    //alert(item.value);
    var type = item.options[item.selectedIndex].value;
    if (type == "text") show_size_input(item.parentNode, value);
    else if (type == "select") show_list_input(item.parentNode, value);
    else if (type == "multiselect") show_list_input(item.parentNode, value);
    else if (type == "selectUser") show_user_input(item.parentNode);
}

function show_size_input(item, value) {
    // hide extra details
    x = document.getElementById(item.id + '_opt');
    if (x != null) item.removeChild(x);
}

function show_list_input(item, value) {
    var i = document.getElementById(item.id + '_opt');
    if (i == null) {
        i = document.createElement('textarea')
            i.name = "token"
            i.id = item.id + '_opt';
        i.rows = 4;
        i.cols = 20;
        i.value = "one\r\nvalue\nper\nline"
            item.appendChild(i);
    }
    if (value) i.value = value;
}

function show_user_input(item) {
    x = document.getElementById(item.id + '_opt');
    if (x != null) item.removeChild(x);
}

function init() {
    //addProperty('id', '#', 'reserved', '');
    //addProperty('ctime', 'Created', 'reserved', '');
    //addProperty('mtime', 'Modified', 'reserved', '');
    //addProperty('summary', 'Description', 'reserved', '');
    //addProperty('status', '', 'select', 'open\nclosed\n');
    //addProperty('priority', '', 'select', 'high\nmedium\nlow\n');
    //addProperty('assignee', '', 'selectUser', '');
    //replaceContentInContainer();
}

function replaceContentInContainer() {
    matchClass = "updatable";

    var elems = document.getElementsByTagName('*'), i;
    for (i in elems) {
        if((' ' + elems[i].className + ' ').indexOf(' ' + matchClass + ' ')
                > -1) {
                    fupdate(elems[i]);
                }
    }
}
