// The site menu: glass once the page scrolls, a menu button on phones, and the link for the
// section on screen lit up. Works with reduced motion too: nothing here animates.
(() => {
  const nav = document.querySelector('.nav');
  if (!nav) return;
  const toggle = nav.querySelector('.nav-toggle');
  const icon = toggle?.querySelector('use');
  const setOpen = (open) => {
    nav.classList.toggle('open', open);
    toggle?.setAttribute('aria-expanded', String(open));
    icon?.setAttribute('href', icon.getAttribute('href').replace(/#\w+$/, open ? '#close' : '#menu'));
  };
  toggle?.addEventListener('click', () => setOpen(!nav.classList.contains('open')));
  nav.querySelectorAll('.nav-links a').forEach((a) => a.addEventListener('click', () => setOpen(false)));
  addEventListener('keydown', (e) => { if (e.key === 'Escape') setOpen(false); });

  const onScroll = () => nav.classList.toggle('scrolled', scrollY > 12);
  addEventListener('scroll', onScroll, { passive: true });
  onScroll();

  const links = [...nav.querySelectorAll('.nav-links a[href^="#"]')];
  const byId = new Map(links.map((a) => [a.getAttribute('href').slice(1), a]));
  const spy = new IntersectionObserver((entries) => {
    for (const e of entries) {
      if (!e.isIntersecting) continue;
      links.forEach((a) => a.classList.remove('active'));
      byId.get(e.target.id)?.classList.add('active');
    }
  }, { rootMargin: '-45% 0px -50% 0px' });
  byId.forEach((_, id) => { const el = document.getElementById(id); if (el) spy.observe(el); });
})();
