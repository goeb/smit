/* Small Issue Tracker
 * Copyright (C) 2013 Frederic Hoerni
 * GNU General Public License v2
 */
function changeWrapping() {
    var msg = document.getElementsByName('+message')[0];
    if (msg.wrap != "off") msg.wrap = "off";
    else msg.wrap = "hard";
}

function deleteEntry(urlPrefix, entryId) 
{
    var r = confirm("Confirm delete?");
    if (r==true) {
        var request = new XMLHttpRequest();
        request.open('POST', urlPrefix + '/' + entryId + '/delete', false); // synchronous
        request.send(null);
        status = request.status;
        if (status == 200) {
            // ok
            // remove entry from current HTML page
            e = document.getElementById(entryId);
            e.parentNode.removeChild(e);
        } else alert('error');
    }
}

function updateFileInput(classname) {
    var inputs = document.getElementsByClassName(classname);
    if (inputs.length > 0) {
        var i;
        var oneSlotFree = false;
        for (i=0; i<inputs.length; i++) {
            if (inputs[i].value == "") oneSlotFree = true;
        }

        if (!oneSlotFree) {
            lastInput = inputs[i-1];
            var cell = lastInput.parentNode;
            var br = document.createElement('br');
            cell.appendChild(br);

            // duplicate
            newInput = lastInput.cloneNode(true);
            newInput.value = "";
            cell.appendChild(newInput);
        }
    }
}
function createSelect(items, selected, allowVoid) {
    var select = document.createElement('select');
    var items2 = items.slice(0); // copy the array
    if (allowVoid) items2.push(''); // add an empty value
    var length = items2.length;
    for (var i = 0; i < length; i++) {
        var opt = document.createElement('option');
        opt.innerHTML = items2[i];
        opt.value = items2[i];
        opt.text = items2[i];
        if (selected == items2[i]) {
            opt.selected = "selected";
            select.selectedIndex = i;
        }
        select.appendChild(opt);
    }
    return select;
}

function hideSuperadminZone() {
    var divs = document.getElementsByClassName('sm_user_superadmin_zone');
    for(var i=0; i<divs.length; i++) {
          divs[i].style.display='none';
    }
}
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
    i.className = 'sm_project_propname';
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
    i.className = 'sm_project_label';
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
        i = document.createElement('textarea');
        i.name = "selectOptions";
        i.id = item.id + '_opt';
        i.className = 'sm_project_list';
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

function addProject(divname, selectedProject, selectedRole) {
    var div = document.getElementById(divname);

    var select = createSelect(Projects, selectedProject, true);
    select.name = 'project';
    div.appendChild(select);
    var i = createSelect(Roles, selectedRole, true);
    i.name = 'role';
    div.appendChild(i);
    div.appendChild(document.createElement('br'));
}
function setName(value) {
    var iname = 'name';
    var input = document.getElementById(iname);
    input.value = value;
}
function setSuperadminCheckbox() {
    var input = document.getElementById('sm_superadmin');
    input.checked = true;
}

function addFilter(divname, selected, value) {
    var div = document.getElementById(divname);
    var select = createSelect(Properties, selected, true);
    select.name = divname;
    div.appendChild(select);
    var i = document.createElement('input');
    i.type = "text";
    i.value = value;
    i.name = 'filter_value';
    div.appendChild(i);
    div.appendChild(document.createElement('br'));
}
function addColspec(selected) {
    var divname = 'colspec';
    var div = document.getElementById(divname);
    var select = createSelect(Properties, selected, true);
    select.name = 'colspec';
    div.appendChild(select);
}
function addSort(selectedDirection, selectedProperty) {
    var divname = 'sort';
    var div = document.getElementById(divname);
    var select = createSelect(['Ascending', 'Descending'], selectedDirection, true);
    select.name = 'sort_direction';
    div.appendChild(select);
    select = createSelect(Properties, selectedProperty, true);
    select.name = 'sort_property';
    div.appendChild(select);
    div.appendChild(document.createElement('br'));
}
function setSearch(value) {
    var iname = 'search';
    var input = document.getElementById(iname);
    input.value = value;
}
function setName(value) {
    var iname = 'name'; 
    var input = document.getElementById(iname);
    input.value = value;
}
function setUrl(value) {
    var aname = 'sm_view_current_url'; 
    var a = document.getElementById(aname);
    a.href = value;
    a.innerHTML = value;
}
function hideAdminZone() {
    var divs = document.getElementsByClassName('sm_view_admin_zone');
    for(var i=0; i<divs.length; i++) { 
          divs[i].style.display='none';
    }
}
function setDefaultCheckbox() {
    var input = document.getElementById('sm_default_view');
    input.checked = true;
}
