'use strict';
(async function () {
    const list = document.getElementById('challenge-list');
    const filterBtns = document.querySelectorAll('.filter-btn');
    
    let data;
    try {
        data = await api('/api/challenges');
    } catch (err) {
        list.innerHTML = `<div class="terminal-block">> [ERROR] ${err.message}</div>`;
        return;
    }
    
    const items = data.challenges;
    if (items.length === 0) {
        list.innerHTML = `<div class="terminal-block">> [204 NO_CONTENT] Targets offline.</div>`;
        return;
    }

    const solved = solvedSet();
    let activeCategory = 'all';

    function render() {
        list.textContent = '';
        for (const c of items) {
            if (activeCategory !== 'all' && c.category !== activeCategory) continue;
            const done = solved.has(c.slug);
            
            const card = el('a', { href: '/event/ctf/challenge.html?id=' + encodeURIComponent(c.slug), class: 'card' },
                el('div', {}, 
                    el('div', { class: 'card-meta' },
                        el('span', { class: 'badge' }, `[${c.category.toUpperCase()}]`),
                        el('span', {}, `${c.points} PTS`)
                    ),
                    el('h3', { class: 'card-title' }, c.title)
                ),
                el('div', { class: 'card-meta', style: 'margin-bottom: 0;' },
                    el('span', {}, done ? 'STATUS: PWNED' : 'STATUS: SECURE'),
                    el('span', { style: done ? 'color: #81c995;' : '' }, done ? '[x]' : '[ ]')
                )
            );
            
            if (done) card.style.borderColor = '#81c995'; // Highlight solved
            list.append(card);
        }
    }

    // Attach to your hardcoded UI buttons
    filterBtns.forEach(btn => {
        btn.addEventListener('click', () => {
            activeCategory = btn.getAttribute('data-category');
            filterBtns.forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            render();
        });
    });

    render();
})();