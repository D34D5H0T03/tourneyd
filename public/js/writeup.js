'use strict';
(async function () {
    const title = document.getElementById('title');
    const header = document.getElementById('meta-header');
    const body = document.getElementById('body');
    const sourceBtn = document.getElementById('source');

    const slug = param('slug');
    if (!slug || !SLUG_RE.test(slug)) { title.textContent = 'Document Not Found'; return; }

    let w;
    try {
        w = await api('/api/writeups/' + slug);
    } catch (err) {
        title.textContent = err.status === 404 ? 'Document Not Found' : 'System Error';
        body.textContent = `> ${err.message}`;
        return;
    }

    document.title = w.title + ' | tourneyd archive';
    title.textContent = w.title;
    
    header.append(
        el('span', { class: 'badge' }, `[AUTHOR: ${w.author ? w.author.toUpperCase() : 'SYSTEM'}]`),
        el('span', {}, `DATE: ${fmtDate(w.created_at)}`),
        w.challenge ? el('span', {}, `TARGET: ${w.challenge.toUpperCase()}`) : ''
    );

    renderText(body, w.body_md);

    const url = w.source_url ? safeExternalUrl(w.source_url) : null;
    if (url) {
        sourceBtn.href = url;
        sourceBtn.style.display = 'inline-flex';
    }
})();