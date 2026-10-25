'use strict';
(async function () {
    const list = document.getElementById('writeup-list');
    
    let data;
    try {
        data = await api('/api/writeups?limit=50&offset=0');
    } catch (err) {
        list.innerHTML = `<div class="terminal-block">> [ERROR] ${err.message}</div>`;
        return;
    }
    
    if (data.total === 0) {
        // Keep your empty state if no writeups
        list.innerHTML = `
        <div class="terminal-block">
            > [204 NO_CONTENT]: Writeup index is currently empty.<br>
            > Pending publication of initial CTF post-mortems.
        </div>`;
        return;
    }

    list.textContent = '';
    const board = el('div', { class: 'archive-board' });

    for (let i = 0; i < data.writeups.length; i++) {
        const w = data.writeups[i];
        const row = el('a', { href: '/blog/ctf_writeups/writeup.html?slug=' + encodeURIComponent(w.slug), class: 'archive-row' },
            el('span', { class: 'badge' }, `#${String(i + 1).padStart(3, '0')}`),
            el('span', {}, el('strong', {}, w.title)),
            el('span', { style: 'color: var(--fg-muted);' }, `${fmtDate(w.created_at)} | ${w.author || 'system'}`)
        );
        board.append(row);
    }
    
    list.append(board);
})();