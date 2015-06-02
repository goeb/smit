/* Small Issue Tracker */
function changeWrapping() {
    var msg = document.getElementsByName('+message')[0];
    if (msg.wrap != "off") msg.wrap = "off";
    else msg.wrap = "hard";
}

function ajaxSend(url, method) {
    var request = new XMLHttpRequest();
    request.open(method, url, false); // synchronous
    request.send(null);
    var status = request.status;
    if (status == 200) return 'ok';
    else return request.responseText;
}
function previewMessage() {
    var divPreview = document.getElementById('sm_entry_preview');
    if (!divPreview) {
        alert('Preview not available');
        return;
    }
    var msg = document.getElementsByName('+message')[0];
    var value = msg.value;
    // url-encode value TODO
    var url = '/sm/preview?message=' + encodeURIComponent(value);
    var request = new XMLHttpRequest();
    request.open('GET', url, false); // synchronous
    request.send(null);
    divPreview.innerHTML = request.responseText;
}

function sm_deleteResource(redirect) {
    var r = confirm("Confirm delete?");
    if (r==true) {
        r = ajaxSend('#', 'DELETE');
        if (r != 'ok') alert(r);
        else window.location.href = redirect;
    }
}

function tagEntry(urlPrefix, entryId, tagId) {
    var r = ajaxSend(urlPrefix + '/' + entryId + '/' + tagId, 'POST');
    if (r == 'ok') {
        var etag = document.getElementById('sm_tag_' + entryId + "_" + tagId);
        var doTag = false;
        taggedStyle = 'sm_entry_tag_' + tagId;

        if (etag.className.match(/sm_entry_notag/)) doTag = true;

        // tag of the entry
        if (doTag) {
            etag.className = etag.className.replace('sm_entry_notag', '');
            etag.className = etag.className + ' sm_entry_tagged ' + taggedStyle;
        } else {
            etag.className = etag.className.replace('sm_entry_tagged', '');
            etag.className = etag.className.replace(taggedStyle, '');
            etag.className = etag.className + ' sm_entry_notag';
        }

        // entry
        var e2 = document.getElementById(entryId);
        if (doTag) {
            e2.className = e2.className.replace('sm_entry_notag', '');
            e2.className = e2.className + ' sm_entry_tagged ' + taggedStyle;
        } else {
            e2.className = e2.className.replace('sm_entry_tagged', '');
            e2.className = e2.className.replace(taggedStyle, '');
            e2.className = e2.className + ' sm_entry_notag';
        }

        // update the box of the issue header
        var box = document.getElementById('sm_issue_tag_' + tagId);
        if (box) {
            var n = parseInt(box.getAttribute('data-n'), 10);
            if (doTag) n = n+1;
            else n = n-1;
            box.setAttribute('data-n', n);
            if (n > 0) box.className = 'sm_issue_tagged';
            else box.className = 'sm_issue_notag';
        }

    } else alert('error');
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
function createDatalist(id, items) {
    var datalist = document.createElement('datalist');
    datalist.id = id;
    var length = items.length;
    for (var i = 0; i < length; i++) {
        var opt = document.createElement('option');
        opt.innerHTML = items[i];
        opt.value = items[i];
        opt.text = items[i];
        datalist.appendChild(opt);
    }
    return datalist;
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
        var inputs = divs[i].getElementsByTagName('input');
        for(var j=0; j<inputs.length; j++) inputs[j].disabled = true;
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
    var buttonUp = document.createElement('button');
    buttonUp.innerHTML = '&#9650;';
    buttonUp.onclick = function() {moveRowUp(buttonUp); return false;};
    cell.appendChild(buttonUp);
    var buttonDown = document.createElement('button');
    buttonDown.innerHTML = '&#9660;';
    buttonDown.onclick = function() {moveRowDown(buttonDown); return false;};
    cell.appendChild(buttonDown);

    cell = row.insertCell(row.cells.length);
    i = document.createElement('input');
    i.name = 'propertyName';
    i.className = 'sm_project_propname';
    i.value = name;
    i.pattern = "[a-zA-Z_0-9]+";
    i.placeholder = "logical_name";
    i.title = "Allowed characters: letters, digits, underscore";
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
        var options = ['text', 'select', 'multiselect', 'selectUser', 'textarea', 'textarea2', 'association'];
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
    if (type == "text") show_size_input(item.parentNode);
    else if (type == "textarea") show_size_input(item.parentNode);
    else if (type == "textarea2") show_size_input(item.parentNode);
    else if (type == "select") show_list_input(item.parentNode, value);
    else if (type == "multiselect") show_list_input(item.parentNode, value);
    else if (type == "selectUser") show_user_input(item.parentNode);
    else if (type == "association") show_association_input(item.parentNode, value);
}

function show_size_input(item) {
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
function show_association_input(item, value) {
	var i = document.getElementById(item.id + '_opt');
    if (i == null) {
        i = document.createElement('input');
        i.name = 'reverseAssociation';
        i.id = item.id + '_opt';
        i.placeholder = "label of the reverse association";
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

function addFilterDatalist(divname, id, items) {
    var div = document.getElementById(divname);
    var datalist = createDatalist(id, items);
    div.appendChild(datalist);
}
function addProjectDatalist(divname) {
    var div = document.getElementById(divname);
    var datalist = createDatalist('datalist_projects', Projects);
    div.appendChild(datalist);

}
function addPermission(divname, selectedProject, selectedRole) {
    var div = document.getElementById(divname);
    var datalist = document.getElementById('datalist_projects');

    // input for the project name or wildcard
    var input = document.createElement('input');
    input.type = 'text';
    input.value = selectedProject;
    input.setAttribute('list', 'datalist_projects');
    input.name = 'project_wildcard';
    div.appendChild(input);

    // input for the role (ref, ro, rw,...)
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

function removeElement(node) {
    if (node) node.parentNode.removeChild(node);
}

function updateFilterValue(divObject, value) {
    // divObject must have 1 or 2 children: a select a optionnaly another input
    // remove the second one, and recalculate it
    removeElement(divObject.childNodes.item(1));
    var selectedKey = divObject.childNodes.item(0);

    // build text input
    var i = document.createElement('input');
    i.type = "text";
    i.value = value;
    i.name = 'filter_value_' + randomString(5);
    i.setAttribute('list', 'datalist_'+selectedKey.value);
    divObject.appendChild(i);
}

function addFilter(divname, selectedProperty, value) {
    var div = document.getElementById(divname);
    var select = createSelect(Properties, selectedProperty, true);
    select.name = divname;

    var container = document.createElement('div');
    container.className = 'sm_filter';
    container.appendChild(select);
    select.onchange = function() { updateFilterValue(container, value); };
    // update now
    updateFilterValue(container, value);
    div.appendChild(container);
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
        var inputs = divs[i].getElementsByTagName('input');
        for(var j=0; j<inputs.length; j++) inputs[j].disabled = true;
    }
}
function setDefaultCheckbox() {
    var input = document.getElementById('sm_default_view');
    input.checked = true;
}
function showPropertiesChanges() {
    var i1 = document.getElementsByClassName('sm_entry_no_contents');
    for(var i=0; i<i1.length; i++) { i1[i].style.display='block'; }

    var i2 = document.getElementsByClassName('sm_entry_other_properties');
    for(var i=0; i<i2.length; i++) { i2[i].style.display='block'; }
}
function hideUntaggedEntries() {
    var i1 = document.getElementsByClassName('sm_entry_notag');
    for(var i=0; i<i1.length; i++) { i1[i].style.display='none'; }
}

function moveRowUp(item) {
    var tr = item.parentNode.parentNode;
    var prevRow = tr.previousElementSibling;
    if (prevRow) {
        var parent = tr.parentNode;
        parent.insertBefore(tr, prevRow);
    }
}
function moveRowDown(item) {
    var tr = item.parentNode.parentNode;
    var nextRow = tr.nextElementSibling;
    if (nextRow) {
        var parent = tr.parentNode;
        parent.insertBefore(nextRow, tr);
    }
}
function updateHref(className, href) {
    var items = document.getElementsByClassName(className);
    for(var i=0; i<items.length; i++) {
        if (!href) items[i].style.display = 'none';
        else if (items[i].nodeName == 'A') items[i].href = href;
    }
}
function addTag(name, label, display) {
    var table = document.getElementById('sm_config_tags');
    var n = table.rows.length;
    var row = table.insertRow(n);

    row.id = 'tag' + n;

    // input boxes for this property
    var cell;
    cell = row.insertCell(row.cells.length);
    var buttonUp = document.createElement('button');
    buttonUp.innerHTML = '&#9650;';
    buttonUp.onclick = function() {moveRowUp(buttonUp); return false;};
    cell.appendChild(buttonUp);
    var buttonDown = document.createElement('button');
    buttonDown.innerHTML = '&#9660;';
    buttonDown.onclick = function() {moveRowDown(buttonDown); return false;};
    cell.appendChild(buttonDown);

    cell = row.insertCell(row.cells.length);
    i = document.createElement('input');
    i.name = 'tagName';
    i.className = 'sm_project_tagname';
    i.value = name;
    i.pattern = "[a-zA-Z_0-9]+";
    i.placeholder = "logical_name";
    i.title = "Allowed characters: letters, digits, underscore";
    cell.appendChild(i);

    // label
    cell = row.insertCell(row.cells.length);
    i = document.createElement('input');
    i.name = 'label';
    i.className = 'sm_project_label';
    i.value = label;
    i.placeholder = "Label that will be displayed";
    i.title = "Label that will be displayed";
    cell.appendChild(i);

    // checkbox
    cell = row.insertCell(row.cells.length);
    cell.id = 'type_' + n;
    i = document.createElement('input');
    i.type = 'checkbox';
    i.name = 'tagDisplay';
    if (display) i.checked = true;
    cell.appendChild(i);
}
function addMoreTags(n) {
    // add n more tags on page
    for (var i=0; i<n; i++) addTag('', '', false);
}
function randomString(length) {
    var chars = '0123456789abcdefghijklmnopqrstuvwxyz';
    var result = '';
    for (var i = length; i > 0; --i) result += chars[Math.round(Math.random() * (chars.length - 1))];
    return result;
}
