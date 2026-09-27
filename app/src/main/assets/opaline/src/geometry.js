import * as THREE from 'three';
import { MarchingCubes } from '../vendor/three/examples/jsm/objects/MarchingCubes.js';

// Physical authoring geometry: +Z control face; Y-up environment.
// Fluid/film objects are initial solver domains, not baked fluid simulations.
const CATALOGUE = {"A01":{"name":"Gel pebble","geometry":"Asymmetric dome with flattened mounting patch","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","lightReceiver","eventOut"]},"A02":{"name":"River oval","geometry":"Long low oval with unequal end radii","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","lightReceiver","eventOut"]},"A03":{"name":"Round lens body","geometry":"Closed biconvex solid with a dense centre","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","lightReceiver","eventOut"]},"A04":{"name":"Cushion square","geometry":"Rounded square with a softly bulging front","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","lightReceiver","eventOut"]},"A05":{"name":"Capsule body","geometry":"Straight centre with rounded volumetric ends","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","lightReceiver","eventOut"]},"A06":{"name":"Tall capsule","geometry":"Vertically oriented closed body with lower mounting patch","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","lightReceiver","eventOut"]},"A07":{"name":"Lozenge body","geometry":"Rounded diamond with softly tucked ends","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","lightReceiver","eventOut"]},"A08":{"name":"Seed body","geometry":"Tapered asymmetric ovoid with a thick root","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","lightReceiver","eventOut"]},"A09":{"name":"Droplet body","geometry":"Closed teardrop with a rounded narrow head","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","lightReceiver","eventOut"]},"A10":{"name":"Petal body","geometry":"Broad convex lobe tapering toward one mount","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","lightReceiver","eventOut"]},"A11":{"name":"Rounded triangle","geometry":"Three broad rounded shoulders around a dome","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","lightReceiver","eventOut"]},"A12":{"name":"Saddle body","geometry":"Closed concave crown over a substantial base","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","lightReceiver","eventOut"]},"A13":{"name":"Twin-lobe body","geometry":"Two domes joined by a thick waist","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","lightReceiver","eventOut"]},"A14":{"name":"Trilobe body","geometry":"Three connected domes with a central mounting region","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","lightReceiver","eventOut"]},"A15":{"name":"Annular body","geometry":"Thick closed torus with a soft elliptical section","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","lightReceiver","eventOut"]},"A16":{"name":"Crescent body","geometry":"Curved closed segment with blunt tapered ends","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","lightReceiver","eventOut"]},"A17":{"name":"Arch body","geometry":"Two mounted feet joined by a thick gel bridge","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","lightReceiver","eventOut"]},"A18":{"name":"Rolled-edge body","geometry":"Closed slab with raised rounded perimeter","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","lightReceiver","eventOut"]},"A19":{"name":"Pillow strip","geometry":"Long closed cushion with alternating broad shoulders","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","lightReceiver","eventOut"]},"A20":{"name":"Convex wedge","geometry":"Soft sloping front with a broad high shoulder","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","lightReceiver","eventOut"]},"A21":{"name":"Nested dome","geometry":"Independent small dome mounted in a larger soft bed","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","lightReceiver","eventOut"]},"A22":{"name":"Soft puck","geometry":"Short cylinder with a domed cap and thick rounded sidewall","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","lightReceiver","eventOut"]},"A23":{"name":"Split action body","geometry":"Two separate pebbles fitted into a common soft cradle","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","lightReceiver","eventOut"]},"A24":{"name":"Concave grip body","geometry":"Thick closed body with a finger-sized shallow depression","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","lightReceiver","eventOut"]},"B01":{"name":"Elastic straight slider","geometry":"Thick straight guide with gel thumb and flexible skirt","connections":["mount","contentFrame","touchRegion","collider","fluidIn","fluidOut","eventOut"]},"B02":{"name":"Curved slider","geometry":"Three-dimensional arc guide with a rounded travelling thumb","connections":["mount","contentFrame","touchRegion","collider","fluidIn","fluidOut","eventOut"]},"B03":{"name":"Vertical fader","geometry":"Upright guide with a substantial collar thumb","connections":["mount","contentFrame","touchRegion","collider","fluidIn","fluidOut","eventOut"]},"B04":{"name":"Dual-range slider","geometry":"Two independent thumbs sharing one channel","connections":["mount","contentFrame","touchRegion","collider","fluidIn","fluidOut","eventOut"]},"B05":{"name":"Stepped slider","geometry":"Guide with real mechanical detent wells","connections":["mount","contentFrame","touchRegion","collider","fluidIn","fluidOut","eventOut"]},"B06":{"name":"Ribbon slider","geometry":"Anchored flexible sheet beside a constrained thumb","connections":["mount","contentFrame","touchRegion","collider","fluidIn","fluidOut","eventOut"]},"B07":{"name":"Meniscus slider","geometry":"Stable liquid channel with a moving displacement body","connections":["mount","contentFrame","touchRegion","collider","fluidIn","fluidOut","eventOut"]},"B08":{"name":"Floating slider","geometry":"Thumb suspended above a guide by a soft constraint","connections":["mount","contentFrame","touchRegion","collider","fluidIn","fluidOut","eventOut"]},"B09":{"name":"Capsule toggle","geometry":"Two-stop cradle with a thick travelling bead","connections":["mount","contentFrame","touchRegion","collider","fluidIn","fluidOut","eventOut"]},"B10":{"name":"Twin-well toggle","geometry":"Two recessed wells connected by a narrow channel","connections":["mount","contentFrame","touchRegion","collider","fluidIn","fluidOut","eventOut"]},"B11":{"name":"Rocker","geometry":"Pivoted thick rounded plate in a soft bed","connections":["mount","contentFrame","touchRegion","collider","fluidIn","fluidOut","eventOut"]},"B12":{"name":"Three-position selector","geometry":"Three detents with a volumetric travelling cap","connections":["mount","contentFrame","touchRegion","collider","fluidIn","fluidOut","eventOut"]},"B13":{"name":"Domed rotary dial","geometry":"Domed cap above a separate collar and axis","connections":["mount","contentFrame","touchRegion","collider","fluidIn","fluidOut","eventOut"]},"B14":{"name":"Ribbed rotary dial","geometry":"Rounded cap with genuine sidewall ribs","connections":["mount","contentFrame","touchRegion","collider","fluidIn","fluidOut","eventOut"]},"B15":{"name":"Concentric dual dial","geometry":"Two coaxial independently constrained rings","connections":["mount","contentFrame","touchRegion","collider","fluidIn","fluidOut","eventOut"]},"B16":{"name":"Arc dial","geometry":"Open thick arc with an orbiting grip","connections":["mount","contentFrame","touchRegion","collider","fluidIn","fluidOut","eventOut"]},"B17":{"name":"Ring selector","geometry":"Closed annulus with a captive orbiting bead","connections":["mount","contentFrame","touchRegion","collider","fluidIn","fluidOut","eventOut"]},"B18":{"name":"Trackball","geometry":"Exposed soft sphere in a supporting cup","connections":["mount","contentFrame","touchRegion","collider","fluidIn","fluidOut","eventOut"]},"B19":{"name":"Soft joystick","geometry":"Stem and crown with elastic root and physical travel cone","connections":["mount","contentFrame","touchRegion","collider","fluidIn","fluidOut","eventOut"]},"B20":{"name":"Pressure pad","geometry":"Thick pad with bounded contact surface","connections":["mount","contentFrame","touchRegion","collider","fluidIn","fluidOut","eventOut"]},"B21":{"name":"XY saddle control","geometry":"Concave two-axis surface with a movable puck","connections":["mount","contentFrame","touchRegion","collider","fluidIn","fluidOut","eventOut"]},"B22":{"name":"Scroll wheel","geometry":"Thick cylindrical wheel held between two supports","connections":["mount","contentFrame","touchRegion","collider","fluidIn","fluidOut","eventOut"]},"B23":{"name":"Grip handle","geometry":"Curved closed grip attached at two ends","connections":["mount","contentFrame","touchRegion","collider","fluidIn","fluidOut","eventOut"]},"B24":{"name":"Iris control","geometry":"Overlapping thick curved leaves on radial guides","connections":["mount","contentFrame","touchRegion","collider","fluidIn","fluidOut","eventOut"]},"C01":{"name":"Compact slab","geometry":"Thick rounded rectangular volume with a quiet front","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","fluidIn","fluidOut","lightReceiver"]},"C02":{"name":"Wide slab","geometry":"Broad low panel with substantial rounded thickness","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","fluidIn","fluidOut","lightReceiver"]},"C03":{"name":"Portrait slab","geometry":"Tall panel with top and bottom mounting options","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","fluidIn","fluidOut","lightReceiver"]},"C04":{"name":"Banner strip","geometry":"Long shallow rounded slab with restrained interior colour","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","fluidIn","fluidOut","lightReceiver"]},"C05":{"name":"Speech shell left","geometry":"Rounded volume with a curved left attachment tail","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","fluidIn","fluidOut","lightReceiver"]},"C06":{"name":"Speech shell right","geometry":"Rounded volume with a right-side curved attachment","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","fluidIn","fluidOut","lightReceiver"]},"C07":{"name":"Empty toast body","geometry":"Compact hovering slab with an underside light recess","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","fluidIn","fluidOut","lightReceiver"]},"C08":{"name":"Shallow round tray","geometry":"Closed bowl wall with broad floor and rounded lip","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","fluidIn","fluidOut","lightReceiver"]},"C09":{"name":"Oval tray","geometry":"Elongated bowl with unequal curvature at ends","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","fluidIn","fluidOut","lightReceiver"]},"C10":{"name":"Rectangular tray","geometry":"Rounded basin with a flat central floor","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","fluidIn","fluidOut","lightReceiver"]},"C11":{"name":"Deep reservoir","geometry":"Thick transparent shell enclosing a substantial cavity","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","fluidIn","fluidOut","lightReceiver"]},"C12":{"name":"Open cup","geometry":"Small thick-walled cup with an exposed meniscus","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","fluidIn","fluidOut","lightReceiver"]},"C13":{"name":"Inset bed","geometry":"Soft perimeter around a stable recessed mounting patch","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","fluidIn","fluidOut","lightReceiver"]},"C14":{"name":"Floating shelf","geometry":"Substantial cantilevered rounded platform","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","fluidIn","fluidOut","lightReceiver"]},"C15":{"name":"Stacked slabs","geometry":"Three independent panels with declared spacer contacts","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","fluidIn","fluidOut","lightReceiver"]},"C16":{"name":"Twin hinged panel","geometry":"Two thick panels joined by an elastic hinge","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","fluidIn","fluidOut","lightReceiver"]},"C17":{"name":"Flexible sheet panel","geometry":"Finite-thickness sheet with reinforced perimeter","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","fluidIn","fluidOut","lightReceiver"]},"C18":{"name":"Curled corner panel","geometry":"Panel with one physically curled free corner","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","fluidIn","fluidOut","lightReceiver"]},"C19":{"name":"Rounded frame","geometry":"Closed thick perimeter with a genuine empty centre","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","fluidIn","fluidOut","lightReceiver"]},"C20":{"name":"Circular frame","geometry":"Thick annular frame with an inner attachment ring","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","fluidIn","fluidOut","lightReceiver"]},"C21":{"name":"Arch frame","geometry":"Two feet and a curved upper border enclosing open space","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","fluidIn","fluidOut","lightReceiver"]},"C22":{"name":"Nested well","geometry":"Stepped recess with separately modelled inner and outer lips","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","fluidIn","fluidOut","lightReceiver"]},"C23":{"name":"Expandable reservoir","geometry":"Pleated thick shell with a controlled variable cavity","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","fluidIn","fluidOut","lightReceiver"]},"C24":{"name":"Edge gutter","geometry":"Curved channel mounted along another component's perimeter","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","fluidIn","fluidOut","lightReceiver"]},"D01":{"name":"Water-mounted dock","geometry":"Common buoyant support with independent child mounts","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","eventOut"]},"D02":{"name":"Straight modular dock","geometry":"Rail with repeated sockets and removable bodies","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","eventOut"]},"D03":{"name":"Crescent dock","geometry":"Curved platform whose sockets face a common viewing region","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","eventOut"]},"D04":{"name":"Stepped dock","geometry":"Three-dimensional tiered support with separate ledges","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","eventOut"]},"D05":{"name":"Ring dock","geometry":"Annular support with radial sockets","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","eventOut"]},"D06":{"name":"Branch dock","geometry":"Organic branching support with terminal sockets","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","eventOut"]},"D07":{"name":"Suspended rail","geometry":"Tensioned curved support held at two mounts","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","eventOut"]},"D08":{"name":"Orbit carrier","geometry":"Constraint system carrying bodies around a shared centre","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","eventOut"]},"D09":{"name":"Radial hub","geometry":"Central body with replaceable spoke connections","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","eventOut"]},"D10":{"name":"Bead path","geometry":"Discrete mounted beads along a 3D curve","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","eventOut"]},"D11":{"name":"Stepping path","geometry":"Sequence of separate water-contact platforms","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","eventOut"]},"D12":{"name":"Flexible connector","geometry":"Solid gel neck between two typed mounts","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","eventOut"]},"D13":{"name":"Fluid connector","geometry":"Hollow channel with independent wall and fluid ports","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","eventOut"]},"D14":{"name":"Light conduit","geometry":"Transmissive curved guide with a volumetric light core","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","eventOut"]},"D15":{"name":"Magnetic mount","geometry":"Paired mating surfaces with a declared attraction zone","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","eventOut"]},"D16":{"name":"Hinge mount","geometry":"Finite-thickness hinge and declared rotational axis","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","eventOut"]},"D17":{"name":"Ball socket","geometry":"Rounded joint with a real retaining cavity","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","eventOut"]},"D18":{"name":"Constellation assembly","geometry":"Sparse graph of floating bodies and optional connectors","connections":["mount","contentFrame","touchRegion","collider","wetBoundary","eventOut"]},"E01":{"name":"Free liquid surface","geometry":"Bounded 3D volume with an exposed upper interface and receiver bed","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver","eventOut"]},"E02":{"name":"Deep liquid basin","geometry":"Water volume around submerged obstacles and sloped walls","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver","eventOut"]},"E03":{"name":"Thin liquid pool","geometry":"Shallow but finite volume with a moving contact line","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver","eventOut"]},"E04":{"name":"Liquid mound","geometry":"Local raised volume continuous with a parent pool","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver","eventOut"]},"E05":{"name":"Impact crown","geometry":"Free-surface splash around an incoming collision","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver","eventOut"]},"E06":{"name":"Rolling crest","geometry":"Advancing wave with a curling overhang","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver","eventOut"]},"E07":{"name":"Liquid arch","geometry":"Two rooted streams joining over open space","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver","eventOut"]},"E08":{"name":"Standing droplet","geometry":"Compact liquid body on a wettable support","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver","eventOut"]},"E09":{"name":"Hanging droplet","geometry":"Liquid attached to an underside outlet","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver","eventOut"]},"E10":{"name":"Falling droplet","geometry":"Detached closed volume with inherited velocity","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver","eventOut"]},"E11":{"name":"Capillary bridge","geometry":"Liquid neck spanning two moving wettable boundaries","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver","eventOut"]},"E12":{"name":"Contained pigment channel","geometry":"Clear shell with carrier fluid and a separate coloured inflow","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver","eventOut"]},"E13":{"name":"Curved fluid channel","geometry":"Hollow three-dimensional conduit with a bent centreline","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver","eventOut"]},"E14":{"name":"Annular fluid channel","geometry":"Closed ring cavity with separately driven circulation","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver","eventOut"]},"E15":{"name":"Fluid junction","geometry":"Branching cavity with multiple declared inlets and outlets","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver","eventOut"]},"E16":{"name":"Liquid curtain","geometry":"Continuous falling sheet of finite thickness","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver","eventOut"]},"E17":{"name":"Viscous ribbon","geometry":"Thick coloured strand with varying cross-section","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver","eventOut"]},"E18":{"name":"Folded lamina","geometry":"Thin coloured sheet inside a clear carrier","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver","eventOut"]},"E19":{"name":"Viscous bulb","geometry":"Rounded high-viscosity volume with a narrow attachment","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver","eventOut"]},"E20":{"name":"Liquid strand network","geometry":"Connected branching streams with tracked junctions","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver","eventOut"]},"E21":{"name":"Whirlpool volume","geometry":"Rotating fluid domain with a depressed free surface","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver","eventOut"]},"E22":{"name":"Liquid tongue","geometry":"Local extrusion from a parent edge toward a target","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver","eventOut"]},"E23":{"name":"Upward meniscus transfer","geometry":"Locally attracted liquid peak and target wet surface","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver","eventOut"]},"E24":{"name":"Coalescing drops","geometry":"Two independent fluid volumes in one collision domain","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver","eventOut"]},"F01":{"name":"Isolated film bubble","geometry":"Closed thin film enclosing a gas volume","connections":["collider","fieldRegion","lightReceiver","eventOut"]},"F02":{"name":"Elongated film bubble","geometry":"Asymmetric gas region under a declared constraint","connections":["collider","fieldRegion","lightReceiver","eventOut"]},"F03":{"name":"Bubble contact pair","geometry":"Two gas regions with a shared film interface","connections":["collider","fieldRegion","lightReceiver","eventOut"]},"F04":{"name":"Triple bubble junction","geometry":"Three gas cells sharing a film network","connections":["collider","fieldRegion","lightReceiver","eventOut"]},"F05":{"name":"Bubble cluster","geometry":"Multiple connected gas regions with independent volume targets","connections":["collider","fieldRegion","lightReceiver","eventOut"]},"F06":{"name":"Film bridge","geometry":"Thin film connecting two rings or bubble regions","connections":["collider","fieldRegion","lightReceiver","eventOut"]},"F07":{"name":"Film curtain","geometry":"Finite film with supported edges and a thickness field","connections":["collider","fieldRegion","lightReceiver","eventOut"]},"F08":{"name":"Film drainage","geometry":"Surface flow field on an existing film network","connections":["collider","fieldRegion","lightReceiver","eventOut"]},"F09":{"name":"Film interference transport","geometry":"Optical field derived from evolving film thickness","connections":["collider","fieldRegion","lightReceiver","eventOut"]},"F10":{"name":"Bubble pinch separation","geometry":"Connected lobes with an evolving narrow film neck","connections":["collider","fieldRegion","lightReceiver","eventOut"]},"F11":{"name":"Bubble coalescence","geometry":"Contacting films with explicit wall removal criteria","connections":["collider","fieldRegion","lightReceiver","eventOut"]},"F12":{"name":"Film rupture","geometry":"Film network with a physically defined hole boundary","connections":["collider","fieldRegion","lightReceiver","eventOut"]},"J01":{"name":"Water dock environment","geometry":"Local water basin and buoyant mounting assembly","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver"]},"J02":{"name":"Open lake volume","geometry":"Broad water body with distant shoreline and depth","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver"]},"J03":{"name":"Shallow rock pool","geometry":"Finite water region over a visible mineral bed","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver"]},"J04":{"name":"Curved shore segment","geometry":"Sloped wettable boundary joining land and liquid","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver"]},"J05":{"name":"Pebble shore patch","geometry":"Multiple independent mineral bodies with gaps","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver"]},"J06":{"name":"Single wet stone","geometry":"Irregular closed mineral with a retained water film","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver"]},"J07":{"name":"Stacked stone support","geometry":"Several mineral bodies with real contact regions","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver"]},"J08":{"name":"Mineral pedestal","geometry":"Raised rounded support with a separate mounting patch","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver"]},"J09":{"name":"Stepping island","geometry":"Small land volume partially submerged in water","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver"]},"J10":{"name":"Submerged shelf","geometry":"Underwater receiving surface with changing depth","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver"]},"J11":{"name":"Reflective basin wall","geometry":"Curved wet wall forming part of a fluid boundary","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver"]},"J12":{"name":"Root arch","geometry":"Organic solid arch joining two ground mounts","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver"]},"J13":{"name":"Fern frond","geometry":"Finite-thickness articulated leaflets along an elastic stem","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver"]},"J14":{"name":"Fern cluster","geometry":"Several fronds rooted in one modular patch","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver"]},"J15":{"name":"Moss cushion","geometry":"Dense small-scale botanical volume over a mineral base","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver"]},"J16":{"name":"Reed stem","geometry":"Elastic slender body with a anchored root","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver"]},"J17":{"name":"Floating leaf","geometry":"Buoyant thin organic body on the water surface","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver"]},"J18":{"name":"Dew-bearing leaf","geometry":"Flexible leaf carrying attached droplets","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver"]},"J19":{"name":"Mist bank","geometry":"World-space density volume near the water surface","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver"]},"J20":{"name":"Low fog corridor","geometry":"Elongated atmospheric volume fitted between components","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver"]},"J21":{"name":"Distant canopy","geometry":"Layered far-field vegetation geometry within atmosphere","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver"]},"J22":{"name":"Bioluminescent bed","geometry":"Sparse emitting matter attached to submerged surfaces","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver"]},"J23":{"name":"Waterfall lip","geometry":"Wettable curved edge feeding a falling liquid sheet","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver"]},"J24":{"name":"Nature light rig","geometry":"Coherent environment and area-light arrangement","connections":["mount","collider","wetBoundary","fluidIn","fluidOut","fieldRegion","lightReceiver"]}};

