
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
    var x = document.getElementById("smit_data");
    if (x) {
        var data = JSON.parse(document.getElementById("smit_data").innerHTML);
        for(var key in data) update(key, data[key]);
    }
}

function changeWrapping(){
    var msg = document.getElementsByName('message')[0];
    if (msg.wrap != "off") msg.wrap = "off";
    else msg.wrap = "hard";
}

window.onload = updateFields;
