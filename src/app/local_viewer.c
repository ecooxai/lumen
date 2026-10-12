#include "local_viewer.h"
#include "../base/util.h"

char *local_viewer_html(const char *url, const char *type) {
    if (!url || !str_istarts(url, "file:") || !type) return NULL;
    bool image = str_istarts(type, "image/"), video = str_istarts(type, "video/"), audio = str_istarts(type, "audio/");
    if (!image && !video && !audio) return NULL;
    SB output; sb_init(&output);
    sb_puts(&output, "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Local media · Lumen</title><style>"
        "body{margin:0;background:#eaf3fc;color:#182b40;font:15px system-ui}"
        "header{padding:12px 16px;background:white;border-bottom:1px solid #ccdbe9}"
        "label{margin-left:20px}select{font:inherit;padding:5px}main{padding:16px;overflow:auto}"
        "img,video{display:block;max-width:none;max-height:none;background:#fff}"
        "audio{width:100%;max-width:640px}p{font-size:13px;color:#476079}"
        "</style></head><body><header><strong>Local media</strong>");
    if (!audio) sb_puts(&output, "<label>Zoom <select id=\"zoom\" aria-label=\"Media zoom\">"
        "<option>10%</option><option>25%</option><option>50%</option><option>75%</option>"
        "<option selected>100%</option><option>150%</option><option>200%</option><option>300%</option>"
        "</select></label>");
    sb_puts(&output, "</header><main>");
    sb_puts(&output, image ? "<img id=\"media\" alt=\"Local image\" src=\"" : video ? "<video id=\"media\" controls playsinline preload=\"metadata\" src=\"" : "<audio id=\"media\" controls preload=\"metadata\" src=\"");
    for (const char *cursor = url; *cursor; cursor++) {
        switch (*cursor) {
        case '&': sb_puts(&output, "&amp;"); break;
        case '<': sb_puts(&output, "&lt;"); break;
        case '>': sb_puts(&output, "&gt;"); break;
        case '"': sb_puts(&output, "&quot;"); break;
        default: sb_putc(&output, *cursor); break;
        }
    }
    sb_puts(&output, image ? "\">" : video ? "\"></video>" : "\"></audio>");
    sb_puts(&output, "<p id=\"status\" role=\"status\">Loading local media…</p></main><script>"
        "const media=document.getElementById('media');const status=document.getElementById('status');"
        "media.addEventListener('error',()=>{status.textContent='This file could not be decoded.';});");
    if (audio) sb_puts(&output, "media.addEventListener('loadedmetadata',()=>{status.textContent='Local audio — use the playback controls.';});");
    else sb_puts(&output, "const levels=[10,25,50,75,100,150,200,300];const zoom=document.getElementById('zoom');"
        "function resizeMedia(){const width=media.naturalWidth||media.videoWidth;const height=media.naturalHeight||media.videoHeight;"
        "if(!width||!height)return;const percent=levels[zoom.selectedIndex]||100;const ratio=window.devicePixelRatio||1;"
        "media.style.width=(width*percent/100/ratio)+'px';media.style.height=(height*percent/100/ratio)+'px';"
        "status.textContent=width+' × '+height+' · '+percent+'% · 100% maps one source pixel to one display pixel';}"
        "zoom.addEventListener('change',resizeMedia);media.addEventListener('load',resizeMedia);"
        "media.addEventListener('loadedmetadata',resizeMedia);window.addEventListener('resize',resizeMedia);resizeMedia();");
    sb_puts(&output, "</script></body></html>");
    return sb_take(&output);
}