Object.assign(CATALOGUE, {"N01":{"name":"Seed button","geometry":"Asymmetric thick seed body with raised seam and separate dew indicator","connections":["mount","touchRegion","lightReceiver","eventOut"]},"N02":{"name":"Three-compartment leaf tray","geometry":"Curved leaf silhouette with three real recessed cavities, closed floor and flexible stem","connections":["mount","contentFrame","collider","wetBoundary","eventOut"]},"N03":{"name":"Dew lens","geometry":"Closed biconvex water lens in an independent curved meniscus seat","connections":["mount","touchRegion","lightReceiver","wetBoundary"]},"N04":{"name":"Tendril connector","geometry":"Three-dimensional branching tendril with curled side shoot and typed end sockets","connections":["mount","collider","eventOut","lightReceiver"]},"N05":{"name":"Glass fern frond","geometry":"Curving transparent stem with independently articulated finite-thickness glass leaflets","connections":["mount","windReceiver","collider","lightReceiver"]},"N06":{"name":"Luminous spore","geometry":"Hollow curved shell around an isolated luminous core, glass fins and flexible trailing tail","connections":["mount","windReceiver","collider","lightEmitter","eventOut"]},"N07":{"name":"Water-root panel","geometry":"Hollow glass panel enclosing a nonintersecting water volume with flexible roots and terminal dew","connections":["mount","contentFrame","touchRegion","windReceiver","collider","wetBoundary"]}});

