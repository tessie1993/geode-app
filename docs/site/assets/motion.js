// Page motion shared by every page: reveals arrive one after another, and anything marked
// data-drift moves a little slower than the scroll. Nothing runs under prefers-reduced-motion.
if (!matchMedia('(prefers-reduced-motion: reduce)').matches) {
  const groups = new Map();
  document.querySelectorAll('.reveal').forEach((el) => {
    const n = groups.get(el.parentElement) ?? 0;
    groups.set(el.parentElement, n + 1);
    el.style.transitionDelay = `${Math.min(n, 5) * 90}ms`;
  });

  const drifting = [...document.querySelectorAll('[data-drift]')];
  let queued = false;
  const drift = () => {
    queued = false;
    const mid = innerHeight / 2;
    for (const el of drifting) {
      const r = el.getBoundingClientRect();
      if (r.bottom < -200 || r.top > innerHeight + 200) continue;
      const k = Number(el.dataset.drift) || 0.08;
      el.style.translate = `0 ${((r.top + r.height / 2 - mid) * -k).toFixed(1)}px`;
    }
  };
  if (drifting.length) {
    addEventListener('scroll', () => { if (!queued) { queued = true; requestAnimationFrame(drift); } }, { passive: true });
    drift();
  }
}
