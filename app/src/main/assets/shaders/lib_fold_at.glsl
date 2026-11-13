// The Fold family's read of the style's map at a world point, bent and grown: what FOLD_AT (lib_fold)
// calls. Its own file, included at the top of each Fold march, because it calls foldMap, which the
// style defines after lib_fold and before the march.
//
// WHY a loop: the map was pasted three times at every read (base, Spawn's copy, Fractal's child) and
// the marches read it at six places, so a Fold style compiled eighteen copies of its fractal, past the
// GPU's compile budget (CI's ES 3.00 check lost its WebGL context). One map call in a loop the compiler
// cannot unroll (it starts at a uniform) compiles the fractal once per read; Spawn's and Fractal's
// passes run only while each is on, as before. Life's three births (lib_grow) are passes 3 to 5, each
// only while it lives, and the nearest is kept after foldGrow, so at Life 0 the read is the old one.
//
// INCLUDE ORDER: after lib_fold and after the style's foldMap.

vec4 foldAt(vec3 p) {
    vec3 q = foldQ(p);
    vec4 m = vec4(1.0e9);
    vec4 spawn = vec4(1.0e9);
    vec4 child = vec4(1.0e9);
    vec4 born = vec4(1.0e9);
    for (int i = min(int(uSteps), 0); i < 6; i++) {
        if ((i == 1 && !(uSpawn > 0.0)) || (i == 2 && !(uFractal > 0.0)) || (i > 2 && !growBorn(i - 3))) continue;
        vec4 r = foldMap(i == 0 ? q : (i == 1 ? q + growSpawnShift(vec3(0.0, 0.0, 1.0), 1.0) : (i == 2 ? growChildSpace(q, 3.0, 2.0) : growBirthAt(i - 3, p))));
        if (i == 0) m = r;
        else if (i == 1) spawn = r;
        else if (i == 2) child = r;
        else if (growBirthDist(r.x) < born.x) born = vec4(growBirthDist(r.x), r.y, r.zw * gGrowBirthS);
    }
    m = foldGrow(p, m, spawn, child);
    if (born.x < m.x) m = born;
    return m;
}