const PI = Math.PI;
const V = (x=0,y=0,z=0)=>new THREE.Vector3(x,y,z);
const signPow = (v,p)=>Math.sign(v)*Math.pow(Math.abs(v),p);
const DEFAULTS = {
  gel: {color:0x99cedd,roughness:.22,metalness:0,transmission:.73,thickness:.6,ior:1.41},
  blue: {color:0x448dc1,roughness:.18,transmission:.76,thickness:.45,ior:1.4},
  water: {color:0xe1f8fa,roughness:.06,transmission:.93,thickness:1.2,ior:1.333},
  shell: {color:0xe5f4e9,roughness:.09,transmission:.96,thickness:.12,ior:1.46},
  pigment: {color:0x448dc1,roughness:.3,transmission:.43,thickness:.5,ior:1.34},
  film: {color:0xf1ddef,roughness:.045,transmission:.97,thickness:.002,ior:1.33,iridescence:1,iridescenceIOR:1.33,iridescenceThicknessRange:[120,620]},
  nacre: {color:0xd7d8ba,roughness:.3,metalness:.15,clearcoat:1,iridescence:.6},
  stone: {color:0x526d78,roughness:.55,metalness:0,clearcoat:.45},
  leaf: {color:0x467c66,roughness:.42,metalness:0,clearcoat:.35},
  glow: {color:0x9ceacb,emissive:0x7cd5b5,emissiveIntensity:1.8,roughness:.2}
};
function materialSet(input={}) {
 const cache={};
 for (const k of Object.keys(DEFAULTS)) { cache[k]=input[k] || new THREE.MeshPhysicalMaterial(DEFAULTS[k]); if(!cache[k].name) cache[k].name=k; }
 return cache;
}
function mesh(g,mat,name,role='body') {
 const m=new THREE.Mesh(g,mat); m.name=name; m.castShadow=true; m.receiveShadow=true;
 m.userData={role,interactive:!['stone','environment','mist','light'].includes(role),deformable:['body','thumb','leaf','water','film','hinge','connector','pigment'].includes(role)};
 return m;
}
function add(group,g,mat,name,pos=[0,0,0],rot=[0,0,0],role='body') {
 const m=mesh(g,mat,name,role);m.position.set(...pos);m.rotation.set(...rot);group.add(m);return m;
}
function sphere(rx=1,ry=.6,rz=.35,exponent=1,map=null,segments=64) {
 const geo=new THREE.SphereGeometry(1,segments,Math.max(24,segments/2));
 const p=geo.attributes.position;
 for(let i=0;i<p.count;i++) {
  const u=p.getX(i),v=p.getY(i),w=p.getZ(i);
  let x=signPow(u,exponent)*rx,y=signPow(v,exponent)*ry,z=signPow(w,exponent)*rz;
  if(map) [x,y,z]=map(x,y,z,u,v,w);
  p.setXYZ(i,x,y,z);
 }
 smoothNormals(geo);geo.computeBoundingBox();geo.computeBoundingSphere();return geo;
}
function smoothNormals(geo) {
 geo.computeVertexNormals();
 const p=geo.attributes.position,n=geo.attributes.normal,groups=new Map();
 for(let i=0;i<p.count;i++) {const key=[p.getX(i),p.getY(i),p.getZ(i)].map(v=>Math.round(v*1e6)).join(',');let a=groups.get(key);if(!a){a={normal:V(),indices:[]};groups.set(key,a);}a.normal.add(V(n.getX(i),n.getY(i),n.getZ(i)));a.indices.push(i);}
 for(const a of groups.values()){if(a.normal.lengthSq()<1e-12){const i=a.indices[0];a.normal.set(p.getX(i),p.getY(i),p.getZ(i));}a.normal.normalize();for(const i of a.indices)n.setXYZ(i,a.normal.x,a.normal.y,a.normal.z);}
 n.needsUpdate=true;return geo;
}
function roundedShape(w,h,r) {
 r=Math.min(r,w/2,h/2);const s=new THREE.Shape();
 const l=-w/2,b=-h/2,R=w/2,t=h/2;
 s.moveTo(l+r,b);s.lineTo(R-r,b);s.quadraticCurveTo(R,b,R,b+r);s.lineTo(R,t-r);s.quadraticCurveTo(R,t,R-r,t);
 s.lineTo(l+r,t);s.quadraticCurveTo(l,t,l,t-r);s.lineTo(l,b+r);s.quadraticCurveTo(l,b,l+r,b);return s;
}
function plate(w=2,h=1,t=.3,r=.3,bevel=.1) {
 bevel=Math.min(bevel,t*.42,r*.5);
 const g=new THREE.ExtrudeGeometry(roundedShape(w-2*bevel,h-2*bevel,Math.max(.025,r-bevel)),{depth:t-2*bevel,bevelEnabled:true,bevelSize:bevel,bevelThickness:bevel,bevelSegments:5,steps:2,curveSegments:12});
 g.translate(0,0,-t/2+bevel);g.computeVertexNormals();return g;
}
function ring(rx=1,ry=.7,tube=.16,start=0,arc=2*PI) {
 if(arc<2*PI-1e-6){const points=[];for(let i=0;i<=48;i++){const a=start+arc*i/48;points.push([rx*Math.cos(a),ry*Math.sin(a),0]);}return volumeTube(points,tube);}
 class Ellipse3 extends THREE.Curve {getPoint(t,target=V()){const a=start+2*PI*t;return target.set(rx*Math.cos(a),ry*Math.sin(a),0);}}
 return new THREE.TubeGeometry(new Ellipse3(),96,tube,20,true);
}
function tube(points,r=.1,radial=16,closed=false) {
 // A true hollow conduit, with independent inner and outer surfaces and annular end faces.
 const curve=new THREE.CatmullRomCurve3(points.map(p=>Array.isArray(p)?V(...p):p),closed);
 const section=new THREE.Shape();section.absarc(0,0,r,0,2*PI,false);
 const hole=new THREE.Path();hole.absarc(0,0,Math.max(r*.5,r-.045),0,2*PI,true);section.holes.push(hole);
 return smoothNormals(new THREE.ExtrudeGeometry(section,{steps:72,bevelEnabled:false,extrudePath:curve,curveSegments:Math.max(16,radial)}));
}
function volumeTube(points,r=.1,ends=true) { // closed tube with explicit caps, preserving normals at cap transition
 const curve=new THREE.CatmullRomCurve3(points.map(p=>V(...p)));
 const g=new THREE.TubeGeometry(curve,72,r,20,false);
 if(!ends)return g;
 // TubeGeometry itself has open ends; close them into a single BufferGeometry.
 const a=g.attributes.position, n=g.attributes.normal,uv=g.attributes.uv;
 const positions=Array.from(a.array),normals=Array.from(n.array),uvs=Array.from(uv.array),indices=Array.from(g.index.array);
 for(const end of [0,1]) {
  const center=curve.getPoint(end),tan=curve.getTangent(end).multiplyScalar(end?1:-1),c=positions.length/3;
  positions.push(center.x,center.y,center.z);normals.push(tan.x,tan.y,tan.z);uvs.push(.5,.5);
  const first=positions.length/3,base=(end?72:0)*21;
  for(let j=0;j<=20;j++){positions.push(a.getX(base+j),a.getY(base+j),a.getZ(base+j));normals.push(tan.x,tan.y,tan.z);uvs.push(.5+.5*Math.cos(j/20*2*PI),.5+.5*Math.sin(j/20*2*PI));}
  for(let j=0;j<20;j++)if(end)indices.push(c,first+j,first+j+1);else indices.push(c,first+j+1,first+j);
 }
 const out=new THREE.BufferGeometry();out.setAttribute('position',new THREE.Float32BufferAttribute(positions,3));out.setAttribute('normal',new THREE.Float32BufferAttribute(normals,3));out.setAttribute('uv',new THREE.Float32BufferAttribute(uvs,2));out.setIndex(indices);return out;
}
function bowl(rx=1,ry=1,depth=.55,wall=.13) {
 // Revolved closed cross-section: outer bottom → rim → inner wall → interior floor.
 const points=[V(0,-depth*.5),V(rx*.55,-depth*.5),V(rx*.88,-depth*.42),V(rx,-depth*.05),V(rx,depth*.38),V(rx*.97,depth*.5),V(rx-wall,depth*.5),V(rx-wall*1.1,depth*.34),V(rx-wall*1.12,-depth*.02),V(rx*.72,-depth*.5+wall),V(0,-depth*.5+wall)];
 const g=new THREE.LatheGeometry(points,72);g.scale(1,1,ry/rx);g.rotateX(PI/2);smoothNormals(g);return g;
}
function frame(w,h,t=.25,r=.4,border=.17) {
 const outer=roundedShape(w,h,r),inner=roundedShape(w-border*2,h-border*2,Math.max(.04,r-border));
 outer.holes.push(new THREE.Path(inner.getPoints(32).reverse()));
 const g=new THREE.ExtrudeGeometry(outer,{depth:t*.56,bevelEnabled:true,bevelThickness:t*.22,bevelSize:Math.min(border*.28,.07),bevelSegments:4,steps:1,curveSegments:12});g.translate(0,0,-t*.28);return g;
}
function paramSheet(w=2,h=1,t=.08,fn=(u,v)=>[u*w/2,v*h/2,0],N=40,M=24) {
 // Two skin layers plus sewn edge strips; nonzero thickness even for flexible sheets.
 const positions=[],uv=[],ind=[];
 for(let side=0;side<2;side++)for(let j=0;j<=M;j++)for(let i=0;i<=N;i++){const u=i/N*2-1,v=j/M*2-1;const [x,y,z]=fn(u,v);positions.push(x,y,z+(side?1:-1)*t/2);uv.push(i/N,j/M);}
 const layer=(N+1)*(M+1);
 for(let s=0;s<2;s++)for(let j=0;j<M;j++)for(let i=0;i<N;i++){const a=s*layer+j*(N+1)+i,b=a+1,c=a+N+1,d=c+1;if(s)ind.push(a,b,d,a,d,c);else ind.push(a,d,b,a,c,d);}
 const edge=[];for(let i=0;i<=N;i++)edge.push(i);for(let j=1;j<=M;j++)edge.push(j*(N+1)+N);for(let i=N-1;i>=0;i--)edge.push(M*(N+1)+i);for(let j=M-1;j>0;j--)edge.push(j*(N+1));
 for(let k=0;k<edge.length;k++){const a=edge[k],b=edge[(k+1)%edge.length];ind.push(a,b,a+layer,b,b+layer,a+layer);}
 const g=new THREE.BufferGeometry();g.setAttribute('position',new THREE.Float32BufferAttribute(positions,3));g.setAttribute('uv',new THREE.Float32BufferAttribute(uv,2));g.setIndex(ind);g.computeVertexNormals();return g;
}
function drop(rx=.55,ry=.8,rz=.5,hang=false) {
 return sphere(rx,ry,rz,1,(x,y,z,u,v,w)=>{const f=hang?.65-.32*v:.72-.28*v;return [x*f,y,z*f];});
}
function leafGeometry(length=1.2,width=.45,curl=.2) {
 return paramSheet(width,length,.035,(u,v)=>{const taper=Math.sin((v+1)*PI/2)*(.8+.2*v);return [u*width*taper,v*length/2,curl*(v*v-.3)+.09*u*u-.05*Math.abs(u)];},24,32);
}
function seedRand(n){return ((Math.sin(n*127.1+19.71)*43758.5453123)%1+1)%1;}
function stoneGeometry(rx=.7,ry=.4,rz=.4,seed=1) {
 return sphere(rx,ry,rz,.86,(x,y,z,u,v,w)=>{const f=1+.08*Math.sin(u*7+seed)*Math.sin(v*5+seed*.3)*Math.cos(w*9);return [x*f,y*f,z*f];});
}
function setMotion(m,kind,extra={}){m.userData.motion={kind,...extra};return m;}


