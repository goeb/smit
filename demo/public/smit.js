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
function createSelect(items, selected) {
    var select = document.createElement('select');
    var items2 = items.slice(0); // copy the array
    items2.push(''); // add an empty value
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

