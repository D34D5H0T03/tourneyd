'use strict';
(async function () {
    // 1. Grab all the elements specific to challenge.html
    const title = document.getElementById('title');
    const metaCat = document.getElementById('meta-category');
    const metaPts = document.getElementById('meta-points');
    const description = document.getElementById('description');
    const form = document.getElementById('flag-form');
    const input = document.getElementById('flag');
    const button = document.getElementById('submit');
    const result = document.getElementById('result');

    const slug = param('id'); 
    
    if (!slug || !SLUG_RE.test(slug)) { 
        title.textContent = 'Target Not Found'; 
        return; 
    }

    let challenge;
    try {
        // 3. Fetch the single challenge
        challenge = await api('/api/challenges/' + slug);
    } catch (err) {
        title.textContent = err.status === 404 ? 'Target Not Found' : 'System Error';
        description.textContent = `> ${err.message}`;
        return;
    }

    // 4. Populate the HTML elements
    document.title = challenge.title + ' | tourneyd';
    title.textContent = challenge.title;
    metaCat.textContent = `[${challenge.category.toUpperCase()}]`;
    metaPts.textContent = `${challenge.points} PTS`;
    renderText(description, challenge.description);
    
    // Unhide the form now that data has loaded
    form.style.display = 'block';
    
    // Check if previously solved
    if (solvedSet().has(slug)) {
        result.textContent = '> [SYS] Target previously compromised on this node.';
        result.style.color = '#81c995';
    }

    // 5. Handle Flag Submission
    form.addEventListener('submit', async (event) => {
        event.preventDefault();
        const flag = input.value.trim();
        if (!flag) return;
        
        button.disabled = true;
        button.textContent = 'AUTH...';
        
        try {
            const reply = await api('/api/challenges/' + slug + '/submit', {
                method: 'POST',
                headers: { 'Content-Type': 'text/plain' },
                body: flag,
            });
            if (reply.correct) {
                result.textContent = '> [ACCESS GRANTED] Flag accepted.';
                result.style.color = '#81c995';
                markSolved(slug);
                input.value = '';
            } else {
                result.textContent = '> [ACCESS DENIED] Invalid payload.';
                result.style.color = 'var(--accent)';
            }
        } catch (err) {
            result.textContent = `> [ERR] ${err.message}`;
            result.style.color = 'var(--accent)';
        } finally {
            button.disabled = false;
            button.textContent = 'SUBMIT_FLAG';
        }
    });
})();