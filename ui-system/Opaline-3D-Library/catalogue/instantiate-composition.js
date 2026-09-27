import * as THREE from 'three';
import {createElement} from '../src/geometry.js';
import {cloneMaterial, updateMaterials} from '../src/materials.js';
import {MotionController} from '../src/motion.js';

/** Instantiate a catalogue recipe without the workbench UI.
 * Content anchors stay empty. Semantic behaviorBindings are returned as data.
 * Call dispose() once; caller-supplied shared materials are never disposed.
 */
export function instantiateComposition(recipe, materials, {
  bindMotion = false,
  independentMaterials = true,
  onChange = () => {}
} = {}) {
  if (!recipe?.id || !Array.isArray(recipe.parts)) throw new TypeError('A valid composition recipe is required.');
  if (!materials?.gel) throw new TypeError('Pass a material set from createMaterials().');
  const root = new THREE.Group();
  root.name = `${recipe.id} ${recipe.name}`;
  root.userData.compositionId = recipe.id;
  root.userData.textFree = true;
  const parts = new Map(), contentFrames = new Map(), controllers = new Map();
  const ownedMaterials = new Set();
  let disposed = false;

  for (const spec of recipe.parts) {
    if (parts.has(spec.id)) throw new RangeError(`Duplicate part ID: ${spec.id}`);
    const local = {};
    for (const [key, material] of Object.entries(materials)) {
      local[key] = independentMaterials ? cloneMaterial(material) : material;
      if (independentMaterials) ownedMaterials.add(local[key]);
    }
    if (spec.material) {
      if (!local[spec.material]) throw new RangeError(`Unknown material selector: ${spec.material}`);
      // A selector replaces the main body role, preserving independent water,
      // clear shell, film, support and emission roles inside a composite part.
      local.gel = local[spec.material];
    }
    const element = createElement(spec.element, local, {dimensions: spec.dimensions});
    element.position.fromArray(spec.position || [0, 0, 0]);
    element.rotation.fromArray([...(spec.rotation || [0, 0, 0]), 'XYZ']);
    if (Array.isArray(spec.scale)) element.scale.multiply(new THREE.Vector3(...spec.scale));
    else if (Number.isFinite(spec.scale)) element.scale.multiplyScalar(spec.scale);
    element.userData.partId = spec.id;
    element.userData.bindings = structuredClone(spec.bindings || []);
    root.add(element);
    parts.set(spec.id, element);
    if (bindMotion) {
      const originalGeometry = new Set();
      element.traverse(object => {if (object.geometry) originalGeometry.add(object.geometry);});
      const controller = new MotionController(element, {
        id: spec.element,
        onChange(value) {
          const event = {type: 'change', compositionId: recipe.id, partId: spec.id, value};
          root.dispatchEvent(event);
          onChange(event);
        }
      });
      controllers.set(spec.id, controller);
      element.userData.motionController = controller;
      element.traverse(object => {if (object.geometry) originalGeometry.delete(object.geometry);});
      for (const geometry of originalGeometry) geometry.dispose();
    }
  }
  root.updateMatrixWorld(true);
  for (const spec of recipe.contentFrames || []) {
    const owner = parts.get(spec.owner);
    if (!owner) throw new RangeError(`Unknown content owner: ${spec.owner}`);
    if (contentFrames.has(spec.id)) throw new RangeError(`Duplicate content frame: ${spec.id}`);
    const anchor = new THREE.Object3D();
    anchor.name = `content:${spec.id}`;
    anchor.position.fromArray(spec.position || [0, 0, 0]);
    anchor.rotation.fromArray([...(spec.rotation || [0, 0, 0]), 'XYZ']);
    anchor.userData = {...structuredClone(spec), renderedContent: null, emptyContentAnchor: true};
    root.add(anchor);
    root.updateMatrixWorld(true);
    owner.attach(anchor); // Preserve composition-local placement under its owner.
    contentFrames.set(spec.id, anchor);
  }

  return {
    root, parts, contentFrames, controllers,
    behaviorBindings: structuredClone(recipe.behaviorBindings || []),
    update(dt, time) {
      if (disposed) return;
      for (const controller of controllers.values()) controller.update(dt, time);
      if (independentMaterials) updateMaterials([...ownedMaterials], time);
    },
    dispose() {
      if (disposed) return;
      disposed = true;
      const geometry = new Set();
      root.traverse(object => {if (object.geometry) geometry.add(object.geometry);});
      for (const controller of controllers.values()) {
        for (const item of controller.deform || []) geometry.delete(item.geometry);
        controller.dispose();
      }
      for (const item of geometry) item.dispose();
      for (const material of ownedMaterials) material.dispose();
      root.removeFromParent();
      root.clear();
      parts.clear(); contentFrames.clear(); controllers.clear(); ownedMaterials.clear();
    }
  };
}
