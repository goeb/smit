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
    var i = document.createTextNode('#' + n + ' ');
    cell.appendChild(i);

    cell = row.insertCell(row.cells.length);
    i = document.createElement('input');
    i.name = 'propertyName';
    i.size = 15;
    i.value = name;
    i.pattern = "[-a-zA-Z_0-9]+";
    i.placeholder = "logical_name";
    i.title = "Allowed characters: letters, digits, underscore, dash";
    if (type == 'reserved') {
        i.type = 'hidden';
        cell.appendChild(i);
        var text = document.createTextNode(name);
        cell.appendChild(text);
    } else {
        cell.appendChild(i);
    }

    // label
    cell = row.insertCell(row.cells.length);
    i = document.createElement('input');
    i.name = 'label';
    i.size = 20;
    i.value = label;
    i.placeholder = "Label that will be displayed";
    i.title = "Label that will be displayed";
    cell.appendChild(i);


    cell = row.insertCell(row.cells.length);
    cell.id = 'type_' + n;
    if (type == 'reserved') {
        i = document.createElement('span');
        i.innerHTML = 'reserved';
        cell.appendChild(i);
    } else {
        var options = ['text', 'select', 'multiselect', 'selectUser'];
        i = createSelect(options, type, false);
        i.name = 'type';
        i.className = "updatable";
        i.onchange = fupdateThis;
        cell.appendChild(i);
        fupdate(i, opts);
    }

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
            i.name = "selectOptions"
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
function setProjectName(value) {
    var iname = 'projectName';
    var input = document.getElementById(iname);
    input.value = value;
}