// A sealed glass shell is two oriented boundaries. Inner normals point into the cavity.
function hollowVolume(outer,inner) {
 const a=outer.index?outer.toNonIndexed():outer.clone(),b=inner.index?inner.toNonIndexed():inner.clone();
 const out=new THREE.BufferGeometry();
 for(const name of ['position','normal','uv']){
  const aa=a.attributes[name],bb=b.attributes[name],s=aa.itemSize,arr=new Float32Array((aa.count+bb.count)*s);arr.set(aa.array);
  for(let i=0;i<bb.count;i+=3)for(let j=0;j<3;j++)for(let k=0;k<s;k++)arr[(aa.count+i+j)*s+k]=bb.array[(i+2-j)*s+k]*(name==='normal'?-1:1);
  out.setAttribute(name,new THREE.BufferAttribute(arr,s));
 }
 out.userData={closedShell:true,interfaces:'outer-positive-inner-negative'};out.computeBoundingSphere();a.dispose();b.dispose();outer.dispose();inner.dispose();return out;
}
function railShape(value=.5){
 const rings=128,around=48,L=1.36,center=-.93+1.86*value;
 const positions=[],normals=[],uvs=[],indices=[],radii=[];let sum=0;
 for(let i=0;i<=rings;i++){
  const x=-L+2*L*i/rings,cap=Math.sqrt(Math.max(0,1-Math.pow(Math.max(0,(Math.abs(x)-1.11)/.25),2)));
  const head=.172*Math.exp(-Math.pow((x-center)/.245,2)),neck=.046*Math.exp(-Math.pow((x-center+.33)/.135,2));
  const r=(.111+head-neck)*cap;radii.push(r);sum+=r*r;
 }
 const volumeScale=Math.sqrt(3.08/sum);
 for(let i=0;i<=rings;i++)for(let j=0;j<=around;j++){
  const x=-L+2*L*i/rings,a=j/around*2*PI,r=radii[i]*volumeScale;
  positions.push(x,Math.cos(a)*r*1.07,Math.sin(a)*r*.94);uvs.push(i/rings,j/around);
 }
 for(let i=0;i<rings;i++)for(let j=0;j<around;j++){const a=i*(around+1)+j,b=a+around+1;indices.push(a,a+1,b,a+1,b+1,b);}
 const g=new THREE.BufferGeometry();g.setAttribute('position',new THREE.Float32BufferAttribute(positions,3));g.setAttribute('uv',new THREE.Float32BufferAttribute(uvs,2));g.setIndex(indices);smoothNormals(g);return g;
}
function morphingLiquidRail(){
 const geometry=railShape(.5);geometry.morphAttributes.position=[];geometry.morphAttributes.normal=[];
 for(let i=0;i<7;i++){const target=railShape(i/6);geometry.morphAttributes.position.push(target.attributes.position);geometry.morphAttributes.normal.push(target.attributes.normal);}
 geometry.morphTargetsRelative=false;geometry.userData={morphMeaning:'Seven absolute traveling liquid-head positions',values:[0,1/6,2/6,.5,4/6,5/6,1],approximateVolumeConservation:true};return geometry;
}
function implicitVolume(field,extent=1.5,resolution=56){
 const surface=new MarchingCubes(resolution,new THREE.MeshBasicMaterial(),true,false,70000),n=resolution;surface.isolation=80;
 for(let z=0;z<n;z++)for(let y=0;y<n;y++)for(let x=0;x<n;x++)surface.field[x+n*(y+n*z)]=80-field((x/n*2-1)*extent,(y/n*2-1)*extent,(z/n*2-1)*extent)*40;
 surface.update();const count=surface.geometry.drawRange.count,geometry=new THREE.BufferGeometry();
 for(const name of ['position','normal','uv']){const a=surface.geometry.attributes[name];geometry.setAttribute(name,new THREE.BufferAttribute(a.array.slice(0,count*a.itemSize),a.itemSize));}
 geometry.scale(extent,extent,extent);geometry.normalizeNormals();geometry.computeBoundingSphere();surface.geometry.dispose();surface.material.dispose();return geometry;
}
function coalescedDropGeometry(){
 return implicitVolume((x,y,z)=>{const a=Math.hypot(x+.47,y+.045,z+.035)-.64,b=Math.hypot(x-.47,y-.08,z-.03)-.59,k=.26,h=THREE.MathUtils.clamp(.5+.5*(b-a)/k,0,1);return THREE.MathUtils.lerp(b,a,h)-k*h*(1-h);},1.45,60);
}

