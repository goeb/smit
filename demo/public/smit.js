function changeWrapping(){
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
    alert("updateFileInput");
    var els = document.getElementsByClassName(classname);
    alert("length(els)="+length(els));
    if (length(els) > 0) {
        var i;
        var oneSlotFree = false;
        for (i=0; i<length, i++) {
            if (length(els[i].value) == 0) oneSlotFree = true;
        }
    alert("oneSlotFree="+oneSlotFree);
        if (!oneSlotFree) {
            cell = els[0].parent;
            input = document.createElement('input');
            input.type = 'file';
            input.class = 'sm_input_file';
            input.onchange = "updateFileInput('sm_input_file')";
            input.name = '+file'; // same as C++ K_FILE
            cell.appendChild(input);
        }
    }
}
