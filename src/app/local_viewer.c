#include "local_viewer.h"
#include "../base/util.h"

char *local_viewer_html(const char *url, const char *type, bool remote) {
    if (!url || !type || !(remote ? str_istarts(url, "http:") || str_istarts(url, "https:") : str_istarts(url, "file:"))) return NULL;
    if (remote && str_istarts(type, "image/svg")) return NULL;
    bool image = str_istarts(type, "image/"), video = str_istarts(type, "video/"), audio = str_istarts(type, "audio/");
    if (!image && !video && !audio) return NULL;
    SB output; sb_init(&output);
    sb_puts(&output, "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Media · Lumen</title><style>"
        "body{margin:0;background:#eaf3fc;color:#182b40;font:15px system-ui}"
        "header{padding:12px 16px;background:white;border-bottom:1px solid #ccdbe9}"
        ".zoom{display:flex;flex-wrap:wrap;gap:6px;margin-top:10px}"
        "button{font:inherit;min-width:48px;min-height:36px;padding:6px 10px;border:1px solid #b8c9dc;border-radius:6px;background:white;color:#182b40}"
        "button[aria-pressed=true]{background:#235c9b;color:white;border-color:#235c9b}"
        "button:focus{outline:2px solid #235c9b;outline-offset:2px}main{padding:16px;overflow:auto}"
        "img,video{display:block;max-width:none;max-height:none;background:#fff}"
        "audio{width:100%;max-width:640px}p{font-size:13px;color:#476079}"
        "</style></head><body><header><strong>");
    const char *leaf = strrchr(url, '/'); leaf = leaf && leaf[1] ? leaf + 1 : url;
    for (const char *cursor = leaf; *cursor && *cursor != '?' && *cursor != '#'; cursor++) {
        if (*cursor == '<') sb_puts(&output, "&lt;"); else if (*cursor == '&') sb_puts(&output, "&amp;"); else sb_putc(&output, *cursor);
    }
    sb_puts(&output, "</strong>");
    if (!audio) {
        sb_puts(&output, "<div class=\"zoom\" role=\"group\" aria-label=\"Media zoom\">");
        const int levels[] = {10, 25, 50, 75, 100, 150, 200, 300};
        for (size_t index = 0; index < sizeof levels / sizeof levels[0]; index++) {
            sb_printf(&output, "<button type=\"button\" data-zoom=\"%d\" aria-pressed=\"%s\">%d%%</button>",
                      levels[index], levels[index] == 100 ? "true" : "false", levels[index]);
        }
        sb_puts(&output, "</div>");
    }
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
    sb_puts(&output, "<p id=\"status\" role=\"status\">Loading media…</p></main><script>"
        "const media=document.getElementById('media');const status=document.getElementById('status');"
        "media.addEventListener('error',()=>{status.textContent='This file could not be decoded.';});");
    if (audio) sb_puts(&output, "media.addEventListener('loadedmetadata',()=>{status.textContent='Audio — use the playback controls.';});");
    else sb_puts(&output, "const levels=[10,25,50,75,100,150,200,300];const buttons=document.querySelectorAll('[data-zoom]');let percent=100;"
        "function resizeMedia(){const width=media.naturalWidth||media.videoWidth;const height=media.naturalHeight||media.videoHeight;"
        "if(!width||!height)return;const ratio=window.devicePixelRatio||1;"
        "media.style.width=(width*percent/100/ratio)+'px';media.style.height=(height*percent/100/ratio)+'px';"
        "status.textContent=width+' × '+height+' · '+percent+'% · 100% maps one source pixel to one display pixel';}"
        "buttons.forEach(button=>button.addEventListener('click',()=>{const value=Number(button.getAttribute('data-zoom'));"
        "if(!levels.includes(value))return;percent=value;buttons.forEach(item=>item.setAttribute('aria-pressed',String(item===button)));resizeMedia();}));"
        "media.addEventListener('load',resizeMedia);"
        "if(media.tagName==='VIDEO'){media.addEventListener('click',()=>{if(media.paused)media.play();else media.pause();});"
        "const state=()=>{document.title=(media.paused?'Paused':'Playing')+' · Lumen';};media.addEventListener('play',state);media.addEventListener('pause',state);}"
        "media.addEventListener('loadedmetadata',resizeMedia);window.addEventListener('resize',resizeMedia);resizeMedia();");
    sb_puts(&output, "</script></body></html>");
    return sb_take(&output);
}