function gelBody(g,n,m) {
 const body=(geo,name='gel-body',pos,rot)=>add(g,geo,m.gel,name,pos,rot,'body');
 switch(n){
 case 1:body(sphere(.92,.66,.38,.84,(x,y,z,u,v,w)=>[x*(1+.1*v),y,z+.035*u]));break;
 case 2:body(sphere(1.3,.48,.34,.82,(x,y,z,u,v,w)=>[x,y*(1+.17*u),z]));break;
 case 3:body(sphere(.82,.82,.32,1));break;
 case 4:body(sphere(.83,.76,.35,.42));break;
 case 5:body(plate(2.3,.88,.5,.44,.17));break;
 case 6:body(plate(.88,2,.58,.44,.18));break;
 case 7:body(sphere(.8,.8,.35,.58),undefined,undefined,[0,0,PI/4]);break;
 case 8:body(sphere(.55,.97,.38,1,(x,y,z,u,v,w)=>[x*(.8-.27*v),y,z*(.8-.16*v)]));break;
 case 9:body(drop(.8,1,.55));break;
 case 10:body(sphere(.9,.83,.22,1,(x,y,z,u,v,w)=>[x*(.65+.33*v),y,z+.12*v*v]));break;
 case 11:body(sphere(.85,.83,.35,.8,(x,y,z,u,v,w)=>{const a=Math.atan2(y,x);const s=1+.16*Math.cos(3*a+PI/2);return[x*s,y*s,z];}));break;
 case 12:body(sphere(1,.67,.36,.5,(x,y,z,u,v,w)=>[x,y,z-.17*(1-u*u)*(1-v*v)]));break;
 case 13:body(sphere(1.14,.63,.4,.6,(x,y,z,u,v,w)=>[x,y*(.67+.33*Math.abs(u)),z*(.7+.3*Math.abs(u))]));break;
 case 14:body(sphere(.85,.85,.36,.65,(x,y,z,u,v,w)=>{const a=Math.atan2(y,x),f=.85+.25*Math.cos(3*a-PI/2);return[x*f,y*f,z];}));break;
 case 15:body(ring(.76,.63,.24));break;
 case 16:body(volumeTube([[-.6,-.75,0],[-.96,-.1,0],[-.63,.7,0],[.2,.92,0],[.73,.54,0]],.22));break;
 case 17:body(volumeTube([[-.9,-.65,0],[-.82,.25,0],[0,.8,0],[.82,.25,0],[.9,-.65,0]],.24));break;
 case 18:body(plate(1.9,1.3,.28,.4));body(frame(1.88,1.28,.24,.39,.19),'rolled-perimeter',[0,0,.19]);break;
 case 19:body(sphere(1.5,.42,.31,.5,(x,y,z,u,v,w)=>[x,y*(1+.12*Math.cos(u*4*PI)),z*(1+.07*Math.cos(u*4*PI))]));break;
 case 20:body(sphere(.94,.69,.35,.45,(x,y,z,u,v,w)=>[x,y,z*(.85+.38*u)+.1*u]));break;
 case 21:body(sphere(.94,.82,.25,.7),'outer-bed');add(g,sphere(.48,.44,.24),m.blue,'inner-dome',[0,0,.3],undefined,'thumb');break;
 case 22:body(plate(1.48,1.48,.55,.7,.18));break;
 case 23:body(plate(2.04,.98,.35,.45),'shared-cradle');add(g,sphere(.48,.4,.25),m.blue,'left-action',[-.48,0,.23],undefined,'thumb');add(g,sphere(.48,.4,.25),m.pigment,'right-action',[.48,0,.23],undefined,'thumb');break;
 case 24:body(sphere(.98,.7,.43,.53,(x,y,z,u,v,w)=>[x,y,z-.27*Math.exp(-7*(u*u+v*v))*Math.max(w,0)]));break;
 }
}
function slider(g,m,{vertical=false,dual=false,steps=0,floating=false,meniscus=false,three=false,twowell=false}={}) {
 const assembly=new THREE.Group();assembly.name='slider-assembly';g.add(assembly);if(vertical)assembly.rotation.z=PI/2;
 add(assembly,plate(2.6,.69,.36,.34),m.gel,'cradle');
 add(assembly,plate(2.14,.19,.13,.095,.04),meniscus?m.water:m.blue,'channel',[0,0,.22],undefined,meniscus?'water':'guide');
 for(let i=0;i<steps;i++)add(assembly,bowl(.085,.085,.06,.02),m.nacre,'detent-'+i,[-.93+i*1.86/(steps-1),0,.28],undefined,'guide');
 if(twowell)for(const x of [-.7,.7])add(assembly,ring(.22,.22,.035),m.nacre,'well-lip-'+x,[x,0,.25],undefined,'guide');
 const poses=dual?[-.62,.62]:[three?0:-.58];
 for(let i=0;i<poses.length;i++){
  const o=add(assembly,sphere(.32,.27,.24,.7),i?m.pigment:m.shell,'thumb-'+i,[poses[i],0,floating?.63:.39],undefined,'thumb');setMotion(o,'slider',{axis:'x',min:-.94,max:.94,detents:steps|| (three?3:twowell?2:0),index:i});
  if(floating){add(o,volumeTube([[-.18,0,-.36],[-.15,0,-.18],[0,0,-.09]],.025),m.blue,'suspension-'+i,undefined,undefined,'connector');}
 }
}
function precision(g,n,m) {
 switch(n){
 case 1:slider(g,m);add(g.getObjectByName('thumb-0'),ring(.38,.31,.055),m.blue,'flexible-skirt',[0,0,-.08],undefined,'connector');break;
 case 2:{const points=[];for(let i=0;i<=32;i++){const a=THREE.MathUtils.lerp(PI-.611,.611,i/32);points.push([Math.cos(a)*1.22,Math.sin(a)*1.22-1.02,0]);}add(g,volumeTube(points,.18),m.gel,'curved-guide');setMotion(add(g,sphere(.3,.27,.26),m.shell,'thumb',[0,.2,.2],undefined,'thumb'),'arc',{radius:1.22,start:PI-.611,end:.611,center:[0,-1.02]});break;}
 case 3:slider(g,m,{vertical:true});break;
 case 4:slider(g,m,{dual:true});break;
 case 5:slider(g,m,{steps:7});break;
 case 6:slider(g,m);add(g,paramSheet(2.1,.45,.055,(u,v)=>[u*1.05,v*.22-.38,.2+.12*Math.sin(u*PI*2)]),m.pigment,'flexible-ribbon',undefined,undefined,'connector');break;
 case 7:{const rail=add(g,morphingLiquidRail(),m.blue,'liquid-rail',undefined,undefined,'water');setMotion(rail,'liquidRail',{steps:7,min:0,max:1});rail.userData.deformable=false;rail.userData.continuousLiquid=true;break;}
 case 8:slider(g,m,{floating:true});break;
 case 9:slider(g,m,{twowell:true});g.scale.x=.66;break;
 case 10:slider(g,m,{twowell:true});add(g,sphere(.16,.18,.1),m.gel,'centre-saddle',[0,0,.29]);break;
 case 11:add(g,bowl(1.1,.65,.32,.13),m.gel,'rocker-bed');setMotion(add(g,plate(1.86,.82,.3,.3),m.blue,'rocker',[0,0,.18],undefined,'dial'),'rocker',{axis:'y',min:-.26,max:.26});break;
 case 12:slider(g,m,{steps:3,three:true});break;
 case 13:case 14:{add(g,ring(.75,.75,.14),m.gel,'dial-collar');const knob=add(g,sphere(.67,.67,.35,.63),m.blue,'dial-cap',[0,0,.12],undefined,'dial');setMotion(knob,'dial',{axis:'z'});add(knob,sphere(.045,.13,.02),m.glow,'dial-marker',[0,.46,.32],undefined,'light');if(n===14)for(let i=0;i<32;i++){let a=i/32*PI*2;add(knob,sphere(.045,.13,.06,.6),m.gel,'grip-rib-'+i,[Math.sin(a)*.64,Math.cos(a)*.64,.06],[0,0,-a],'body');}break;}
 case 15:add(g,sphere(.49,.49,.3),m.blue,'inner-dial',[0,0,.15],undefined,'dial');add(g,ring(.86,.86,.17),m.gel,'outer-dial',undefined,undefined,'dial');add(g,ring(1.11,1.11,.08),m.nacre,'fixed-collar',undefined,undefined,'guide');break;
 case 16:add(g,ring(.93,.93,.17,-PI*.3,PI*1.6),m.gel,'arc-guide');setMotion(add(g,sphere(.26,.26,.24),m.shell,'orbit-grip',[.78,.5,.14],undefined,'thumb'),'arc',{radius:.93,start:-PI*.3,end:PI*1.3});break;
 case 17:add(g,ring(.8,.8,.14),m.gel,'captive-ring');setMotion(add(g,sphere(.26,.26,.25),m.blue,'orbit-bead',[0,.8,.13],undefined,'thumb'),'orbit',{radius:.8});break;
 case 18:add(g,bowl(.8,.8,.58,.2),m.gel,'trackball-cup',[0,0,-.14]);setMotion(add(g,sphere(.59,.59,.59),m.blue,'trackball',[0,0,.15],undefined,'dial'),'trackball');break;
 case 19:{add(g,bowl(.64,.64,.36,.16),m.gel,'joystick-root');const pivot=new THREE.Group();pivot.name='joystick-pivot';pivot.userData={role:'joint'};g.add(pivot);setMotion(pivot,'joystick',{maxAngle:.5});add(pivot,volumeTube([[0,0,.04],[0,0,.38],[0,0,.7]],.15),m.blue,'joystick-stem',undefined,undefined,'connector');add(pivot,sphere(.39,.36,.21),m.shell,'joystick-crown',[0,0,.78],undefined,'thumb');break;}
 case 20:add(g,plate(1.85,1.45,.46,.46),m.gel,'pressure-pad');add(g,sphere(.72,.52,.12,.48),m.blue,'contact-surface',[0,0,.25],undefined,'thumb');break;
 case 21:gelBody(g,12,m);setMotion(add(g,sphere(.24,.24,.16),m.shell,'xy-puck',[.25,.14,.31],undefined,'thumb'),'xy',{bounds:[-.7,.7,-.43,.43]});break;
 case 22:{add(g,plate(1.46,1.27,.34,.32),m.gel,'wheel-bed');for(const x of[-.53,.53])add(g,plate(.22,1.14,.55,.1),m.gel,'wheel-support-'+x,[x,0,.23]);const wheel=add(g,new THREE.CylinderGeometry(.45,.45,.75,64,4),m.blue,'scroll-wheel',[0,0,.3],[0,0,PI/2],'dial');setMotion(wheel,'wheel',{axis:'y'});for(let i=0;i<24;i++){let a=i/24*PI*2;add(wheel,volumeTube([[Math.sin(a)*.45,-.36,Math.cos(a)*.45],[Math.sin(a)*.45,.36,Math.cos(a)*.45]],.017),m.nacre,'wheel-ridge-'+i);}break;}
 case 23:add(g,volumeTube([[-.86,-.28,0],[-.86,.22,.36],[0,.44,.48],[.86,.22,.36],[.86,-.28,0]],.16),m.gel,'grip-handle');for(const x of[-.86,.86])add(g,sphere(.26,.23,.14),m.nacre,'handle-anchor-'+x,[x,-.28,0]);break;
 case 24:{add(g,ring(.98,.98,.12),m.nacre,'iris-ring');for(let i=0;i<8;i++){const a=i*PI/4;const leaf=new THREE.Group();leaf.name='iris-leaf-pivot-'+i;leaf.rotation.z=a;leaf.userData={role:'iris',motion:{kind:'iris',axis:'z',baseAngle:a}};g.add(leaf);add(leaf,paramSheet(.7,.7,.07,(u,v)=>[.41+.37*u,.3*v+.16*u*u,.03+u*.015]),i%2?m.blue:m.gel,'iris-leaf-'+i,[0,0,i*.009],undefined,'leaf');}break;}
 }
}
function rectangularTray(g,m,w=2.1,h=1.4,depth=.4) {
 add(g,plate(w-.12,h-.12,.18,.3,.06),m.gel,'tray-floor',[0,0,-depth/2]);
 add(g,frame(w,h,depth,.4,.2),m.gel,'tray-wall');
}
function containers(g,n,m) {
 const slab=(w,h,t=.38,r=.38,name='panel',pos)=>add(g,plate(w,h,t,r),m.gel,name,pos);
 switch(n){
 case 1:slab(1.9,1.4);break;
 case 2:slab(3.2,1.5,.42);break;
 case 3:slab(1.5,2.5,.43);break;
 case 4:slab(3.2,.62,.31,.28);break;
 case 5:case 6:slab(2.2,1.35,.39);add(g,volumeTube([[(n===5?-1:1)*.87,-.35,0],[(n===5?-1:1)*1.05,-.65,0],[(n===5?-1:1)*1.23,-.76,0]],.12),m.gel,'anchor-tail');break;
 case 7:slab(2.3,.62,.38,.3,'toast-body');add(g,plate(1.6,.05,.035,.023,.009),m.glow,'underside-recess',[0,-.25,-.03],undefined,'light');break;
 case 8:add(g,bowl(.95,.95,.42,.12),m.gel,'round-tray');break;
 case 9:add(g,bowl(1.35,.72,.43,.14),m.gel,'oval-tray');break;
 case 10:rectangularTray(g,m);break;
 case 11:add(g,bowl(.85,.85,1.25,.12),m.shell,'reservoir-wall');add(g,sphere(.7,.7,.4,.38),m.water,'reservoir-fluid',[0,0,-.04],undefined,'water');break;
 case 12:add(g,bowl(.56,.56,.85,.09),m.shell,'cup-wall');add(g,plate(.88,.88,.49,.43,.08),m.water,'cup-fluid',[0,0,-.02],undefined,'water');break;
 case 13:slab(2.05,1.4,.27,.45,'inset-support');add(g,frame(2.05,1.4,.25,.45,.26),m.blue,'raised-bed',[0,0,.18]);break;
 case 14:slab(2.5,.85,.28,.28,'floating-shelf');add(g,volumeTube([[-.9,-.2,-.08],[-.8,-.68,-.18],[-.35,-.32,-.1]],.1),m.nacre,'cantilever-support');break;
 case 15:for(let i=0;i<3;i++){const panel=slab(2.02,1.28,.25,.3,'stack-layer-'+i,[i*.13,i*.14,(i-1)*.35]);panel.userData.role='panel';setMotion(panel,'stack',{layer:i});}break;
 case 16:{const left=slab(1.35,1.7,.3,.25,'hinged-left',[-.74,0,0]);left.rotation.y=-.18;const right=slab(1.35,1.7,.3,.25,'hinged-right',[.74,0,0]);right.rotation.y=.18;setMotion(left,'hinge',{axis:'y',anchor:[-.06,0,0]});setMotion(right,'hinge',{axis:'y',anchor:[.06,0,0]});add(g,volumeTube([[0,-.7,0],[0,0,0],[0,.7,0]],.075),m.blue,'elastic-hinge',undefined,undefined,'hinge');break;}
 case 17:add(g,paramSheet(2.2,1.6,.1,(u,v)=>[u*1.1,v*.8,.17*Math.sin(u*2)*Math.cos(v*1.8)]),m.gel,'flexible-panel',undefined,undefined,'body');break;
 case 18:add(g,paramSheet(2.2,1.6,.09,(u,v)=>{const curl=Math.max(0,u+v-.65);return[u*1.1-.14*curl*curl,v*.8-.1*curl*curl,.4*curl*curl];}),m.gel,'curled-panel');break;
 case 19:add(g,frame(2.1,1.5,.31,.4,.23),m.gel,'rounded-frame');break;
 case 20:add(g,ring(.86,.86,.2),m.gel,'circular-frame');add(g,ring(.65,.65,.04),m.nacre,'inner-attachment');break;
 case 21:add(g,volumeTube([[-.87,-.85,0],[-.87,.12,0],[-.6,.75,0],[0,1,0],[.6,.75,0],[.87,.12,0],[.87,-.85,0]],.16),m.gel,'arch-frame');break;
 case 22:slab(2.2,1.65,.22,.45,'well-bottom',[0,0,-.22]);add(g,frame(2.18,1.63,.27,.45,.19),m.gel,'outer-lip',[0,0,.08]);add(g,frame(1.63,1.08,.18,.28,.13),m.blue,'inner-lip',[0,0,-.07]);break;
 case 23:{add(g,bowl(.92,.78,.7,.1),m.shell,'reservoir-shell');for(let i=0;i<6;i++)add(g,ring(.84,.7,.065),m.gel,'pleat-'+i,[0,0,-.3+i*.12]);break;}
 case 24:add(g,bowl(1.3,.21,.27,.065),m.shell,'edge-gutter');add(g,volumeTube([[1.08,0,-.04],[1.3,-.17,-.1],[1.4,-.37,-.2]],.07),m.water,'gutter-outlet',undefined,undefined,'water');break;
 }
}
function dockSocket(g,m,x,y,z,i=0,normal=[0,0,0]) {
 add(g,ring(.22,.22,.055),m.nacre,'socket-'+i,[x,y,z],normal,'guide');
 const a=new THREE.Object3D();a.name='mount-'+i;a.position.set(x,y,z);a.rotation.set(...normal);a.userData={role:'socket',portType:'mount',radius:.19};g.add(a);
}
function assemblies(g,n,m) {
 switch(n){
 case 1:add(g,sphere(1.55,.58,.28,.65),m.gel,'buoyant-dock');for(let i=0;i<4;i++)dockSocket(g,m,-1.02+i*.68,0,.3,i);break;
 case 2:add(g,plate(3.2,.67,.31,.3),m.gel,'modular-rail');for(let i=0;i<5;i++)dockSocket(g,m,-1.18+i*.59,0,.2,i);break;
 case 3:add(g,volumeTube([[-1.25,.37,0],[-.75,-.22,.06],[0,-.43,.1],[.75,-.22,.06],[1.25,.37,0]],.3),m.gel,'crescent-dock');for(let i=0;i<5;i++){let a=-PI*.8+i*PI*.15;dockSocket(g,m,Math.cos(a)*1.3,Math.sin(a)*.65+.2,.34,i);}break;
 case 4:for(let i=0;i<3;i++){add(g,plate(2.4-i*.42,.65,.28,.25),m.gel,'dock-tier-'+i,[0,(i-1)*.48,i*.28]);dockSocket(g,m,0,(i-1)*.48,i*.28+.17,i);}break;
 case 5:add(g,ring(1.16,1.16,.27),m.gel,'ring-dock');for(let i=0;i<6;i++){let a=i/6*PI*2;dockSocket(g,m,Math.sin(a)*1.16,Math.cos(a)*1.16,.27,i);}break;
 case 6:add(g,volumeTube([[0,-.85,0],[0,-.18,0],[.13,.25,.04],[.14,.85,0]],.15),m.gel,'branch-root');for(let i=0;i<4;i++){const s=i%2?-1:1,y=-.28+Math.floor(i/2)*.6;add(g,volumeTube([[0,y,0],[s*.45,y+.15,0],[s*.91,y+.39,.03]],.13),m.gel,'branch-'+i);dockSocket(g,m,s*.91,y+.39,.15,i);}break;
 case 7:add(g,volumeTube([[-1.4,.55,0],[-.75,.12,.1],[0,-.03,.2],[.75,.12,.1],[1.4,.55,0]],.11),m.blue,'suspended-rail',undefined,undefined,'connector');for(let i=0;i<3;i++)dockSocket(g,m,(i-1)*.74,.03+(i===1?-.04:.12),.29,i);for(const x of[-1.4,1.4])add(g,sphere(.25,.25,.14),m.nacre,'suspension-anchor-'+x,[x,.55,0]);break;
 case 8:add(g,ring(1.28,.58,.04),m.nacre,'orbit-track',undefined,[.3,.2,0],'guide');for(let i=0;i<5;i++){let a=i/5*PI*2;const b=add(g,sphere(.24,.23,.2),m.gel,'orbit-carrier-'+i,[Math.cos(a)*1.28,Math.sin(a)*.58,Math.sin(a)*.35],undefined,'thumb');setMotion(b,'orbit',{radius:1.28,phase:a});}break;
 case 9:add(g,sphere(.43,.43,.26),m.gel,'radial-hub');for(let i=0;i<6;i++){let a=i/6*PI*2;add(g,volumeTube([[Math.cos(a)*.32,Math.sin(a)*.32,0],[Math.cos(a)*.95,Math.sin(a)*.95,0]],.09),m.blue,'spoke-'+i,undefined,undefined,'connector');dockSocket(g,m,Math.cos(a)*1.04,Math.sin(a)*1.04,.08,i);}break;
 case 10:for(let i=0;i<9;i++){const x=(i-4)*.32,y=.22*Math.sin(i*.64),z=.2*Math.cos(i*.64);add(g,sphere(.16,.16,.14),i===4?m.blue:m.gel,'path-bead-'+i,[x,y,z],undefined,'thumb');}break;
 case 11:for(let i=0;i<5;i++){const x=(i-2)*.63,y=Math.sin(i*1.2)*.22,z=Math.cos(i*1.2)*.15;add(g,stoneGeometry(.32,.2,.23,i),m.stone,'stepping-stone-'+i,[x,y,z],undefined,'stone');dockSocket(g,m,x,y,z+.23,i);}break;
 case 12:add(g,volumeTube([[-1,0,0],[-.4,.12,.06],[.25,-.08,.03],[1,.17,0]],.16),m.gel,'elastic-connector',undefined,undefined,'connector');for(const x of[-1,1])add(g,sphere(.24,.24,.2),m.nacre,'connector-port-'+x,[x,x===1?.17:0,0],undefined,'guide');break;
 case 13:add(g,tube([[-1,-.25,0],[-.5,-.15,.2],[0,.2,.25],[1,.3,0]],.22,24),m.shell,'channel-wall',undefined,undefined,'connector');add(g,volumeTube([[-1,-.25,0],[-.5,-.15,.2],[0,.2,.25],[1,.3,0]],.16),m.water,'channel-fluid',undefined,undefined,'water');for(const [i,p]of [[0,[-1,-.25,0]],[1,[1,.3,0]]])dockSocket(g,m,...p,i);break;
 case 14:add(g,volumeTube([[-1,-.3,0],[-.35,.3,.12],[.4,-.17,.21],[1,.34,0]],.13),m.shell,'light-guide');add(g,volumeTube([[-1,-.3,0],[-.35,.3,.12],[.4,-.17,.21],[1,.34,0]],.034),m.glow,'light-core',undefined,undefined,'light');break;
 case 15:for(let i=0;i<2;i++){add(g,plate(.71,1,.28,.3),m.gel,'magnetic-face-'+i,[(i-.5)*1.03,0,0]);add(g,sphere(.14,.33,.16),m.nacre,'magnetic-pole-'+i,[(i-.5)*.47,0,0]);}break;
 case 16:for(const x of[-.5,.5])add(g,plate(.78,1.1,.26,.23),m.gel,'hinge-leaf-'+x,[x,0,0]);add(g,volumeTube([[0,-.48,0],[0,.48,0]],.13),m.nacre,'hinge-axis',undefined,undefined,'hinge');break;
 case 17:add(g,bowl(.53,.53,.75,.17),m.gel,'retaining-socket',[0,0,-.1]);add(g,sphere(.31,.31,.31),m.blue,'joint-ball',[0,0,.22],undefined,'dial');add(g,volumeTube([[0,0,.35],[0,0,.82]],.13),m.nacre,'joint-stem',undefined,undefined,'connector');break;
 case 18:{const p=[[-.9,.3,.2],[0,.64,0],[.9,.3,-.18],[-.55,-.5,-.07],[.6,-.48,.3]];for(let i=0;i<p.length;i++){add(g,sphere(i===1?.34:.24,.25,.23),i%2?m.blue:m.gel,'constellation-node-'+i,p[i],undefined,'thumb');if(i) add(g,volumeTube([p[i-1],p[i]],.025),m.nacre,'constellation-edge-'+i,undefined,undefined,'connector');}break;}
 }
}
function liquid(g,n,m) {
 const fluid=(geo,name,pos,rot)=>add(g,geo,m.water,name,pos,rot,'water');
 switch(n){
 case 1:fluid(plate(2.5,1.65,.4,.48,.09),'bounded-water');add(g,plate(2.65,1.8,.2,.5),m.stone,'receiver-bed',[0,0,-.3],undefined,'stone');break;
 case 2:add(g,bowl(1.17,.88,.85,.16),m.stone,'deep-basin');fluid(sphere(.99,.73,.35,.34),'basin-water',[0,0,.04]);for(let i=0;i<3;i++)add(g,stoneGeometry(.2,.2,.3,i),m.stone,'submerged-obstacle-'+i,[(i-1)*.55,.17*(i%2),.08],undefined,'stone');break;
 case 3:fluid(sphere(1.3,.88,.1,.76,(x,y,z,u,v,w)=>[x*(1+.06*Math.sin(v*5)),y,z]),'thin-pool');break;
 case 4:fluid(sphere(1.1,.8,.12,.8),'parent-pool');fluid(sphere(.46,.44,.58,1,(x,y,z,u,v,w)=>[x*(1-.3*w),y*(1-.3*w),z]),'liquid-mound',[0,0,.2]);break;
 case 5:{const profile=[];for(let i=0;i<=36;i++){const t=i/36;profile.push(V(.36+.31*t,.05+.72*t*t));}for(let i=36;i>=0;i--){const t=i/36;profile.push(V(.32+.31*t,.05+.72*t*t));}const crown=new THREE.LatheGeometry(profile,96);crown.rotateX(PI/2);const a=crown.attributes.position;for(let i=0;i<a.count;i++){const x=a.getX(i),y=a.getY(i),z=a.getZ(i),ang=Math.atan2(y,x);a.setZ(i,z+Math.pow(Math.max(z,0),3)*.23*Math.cos(9*ang));}crown.computeVertexNormals();fluid(crown,'impact-crown');for(let i=0;i<9;i++){let a=i/9*PI*2;fluid(drop(.06,.1,.07), 'crown-satellite-'+i,[Math.cos(a)*.75,Math.sin(a)*.75,.85+(i%3)*.08]);}break;}
 case 6:fluid(paramSheet(2.2,1.2,.2,(u,v)=>[u*1.1,Math.sin((v+1)*2.1)*.48,.1+Math.cos((v+1)*2.1)*-.5+.24*(v+1)]),'rolling-crest');break;
 case 7:fluid(volumeTube([[-.8,-.65,0],[-.8,.3,.17],[0,.9,.2],[.8,.3,.17],[.8,-.65,0]],.18),'liquid-arch');break;
 case 8:fluid(drop(.82,.95,.58),'standing-drop');break;
 case 9:fluid(drop(.7,1,.55,true),'hanging-drop');add(g,ring(.24,.24,.05),m.nacre,'outlet-ring',[0,.74,0],[PI/2,0,0],'guide');break;
 case 10:fluid(sphere(.5,.69,.48,1,(x,y,z,u,v,w)=>[x*(1-.1*v),y,z*(1-.1*v)]),'falling-drop');break;
 case 11:fluid(sphere(.57,1,.48,1,(x,y,z,u,v,w)=>[x*(.4+.58*Math.abs(v)),y,z*(.4+.58*Math.abs(v))]),'capillary-neck');for(const y of[-.93,.93])add(g,plate(1.1,.15,.85,.075,.02),m.nacre,'wet-boundary-'+y,[0,y,0],undefined,'guide');break;
 case 12:{add(g,hollowVolume(plate(2.7,.8,.68,.38,.2),plate(2.55,.65,.53,.31,.15)),m.shell,'pigment-shell');fluid(plate(2.40,.51,.47,.25,.12),'carrier-fluid');add(g,volumeTube([[-1.02,0,0],[-.65,.12,.07],[-.23,-.1,.04],[.15,.1,-.01],[.55,-.04,.1]],.13),m.pigment,'pigment-inflow',undefined,undefined,'pigment');add(g,sphere(.3,.21,.19),m.pigment,'pigment-front',[.7,0,.02],undefined,'pigment');break;}
 case 13:add(g,tube([[-1,-.4,0],[-.7,.42,.1],[.1,.47,.28],[.75,-.35,.1],[1,-.45,0]],.23,24),m.shell,'curved-channel-wall');fluid(volumeTube([[-1,-.4,0],[-.7,.42,.1],[.1,.47,.28],[.75,-.35,.1],[1,-.45,0]],.17),'curved-fluid');break;
 case 14:add(g,hollowVolume(ring(.8,.8,.22),ring(.8,.8,.18)),m.shell,'annular-wall');fluid(ring(.8,.8,.155),'annular-fluid');add(g,ring(.8,.8,.065,0,PI*.85),m.pigment,'retained-pigment',undefined,undefined,'pigment');break;
 case 15:for(let i=0;i<3;i++){const a=i/3*PI*2;const pts=[[0,0,0],[Math.cos(a)*.45,Math.sin(a)*.45,.07],[Math.cos(a),Math.sin(a),0]];add(g,tube(pts,.2,24),m.shell,'junction-wall-'+i);fluid(volumeTube(pts,.14),'junction-fluid-'+i);}fluid(sphere(.3,.3,.25),'junction-core');break;
 case 16:fluid(paramSheet(1.6,1.8,.08,(u,v)=>[u*.8+.06*Math.sin(v*5+u),v*.9,.09*Math.sin(u*6+v*2)]),'liquid-curtain');for(const x of[-.8,.8])fluid(volumeTube([[x,-.9,.02],[x+.02,0,0],[x,.9,0]],.055),'curtain-edge-'+x);break;
 case 17:add(g,volumeTube([[-1,.68,0],[-.72,.1,.12],[-.2,-.45,0],[.38,-.2,.14],[.35,.21,.25],[-.04,.3,.25],[-.33,.06,.4],[.06,-.04,.49],[.72,.14,.32]],.14),m.pigment,'viscous-ribbon',undefined,undefined,'pigment');break;
 case 18:add(g,hollowVolume(sphere(1.2,.85,.43,.54),sphere(1.12,.77,.35,.54)),m.shell,'carrier-envelope');fluid(sphere(1.06,.71,.29,.54),'clear-carrier');for(let i=0;i<3;i++)add(g,paramSheet(1.8,.55,.025,(u,v)=>[u*.9,v*.25+.2*Math.sin(u*5+i*.3),.1*Math.sin(u*4)+i*.11-.1]),m.pigment,'folded-colour-'+i,undefined,undefined,'pigment');break;
 case 19:add(g,drop(.75,1,.55),m.pigment,'viscous-bulb',undefined,[0,0,PI], 'pigment');add(g,volumeTube([[0,-.6,0],[.05,-.95,.02]],.09),m.pigment,'viscous-neck',undefined,undefined,'pigment');break;
 case 20:{const p=[[0,-.8,0],[0,-.1,.06],[-.68,.53,0],[.68,.53,.13]];fluid(volumeTube([p[0],p[1],p[2]],.13),'strand-main');fluid(volumeTube([p[1],p[3]],.11),'strand-branch');fluid(volumeTube([p[2],[-.85,.12,.05]],.075),'strand-return');break;}
 case 21:{const profile=[V(0,-.6),V(.1,-.58),V(.24,-.42),V(.45,-.14),V(.85,.03),V(1.1,.08),V(1.15,-.42),V(.7,-.68),V(0,-.68)];const f=new THREE.LatheGeometry(profile,96);f.rotateX(PI/2);fluid(f,'whirlpool-volume');for(let i=0;i<3;i++){const pts=[];for(let j=0;j<40;j++){let a=j/39*PI*1.7+i*2*PI/3,r=.2+j/39*.78;pts.push([Math.cos(a)*r,Math.sin(a)*r,-.45+j/39*.51]);}add(g,volumeTube(pts,.015),m.pigment,'spiral-tracer-'+i,undefined,undefined,'pigment');}break;}
 case 22:fluid(sphere(.98,.61,.2,.7),'parent-edge',[-.43,0,0]);fluid(volumeTube([[0,0,0],[.5,.17,.1],[.98,.25,.27]],.2),'liquid-tongue');fluid(sphere(.27,.24,.23),'tongue-head',[1,.25,.27]);break;
 case 23:fluid(sphere(.84,.61,.16),'parent-meniscus',[0,-.55,0]);fluid(drop(.5,1.1,.35),'rising-peak',[0,.04,.08]);add(g,sphere(.25,.18,.22),m.water,'transfer-bead',[0,1,.08],undefined,'water');break;
 case 24:{const merged=fluid(coalescedDropGeometry(),'coalesced-liquid');merged.userData.surfaceTopology='single implicit smooth union';merged.userData.initialLobes=[{position:[-.47,-.045,-.035],radius:.64},{position:[.47,.08,.03],radius:.59}];break;}
 }
 g.userData.solverDomain='liquid-initial-geometry';
}
function films(g,n,m) {
 const bubble=(x=0,y=0,z=0,r=.68,name='film-bubble')=>add(g,sphere(r,r,r),m.film,name,[x,y,z],undefined,'film');
 switch(n){
 case 1:bubble(0,0,0,.76);break;
 case 2:add(g,sphere(.55,1.08,.58,1,(x,y,z,u,v,w)=>[x+.1*v*v,y,z]),m.film,'elongated-film',undefined,undefined,'film');break;
 case 3:bubble(-.45,0,0,.67,'bubble-left');bubble(.45,.05,0,.66,'bubble-right');add(g,sphere(.025,.45,.44),m.film,'contact-film',[0,0,0],undefined,'film');break;
 case 4:for(let i=0;i<3;i++){let a=i/3*PI*2; bubble(Math.cos(a)*.4,Math.sin(a)*.4,0,.58,'junction-bubble-'+i);}add(g,volumeTube([[0,-.18,0],[0,.25,0]],.018),m.water,'plateau-border',undefined,undefined,'water');break;
 case 5:for(let i=0;i<9;i++){const a=i*2.4,r=i===0?0:.55+(i%2)*.18;bubble(Math.cos(a)*r,Math.sin(a)*r,Math.sin(i*1.2)*.22,.38+(i%3)*.065,'cluster-cell-'+i);}break;
 case 6:{const profile=[];for(let i=0;i<=36;i++){let y=-.8+i/36*1.6;profile.push(V(.26*Math.cosh(y*1.3),y));}const f=new THREE.LatheGeometry(profile,72);add(g,f,m.film,'catenoid-film',undefined,undefined,'film');for(const y of[-.8,.8])add(g,ring(.413,.413,.027),m.nacre,'boundary-ring-'+y,[0,y,0],[PI/2,0,0],'guide');break;}
 case 7:add(g,paramSheet(1.6,1.6,.008,(u,v)=>[u*.8,v*.8,.28*(1-u*u)*(1-v*v)]),m.film,'supported-film',undefined,undefined,'film');for(const x of[-.8,.8])add(g,volumeTube([[x,-.8,0],[x,.8,0]],.023),m.nacre,'film-support-'+x,undefined,undefined,'guide');break;
 case 8:bubble(0,0,0,.8,'draining-film');for(let i=0;i<5;i++){const y=-.5+i*.2,r=Math.sqrt(.64-y*y);add(g,ring(r,r,.008+Math.max(0,-y)*.024),m.film,'drainage-band-'+i,[0,y,0],[PI/2,0,0],'film');}break;
 case 9:bubble(0,0,0,.8,'interference-film');g.children[0].userData.thicknessField={minNm:120,maxNm:620,advection:true};break;
 case 10:add(g,sphere(1.05,.61,.56,.9,(x,y,z,u,v,w)=>[x,y*(.43+.65*Math.abs(u)),z*(.43+.65*Math.abs(u))]),m.film,'pinching-doublet',undefined,undefined,'film');break;
 case 11:add(g,sphere(.97,.65,.61,.65,(x,y,z,u,v,w)=>[x,y*(.78+.2*Math.abs(u)),z*(.78+.2*Math.abs(u))]),m.film,'coalescing-envelope',undefined,undefined,'film');break;
 case 12:{const f=new THREE.SphereGeometry(.79,72,48,0,2*PI,.42,PI-.42);add(g,f,m.film,'ruptured-film',undefined,[PI/2,0,0],'film');add(g,ring(Math.sin(.42)*.79,Math.sin(.42)*.79,.027),m.water,'retracting-rim',[0,0,Math.cos(.42)*.79],undefined,'water');break;}
 }
 g.userData.solverDomain='thin-film-initial-geometry';
}
function fern(g,m,pos=[0,0,0],length=1.8,angle=0,name='fern') {
 const f=new THREE.Group();f.name=name;f.position.set(...pos);f.rotation.z=angle;g.add(f);
 add(f,volumeTube([[0,0,0],[.05,length*.33,.05],[.17,length*.7,.11],[.28,length,.05]],.026),m.leaf,'fern-stem',undefined,undefined,'leaf');
 for(let i=0;i<10;i++){
  const t=(i+.4)/10,l=(1-t)*.47+.09;
  for(const side of[-1,1]) {
   const leaflet=add(f,leafGeometry(l,l*.27,.05),m.leaf,'leaflet-'+i+'-'+side,[.25*t*t+side*l*.25,t*length,.09*t],[0,0,side*1.04],'leaf');
   leaflet.userData.motion={kind:'wind',stiffness:.55,phase:i*.31+side*.2,anchor:[0,-l/2,0]};
  }
 }
 return f;
}
function environment(g,n,m) {
 const waterPlane=(w=3,h=2,depth=.27,y=0)=>add(g,plate(w,h,depth,.55,.08),m.water,'environment-water',[0,y,0],[-PI/2,0,0],'water');
 const stone=(x,y,z,rx,ry,rz,seed=1)=>add(g,stoneGeometry(rx,ry,rz,seed),m.stone,'stone-'+seed,[x,y,z],undefined,'stone');
 switch(n){
 case 1:waterPlane(3.7,2.7,.3,-.2);const dock=add(g,sphere(1.2,.18,.51,.58),m.gel,'floating-dock',[0,.1,0]);for(let i=0;i<3;i++)dockSocket(g,m,(i-1)*.64,.28,0,i,[-PI/2,0,0]);break;
 case 2:waterPlane(5.2,3.9,.48,-.2);for(let i=0;i<7;i++)stone(-2.2+i*.72,-.17,-1.65,.52,.28,.48,i+5);break;
 case 3:waterPlane(3.1,2.3,.22,-.04);for(let i=0;i<15;i++){const a=i/15*2*PI;stone(Math.cos(a)*1.4,-.1,Math.sin(a),.33,.24,.27,i+8);}for(let i=0;i<4;i++)stone((i-1.5)*.46,-.16,.1*Math.sin(i),.2,.09,.22,i+30);break;
 case 4:{const shore=add(g,volumeTube([[-1.7,0,-.55],[-.9,.06,-.24],[0,.08,.08],[.85,.03,.35],[1.6,-.05,.25]],.43),m.stone,'curved-shore',undefined,undefined,'stone');waterPlane(3.7,2.3,.2,-.26);break;}
 case 5:for(let i=0;i<18;i++){const a=i*2.399,r=.25+.17*Math.sqrt(i);stone(Math.cos(a)*r,-.05+seedRand(i)*.13,Math.sin(a)*r,.2+seedRand(i+30)*.16,.15+seedRand(i+50)*.09,.17+seedRand(i+10)*.13,i);}break;
 case 6:stone(0,0,0,.87,.48,.63,7);add(g,sphere(.865,.485,.635,.86),m.water,'wet-film',undefined,undefined,'water');break;
 case 7:stone(0,-.3,0,.84,.24,.67,4);stone(.11,.09,-.03,.61,.2,.46,7);stone(-.03,.42,.06,.4,.17,.32,11);break;
 case 8:stone(0,-.12,0,.77,.64,.66,17);add(g,sphere(.57,.075,.46,.65),m.nacre,'pedestal-mount',[0,.51,0],undefined,'guide');break;
 case 9:waterPlane(3,2.4,.21,-.24);stone(0,-.11,0,.83,.37,.64,12);add(g,sphere(.59,.1,.45,.68),m.leaf,'island-top',[0,.17,0],undefined,'environment');break;
 case 10:waterPlane(3.2,2.25,.43,.1);add(g,plate(2.6,1.7,.3,.38),m.stone,'submerged-shelf',[0,-.17,0],[-PI/2,.15,0],'stone');break;
 case 11:{const wall=add(g,bowl(1.38,1.1,.8,.18),m.stone,'basin-boundary',undefined,[-PI/2,0,0],'stone');waterPlane(2.3,1.75,.2,.03);break;}
 case 12:add(g,volumeTube([[-.97,-.6,0],[-.88,.12,.05],[-.56,.68,.09],[.16,.9,0],[.81,.33,.07],[1,-.6,0]],.16),m.stone,'root-arch',undefined,undefined,'environment');for(let i=0;i<4;i++)add(g,volumeTube([[-.9+i*.12,-.4,.02],[-1.05+i*.24,-.69,.15],[-1.2+i*.31,-.74,.28]],.05),m.stone,'rootlet-'+i,undefined,undefined,'environment');break;
 case 13:fern(g,m,[0,-.9,0]);break;
 case 14:for(let i=0;i<5;i++)fern(g,m,[.05*(i-2),-.8,.1*Math.sin(i)],1.3+seedRand(i)*.5,(i-2)*.38,'frond-'+i);break;
 case 15:stone(0,-.2,0,.9,.3,.65,5);for(let i=0;i<90;i++){const a=i*2.399,r=Math.sqrt((i+.5)/90);const x=Math.cos(a)*r*.85,z=Math.sin(a)*r*.6,y=.12+.15*(1-r*r);add(g,sphere(.055,.07+seedRand(i)*.055,.055),m.leaf,'moss-tip-'+i,[x,y,z],undefined,'leaf');}break;
 case 16:for(let i=0;i<3;i++){const x=(i-1)*.18,h=1.4+i*.18;add(g,volumeTube([[x,-.9,0],[x+.06,0,.03],[x+.15,h-.9,.05]],.025),m.leaf,'reed-stem-'+i,undefined,undefined,'leaf');add(g,sphere(.055,.22,.045,.6),m.nacre,'reed-head-'+i,[x+.15,h-.85,.05],undefined,'leaf');add(g,leafGeometry(.8,.12,.05),m.leaf,'reed-blade-'+i,[x+.12,-.05,.07],[0,0,-.3],'leaf');}break;
 case 17:add(g,leafGeometry(1.8,.62,.15),m.leaf,'floating-leaf',undefined,[-PI/2,0,.2],'leaf');add(g,volumeTube([[0,0,-.8],[0,.07,0],[0,.12,.8]],.025),m.nacre,'leaf-midvein',undefined,undefined,'leaf');break;
 case 18:add(g,leafGeometry(1.8,.66,.18),m.leaf,'dew-leaf',undefined,[-.95,.12,.25],'leaf');for(let i=0;i<7;i++){const x=(seedRand(i)-.5)*.54,y=(seedRand(i+7)-.5)*1.15;const r=.045+seedRand(i+30)*.055;add(g,sphere(r,r,r),m.water,'dew-drop-'+i,[x,y*.6,.19+y*.65],undefined,'water');}break;
 case 19:case 20:{const dims=n===19?[2.6,.7,1.6]:[3.6,.4,.85];const volume=add(g,new THREE.BoxGeometry(...dims),m.shell,'density-domain',[0,.05,0],undefined,'mist');volume.visible=false;volume.userData={role:'mist',volumeDomain:true,density:.18,bounds:dims,windCoupled:true};g.userData.volumeDomain={kind:'mist',dimensions:dims,density:.18};break;}
 case 21:for(let i=0;i<6;i++){const a=i/6*PI*2,xx=Math.cos(a)*1.8,zz=Math.sin(a)*.65;fern(g,m,[xx,-1,zz],2.5+seedRand(i)*.5,(i-2.5)*.17,'canopy-frond-'+i);}break;
 case 22:stone(0,-.22,0,1.2,.18,.8,9);for(let i=0;i<34;i++){const a=i*2.399,r=Math.sqrt((i+.5)/34),x=Math.cos(a)*r*1.1,z=Math.sin(a)*r*.69,h=.1+seedRand(i)*.23;add(g,volumeTube([[x,-.1,z],[x+.035,h,z+.015]],.012),m.leaf,'luminous-stem-'+i,undefined,undefined,'leaf');add(g,sphere(.038,.063,.038),m.glow,'bioluminescent-tip-'+i,[x+.035,h,z+.015],undefined,'light');}break;
 case 23:add(g,volumeTube([[-1,0,0],[0,.08,-.08],[1,0,0]],.28),m.stone,'waterfall-lip',undefined,undefined,'stone');add(g,paramSheet(1.9,1.5,.08,(u,v)=>[u*.95,v*.75-.67,.12+.1*Math.sin(u*6+v)]),m.water,'falling-sheet',undefined,undefined,'water');break;
 case 24:{const key=new THREE.DirectionalLight(0xe4fff4,3.8);key.position.set(-2,4,3);key.name='cool-canopy-key';key.lookAt(0,0,0);key.target.position.set(0,0,-1);key.add(key.target);key.castShadow=true;key.shadow.mapSize.set(2048,2048);g.add(key);const fill=new THREE.DirectionalLight(0xeacdde,1.8);fill.position.set(3,1,-2);fill.name='opal-fill';fill.lookAt(0,0,0);fill.target.position.set(0,0,-1);fill.add(fill.target);g.add(fill);const hemi=new THREE.HemisphereLight(0xd5f3ed,0x182b27,1.2);hemi.name='environment-hemisphere';g.add(hemi);g.userData.lightRig={hemisphere:{sky:'#d5f3ed',ground:'#182b27',intensity:1.2},key:{color:'#e4fff4',intensity:3.8},fill:{color:'#eacdde',intensity:1.8}};break;}
 }
 g.userData.environment=true;
}

