// Page motion, Framer style, on Motion (assets/vendor/motion.min.js): springy reveals, headings
// rising word by word, a scroll progress bar, the hero lifting away on scroll, cards that tilt
// and catch a light under the pointer, buttons that press, numbers that count up. Nothing runs
// under prefers-reduced-motion, and nothing changes brightness quickly.
(() => {
  const M = window.Motion;
  if (!M || matchMedia('(prefers-reduced-motion: reduce)').matches) return;
  const { animate, inView, scroll, stagger, press } = M;
  document.documentElement.classList.add('has-motion');
  const springy = { type: 'spring', stiffness: 120, damping: 20, mass: 0.9 };

  // Reveals: each group springs up out of a soft blur, one after another.
  const groups = new Map();
  document.querySelectorAll('.reveal').forEach((el) => {
    const list = groups.get(el.parentElement) ?? [];
    list.push(el);
    groups.set(el.parentElement, list);
  });
  for (const list of groups.values()) {
    list.forEach((el) => { el.style.opacity = '0'; });
    inView(list[0].parentElement, () => {
      list.forEach((el, i) => {
        const rise = el.hasAttribute('data-drift') ? {} : { y: [36, 0] };
        animate(el, { opacity: [0, 1], filter: ['blur(8px)', 'blur(0px)'], ...rise }, { ...springy, delay: i * 0.09 });
      });
    }, { margin: '0px 0px -10% 0px' });
  }

  // Headings rise word by word. Split at run time, so the HTML (and its translations) stays plain.
  document.querySelectorAll('main h2').forEach((h) => {
    if (h.children.length) return;
    const words = h.textContent.trim().split(/\s+/);
    h.setAttribute('aria-label', h.textContent.trim());
    h.textContent = '';
    words.forEach((w, i) => {
      const outer = document.createElement('span');
      outer.className = 'word';
      outer.setAttribute('aria-hidden', 'true');
      const inner = document.createElement('span');
      inner.textContent = w;
      outer.append(inner);
      h.append(outer, i < words.length - 1 ? ' ' : '');
    });
    const inner = h.querySelectorAll('.word > span');
    inner.forEach((s) => { s.style.transform = 'translateY(110%)'; });
    inView(h, () => { animate(inner, { y: ['110%', '0%'] }, { ...springy, delay: stagger(0.06) }); }, { amount: 0.6 });
  });

  // A thin progress line under the top edge.
  const bar = document.createElement('div');
  bar.className = 'progress';
  bar.setAttribute('aria-hidden', 'true');
  document.body.append(bar);
  scroll(animate(bar, { scaleX: [0, 1] }, { ease: 'linear' }));

  // The hero lifts and fades as it scrolls away.
  const hero = document.querySelector('.hero .wrap, .promo-hero .wrap');
  if (hero) {
    scroll(animate(hero, { y: [0, -90], opacity: [1, 0.15] }, { ease: 'linear' }), {
      target: hero.parentElement, offset: ['start start', 'end start'],
    });
  }

  // Buttons and links press with a spring.
  press('.cta a, .jump a, .replay, .mz-press', (el) => {
    animate(el, { scale: 0.96 }, { type: 'spring', stiffness: 500, damping: 26 });
    return () => animate(el, { scale: 1 }, { type: 'spring', stiffness: 500, damping: 18 });
  });

  // Cards tilt toward the pointer and carry a soft light under it.
  if (matchMedia('(pointer: fine)').matches) {
    document.querySelectorAll('.card, .specs div, details').forEach((card) => {
      card.classList.add('lit');
      card.addEventListener('pointermove', (e) => {
        const r = card.getBoundingClientRect();
        const x = (e.clientX - r.left) / r.width;
        const y = (e.clientY - r.top) / r.height;
        card.style.setProperty('--mx', `${x * 100}%`);
        card.style.setProperty('--my', `${y * 100}%`);
        if (card.classList.contains('card')) {
          animate(card, { rotateX: (0.5 - y) * 6, rotateY: (x - 0.5) * 8 }, { type: 'spring', stiffness: 220, damping: 22 });
        }
      });
      card.addEventListener('pointerleave', () => {
        if (card.classList.contains('card')) animate(card, { rotateX: 0, rotateY: 0 }, { type: 'spring', stiffness: 160, damping: 18 });
      });
    });
  }

  // The hero phone leans toward the pointer.
  const heroPhone = document.getElementById('hero-phone');
  if (heroPhone && matchMedia('(pointer: fine)').matches) {
    addEventListener('pointermove', (e) => {
      const x = e.clientX / innerWidth - 0.5;
      const y = e.clientY / innerHeight - 0.5;
      animate(heroPhone, { rotateY: x * 16, rotateX: -y * 10 }, { type: 'spring', stiffness: 120, damping: 20 });
    }, { passive: true });
  }

  // Numbers count up once, when they come into view.
  document.querySelectorAll('[data-count]').forEach((el) => {
    const end = Number(el.dataset.count);
    const suffix = el.dataset.suffix ?? '';
    inView(el, () => {
      animate(0, end, { duration: 1.6, ease: [0.16, 1, 0.3, 1], onUpdate: (v) => { el.textContent = `${Math.round(v)}${suffix}`; } });
    }, { amount: 0.8 });
  });

  // Anything marked data-drift moves a little slower than the scroll.
  document.querySelectorAll('[data-drift]').forEach((el) => {
    const k = Number(el.dataset.drift) || 0.08;
    scroll(animate(el, { y: [120 * k * 4, -120 * k * 4] }, { ease: 'linear' }), { target: el, offset: ['start end', 'end start'] });
  });
})();
