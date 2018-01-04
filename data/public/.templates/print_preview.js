function togglePrintPreview(id)
{
    var currCSS = document.getElementById(id);
    if(currCSS.media == 'all') currCSS.media = 'print';
    else currCSS.media = 'all';
    var aTags = document.getElementsByTagName('a');
    var atl = aTags.length;
    var i;

    for (i = 0; i < atl; i++) {
        aTags[i].removeAttribute("href");
    } 
}