function leafOutline(){
 const s=new THREE.Shape();s.moveTo(0,-1.45);s.bezierCurveTo(-.54,-1.04,-.89,-.28,-.65,.52);s.bezierCurveTo(-.52,.99,-.14,1.29,0,1.47);s.bezierCurveTo(.18,1.25,.84,.65,.72,-.17);s.bezierCurveTo(.63,-.86,.14,-1.28,0,-1.45);return s;
}
function extrudeForm(shape,depth=.3,z=0,bevel=.05){
 const g=new THREE.ExtrudeGeometry(shape,{depth:depth-2*bevel,bevelEnabled:bevel>0,bevelThickness:bevel,bevelSize:bevel,bevelSegments:5,steps:2,curveSegments:24});g.translate(0,0,z-depth/2+bevel);return g;
}
function leafTrayGeometry(){
 const holes=[{w:.55,h:.56,x:-.015,y:-.72,r:.19},{w:1.0,h:.57,x:.015,y:-.03,r:.21},{w:.69,h:.59,x:.025,y:.66,r:.21}],positions=[],uvs=[];
 const loop=shape=>{const p=shape.getPoints(24);if(p[0].distanceTo(p[p.length-1])<1e-8)p.pop();return p;};
 const orient=(points,ccw=true)=>{let area=0;for(let i=0;i<points.length;i++){const a=points[i],b=points[(i+1)%points.length];area+=a.x*b.y-b.x*a.y;}return (area>0)===ccw?points:points.reverse();};
 const outer=orient(loop(leafOutline()));
 const cavities=holes.map(h=>orient(loop(roundedShape(h.w,h.h,h.r)).map(p=>new THREE.Vector2(p.x+h.x,p.y+h.y)),false));
 const tri=(a,b,c)=>{for(const p of[a,b,c]){positions.push(...p);uvs.push(p[0]/3+.5,p[1]/3+.5);}};
 const ring3=(points,z,scale=1,center=[0,0])=>points.map(p=>[center[0]+(p.x-center[0])*scale,center[1]+(p.y-center[1])*scale,z]);
 const bridge=(a,b)=>{for(let i=0;i<a.length;i++){const j=(i+1)%a.length;tri(a[i],b[i],a[j]);tri(a[j],b[i],b[j]);}};
 const cap=(contour,holes2,z,up=true)=>{const all=[...contour,...holes2.flat()];for(const [a,b,c]of THREE.ShapeUtils.triangulateShape(contour,holes2)){const va=[all[a].x,all[a].y,z],vb=[all[b].x,all[b].y,z],vc=[all[c].x,all[c].y,z];const cross=(vb[0]-va[0])*(vc[1]-va[1])-(vb[1]-va[1])*(vc[0]-va[0]);if((cross>0)===up)tri(va,vb,vc);else tri(va,vc,vb);}};
 const topOuter=outer.map(p=>p.clone().multiplyScalar(.96));cap(topOuter,cavities,.18,true);
 const outerRings=[ring3(outer,.18,.96),ring3(outer,.12,1),ring3(outer,-.15,.985),ring3(outer,-.22,.94)];for(let i=0;i<outerRings.length-1;i++)bridge(outerRings[i],outerRings[i+1]);
 cap(outer.map(p=>p.clone().multiplyScalar(.94)),[],-.22,false);
 for(let k=0;k<holes.length;k++){const h=holes[k],p=cavities[k],center=[h.x,h.y],r=[ring3(p,.18,1,center),ring3(p,.11,.94,center),ring3(p,-.055,.84,center),ring3(p,-.10,.75,center)];for(let i=0;i<r.length-1;i++)bridge(r[i],r[i+1]);const floor=p.map(v=>new THREE.Vector2(h.x+(v.x-h.x)*.75,h.y+(v.y-h.y)*.75));cap(orient(floor,true),[],-.10,true);}
 const geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.Float32BufferAttribute(positions,3));geometry.setAttribute('uv',new THREE.Float32BufferAttribute(uvs,2));smoothNormals(geometry);geometry.userData={singleClosedSolid:true,recessCount:3,recessDepth:.28};return {geometry,holes};
}
function nature(g,n,m){
 switch(n){
 case 1:{const seed=add(g,sphere(.67,.91,.34,.83,(x,y,z,u,v,w)=>[x*(.82-.28*v),y,z*(.89-.13*v)]),m.gel,'seed-button',undefined,[0,0,-.17],'body');add(g,volumeTube([[-.14,-.73,.14],[-.19,-.28,.32],[-.09,.32,.28],[0,.78,.08]],.018),m.nacre,'seed-seam');add(g,sphere(.07,.06,.033),m.glow,'seed-status-dew',[.2,-.32,.32],undefined,'light');g.userData.control='press';break;}
 case 2:{const tray=leafTrayGeometry();add(g,tray.geometry,m.gel,'leaf-tray-body');for(let i=0;i<tray.holes.length;i++){const h=tray.holes[i],socket=new THREE.Object3D();socket.name='compartment-'+i;socket.position.set(h.x,h.y,-.1);socket.userData={role:'socket',portType:'contentFrame',dimensions:[h.w,h.h,.2]};g.add(socket);}add(g,volumeTube([[0,-1.37,-.07],[-.04,-1.61,-.08],[.09,-1.83,-.1]],.057),m.gel,'leaf-tray-stem',undefined,undefined,'connector');g.userData.compartments=tray.holes.map((h,i)=>({id:i,center:[h.x,h.y,-.1],clearWidth:h.w-.09,clearHeight:h.h-.09,depth:.28}));break;}
 case 3:add(g,sphere(.76,.69,.36,1,(x,y,z,u,v,w)=>[x*(1+.04*v),y,z+.023*u]),m.water,'dew-lens',undefined,undefined,'water');add(g,ring(.64,.57,.047),m.nacre,'dew-lens-seat',[0,0,-.26],undefined,'guide');add(g,volumeTube([[-.53,-.28,-.2],[-.38,-.48,-.27],[-.13,-.57,-.24]],.035),m.gel,'lens-meniscus-root',undefined,undefined,'connector');g.userData.control='press';break;
 case 4:{const p=[[-1.1,-.37,-.05],[-.63,-.38,.13],[-.31,.18,.14],[.16,.41,.03],[.56,-.02,.13],[.98,.15,.02]];add(g,volumeTube(p,.085),m.gel,'tendril-main',undefined,undefined,'connector');add(g,volumeTube([[-.33,.15,.14],[-.64,.55,.08],[-.46,.82,.06],[-.24,.69,.08]],.043),m.blue,'tendril-curl',undefined,undefined,'connector');add(g,volumeTube([[.51,0,.13],[.65,-.45,.16],[.94,-.55,.07]],.04),m.gel,'tendril-branch',undefined,undefined,'connector');for(const [i,pos]of [[0,p[0]],[1,p[p.length-1]]])dockSocket(g,m,...pos,i);g.userData.flexibleSpans=['tendril-main','tendril-curl','tendril-branch'];break;}
 case 5:{const frond=fern(g,{...m,leaf:m.shell},[-.12,-.98,0],1.93,-.08,'glass-frond');add(g,sphere(.19,.16,.13),m.gel,'frond-root',[-.12,-.98,0]);for(let i=0;i<7;i++)add(g,sphere(.022,.026,.018),m.glow,'frond-lumen-'+i,[-.11+.21*(i/6)**2,-.84+i*.25,.105],undefined,'light');g.userData.windArticulation=true;break;}
 case 6:{add(g,hollowVolume(sphere(.43,.46,.34,.83),sphere(.365,.395,.275,.83)),m.shell,'spore-shell',[0,.39,0],undefined,'film');add(g,sphere(.19,.21,.155),m.glow,'spore-luminous-core',[0,.39,0],undefined,'light');const tail=add(g,volumeTube([[0,-.02,0],[-.12,-.36,.08],[.14,-.68,.16],[.03,-.96,.02]],.035),m.gel,'spore-flexible-tail',undefined,undefined,'connector');tail.userData.motion={kind:'wind',stiffness:.2,phase:.4};for(let i=0;i<3;i++){const a=(i-1)*.7;add(g,leafGeometry(.5,.13,.05),m.shell,'spore-fin-'+i,[Math.sin(a)*.33,.46,.08],[0,a*.15,-a],'leaf');}break;}
 case 7:{const panel=add(g,hollowVolume(plate(2.15,2.35,.23,.41,.08),plate(2.015,2.215,.115,.35,.043)),m.shell,'water-root-panel-shell');add(g,plate(1.91,2.11,.065,.31,.025),m.water,'water-root-panel-fluid',undefined,undefined,'water');for(let i=0;i<5;i++){const x=(i-2)*.37,p=[[x,-1.10,0],[x*.92,-1.39,.02],[x*1.14+.07*Math.sin(i),-1.69,.06],[x*1.25,-1.81+.12*Math.sin(i),.08]];const root=add(g,volumeTube(p,.026+(i===2?.018:0)),m.blue,'panel-root-'+i,undefined,undefined,'connector');root.userData.motion={kind:'wind',stiffness:.35,phase:i*.49};add(g,sphere(.049,.08,.045),m.water,'root-dew-'+i,p[p.length-1],undefined,'water');}add(g,volumeTube([[-1.02,-.9,0],[-1.29,-.52,.06],[-1.30,.28,.04],[-1.05,.54,0]],.035),m.gel,'side-root-tendril',undefined,undefined,'connector');g.userData.stableContentFrame={center:[0,0,.13],size:[1.74,1.92]};break;}
 }
 g.userData.natureGeometry=true;
}

