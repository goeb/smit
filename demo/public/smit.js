
// The HTML page must include the smit_data
// <script id="smit_data" type="application/json">
//      {"smit_user": "Mr. John", "smit_issueNumber": 34}
// </script>
// 
// and where to display this information:
// Logged in as <span class="smit_user">nobody</span>

function update(classname, value)
{
    var els = document.getElementsByClassName(classname);
    for(var i=0; i<els.length; i++) els[i].innerHTML = value;
}

function updateFields()
{
    var data = document.getElementById("sm_data");
    if (data) {
        var data = JSON.parse(data.innerHTML);
        for(var key in data) update(key, data[key]);
    }
}

function changeWrapping(){
    var msg = document.getElementsByName('message')[0];
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

window.onload = updateFields;
