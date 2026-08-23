// Shader loading that mirrors `render/scene/GlUtil.kt` exactly, plus a
// declaration scanner used to audit the uniform set.
//
// The include registry is PARSED OUT OF GlUtil.kt rather than duplicated
// here. A hardcoded list is a second source of truth that drifts silently:
// someone adds `lib_foo` to the app, the preview keeps compiling every shader
// that does not use it, and the one shader that does fails here with a
// message about the tool rather than about the app.

import fs from 'node:fs';
import path from 'node:path';

/** The same anchored pattern GlUtil uses. One level, no recursion. */
const INCLUDE_PATTERN = /^[ \t]*\/\/#include[ \t]+(\w+)[ \t]*$/gm;

/**
 * Reads `GlUtil.INCLUDES` out of the Kotlin source.
 *
 * Returns the set of include names the app accepts. Throws if the map cannot
 * be located at all, because silently returning an empty registry would turn
 * every include in the tree into a fabricated "unknown include" error.
 */
export function parseIncludeRegistry(glUtilPath) {
  const src = fs.readFileSync(glUtilPath, 'utf8');
  const m = src.match(/INCLUDES\s*:\s*Map<String,\s*Int>\s*=\s*mapOf\(([\s\S]*?)\)\s*\n/);
  if (!m) throw new Error(`could not find GlUtil.INCLUDES in ${glUtilPath}`);
  const names = new Set();
  const entry = /"(\w+)"\s+to\s+R\.raw\.(\w+)/g;
  let e;
  while ((e = entry.exec(m[1])) !== null) {
    if (e[1] !== e[2]) {
      throw new Error(
        `GlUtil.INCLUDES maps "${e[1]}" to R.raw.${e[2]}; this tool assumes include name == raw resource name`,
      );
    }
    names.add(e[1]);
  }
  if (names.size === 0) throw new Error('GlUtil.INCLUDES parsed as empty');
  return names;
}

/**
 * Raw resource roots may be several: the app merges `res/raw` from :app and
 * from :engine:scenes into one namespace at build time, so a shader in either
 * (and an include crossing between them) is one flat name to the app. This
 * mirrors that merge the only way a file loader can - by searching the roots
 * in order.
 */
export function shaderFile(rawDirs, resourceName) {
  const dirs = Array.isArray(rawDirs) ? rawDirs : [rawDirs];
  for (const d of dirs) {
    const f = path.join(d, `${resourceName}.glsl`);
    if (fs.existsSync(f)) return f;
  }
  throw new Error(`no such shader: ${resourceName}.glsl (searched ${dirs.join(', ')})`);
}

/** Also mirrors GlUtil: unknown include is a hard error, not an empty expansion. */
export function resolveIncludes(source, registry, rawDirs) {
  return source.replace(INCLUDE_PATTERN, (_all, name) => {
    if (!registry.has(name)) throw new Error(`unknown shader include '${name}'`);
    return fs.readFileSync(shaderFile(rawDirs, name), 'utf8');
  });
}

export function loadShader(rawDirs, resourceName, registry) {
  return resolveIncludes(fs.readFileSync(shaderFile(rawDirs, resourceName), 'utf8'), registry, rawDirs);
}

/** Strips comments so a commented-out `uniform` is not read as a declaration. */
function stripComments(src) {
  return src.replace(/\/\*[\s\S]*?\*\//g, ' ').replace(/\/\/[^\n]*/g, '');
}

function parseDefines(src) {
  const out = new Map();
  const re = /^\s*#define\s+(\w+)\s+(-?\d+)\s*$/gm;
  let m;
  while ((m = re.exec(src)) !== null) out.set(m[1], Number(m[2]));
  return out;
}

/**
 * Every `uniform` a shader declares, with its type and (resolved) array size.
 *
 * The array length may be a `#define`, which is exactly the case for
 * array uniforms sized by a #define, so the defines are resolved first.
 */
export function parseUniforms(source) {
  const src = stripComments(source);
  const defines = parseDefines(source);
  const re = /\buniform\s+(?:(?:highp|mediump|lowp)\s+)?(\w+)\s+(\w+)\s*(?:\[\s*(\w+)\s*\])?\s*;/g;
  const out = [];
  let m;
  while ((m = re.exec(src)) !== null) {
    const [, type, name, rawLen] = m;
    let length = 1;
    if (rawLen !== undefined) {
      length = /^\d+$/.test(rawLen) ? Number(rawLen) : defines.get(rawLen);
      if (length === undefined) {
        throw new Error(`uniform ${name}[${rawLen}]: array length is not a resolvable #define`);
      }
    }
    out.push({ name, type, length, isArray: rawLen !== undefined });
  }
  return out;
}