const CONTROL_KINDS={1:'slider',2:'arc-slider',3:'fader',4:'range',5:'stepped-slider',6:'ribbon-slider',7:'fluid-slider',8:'floating-slider',9:'toggle',10:'toggle',11:'rocker',12:'selector',13:'dial',14:'dial',15:'dual-dial',16:'arc-dial',17:'ring-selector',18:'trackball',19:'joystick',20:'pressure',21:'xy',22:'wheel',23:'handle',24:'iris'};

/** Author a physical module. Dimensions are final bounding-box extents, not morphology.
 * @param {string} id Catalogue ID in A–F/J/N.
 * @param {Object<string,THREE.Material>} materials shared material instances.
 * @param {Object} options {dimensions:[w,h,d], scale:number, center:boolean}
 */
export function createElement(id,materials={},options={}) {
 const spec=CATALOGUE[id];if(!spec)throw new RangeError(`Unknown physical element ${id}. Physical families are A–F, J and N.`);
 const group=new THREE.Group();group.name=id+' '+spec.name;
 group.userData={id,name:spec.name,kind:spec.name,family:id[0],control:id[0]==='B'?CONTROL_KINDS[+id.slice(1)]:id[0]==='A'?'press':null,geometryStatus:'authored',geometryDescription:spec.geometry,ports:{},coordinateSystem:id[0]==='J'?'Y-up environment':'XY front, +Z face'};
 const mats=materialSet(materials),n=+id.slice(1);
 ({A:gelBody,B:precision,C:containers,D:assemblies,E:liquid,F:films,J:environment,N:nature}[id[0]])(group,n,mats);
 group.updateMatrixWorld(true);
 let bounds=new THREE.Box3().setFromObject(group);
 if(bounds.isEmpty())bounds.set(V(-1,-1,-1),V(1,1,1));
 const size=bounds.getSize(V()),center=bounds.getCenter(V());
 if(options.center===true){for(const child of group.children)child.position.sub(center);group.updateMatrixWorld(true);bounds=new THREE.Box3().setFromObject(group);}
 if(options.dimensions)group.scale.multiply(new THREE.Vector3(...options.dimensions.map((v,i)=>v/Math.max(size.getComponent(i),.001))));
 if(options.scale)group.scale.multiplyScalar(options.scale);
 group.updateMatrixWorld(true);bounds=new THREE.Box3().setFromObject(group);if(bounds.isEmpty())bounds.set(V(-1,-1,-1),V(1,1,1));
 const s=bounds.getSize(V()),c=bounds.getCenter(V());
 group.userData.bounds={min:bounds.min.toArray(),max:bounds.max.toArray(),size:s.toArray(),center:c.toArray()};
 for(const type of spec.connections)group.userData.ports[type]={type,position:type==='contentFrame'?[0,0,bounds.max.z]:type==='mount'?[0,0,bounds.min.z]:c.toArray(),normal:[0,0,type==='mount'?-1:1]};
 group.traverse(o=>{if(o.isMesh){o.geometry.computeBoundingSphere();o.userData.elementId=id;if(o.userData.deformable)o.userData.deformationSpace='local';}});
 return group;
}
export const listGeometryIds=()=>Object.keys(CATALOGUE);
export const geometryCatalogue=CATALOGUE;
