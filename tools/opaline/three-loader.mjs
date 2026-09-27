const three = new URL('../../app/src/main/assets/opaline/vendor/three/build/three.module.js', import.meta.url).href;
export function resolve(specifier, context, nextResolve) {
  if (specifier === 'three') return {url: three, shortCircuit: true};
  return nextResolve(specifier, context);
}
