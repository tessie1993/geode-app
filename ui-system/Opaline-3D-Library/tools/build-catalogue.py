#!/usr/bin/env python3
"""Rebuild reusable compositions, design tokens and the actual-file asset manifest.

No network, no external Python dependencies. Existing elements.json is authoritative
for implementation status; this script never promotes a planned target to shipped.
"""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'catalogue'
OUT.mkdir(exist_ok=True)

def write(name, value):
    (OUT / name).write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n')

def part(id, element, xyz=(0,0,0), dimensions=(1,1,.24), material='gel', rotation=(0,0,0), role='body'):
    return {'id':id, 'element':element, 'position':list(xyz), 'rotation':list(rotation),
            'scale':[1,1,1], 'dimensions':list(dimensions), 'material':material, 'role':role,
            'bindings':[{'event':'pointerdown','action':'press','target':id},
                        {'event':'pointerup','action':'release','target':id},
                        {'event':'pointercancel','action':'release','target':id}]}

def frame(id='content', xyz=(0,0,.18), wh=(1,.6), role='content', owner='body'):
    return {'id':id,'owner':owner,'position':list(xyz),'rotation':[0,0,0],
            'size':list(wh),'padding':.08,'role':role,'renderedContent':None,
            'attachment':'rigid-readable-plane','normal':[0,0,1]}

RECIPES=[]
def add(id,name,category,parts,frames=None,meaning='',bindings=None,dimensions=None):
    RECIPES.append({'id':id,'name':name,'category':category,'version':'3.0.0',
      'description':meaning,'textFree':True,'coordinateSystem':'metres, +Y up, +Z front; Euler XYZ radians',
      'dimensions':dimensions or [3,2,.6], 'parts':parts,'contentFrames':frames or [],
      'behaviorBindings':bindings or [],
      'implementation':{'geometry':'instantiable-composition','interaction':'shared pointer press/release; semantic behavior bindings are declarative integration contracts',
                        'semanticUI':'host adapter required; no keyboard, IME, form validation or navigation is implemented by the recipe'},
      'assemblyRules':{'sharedMaterialSet':True,'sharedLighting':True,'rigidReadableContent':True,
                       'onePointerOwnerPerPart':True,'cancelReleasesContact':True,'decorationsDoNotCapturePointer':True}})

def bind(event,action,target='body',**params):
    return {'event':event,'action':action,'target':target,'parameters':params,'execution':'host-adapter-contract'}

def rows(n,width=2.3,height=.35,gap=.1,z=.2,body='A05',prefix='row'):
    return [part(f'{prefix}-{i}',body,(0,((n-1)/2-i)*(height+gap),z),(width,height,.16)) for i in range(n)]

def menu(id,name,n=4,element='C03',width=2.6,height=2.5,category='menus'):
    p=[part('body',element,dimensions=(width,height,.3),material='shell')]+rows(n,width-.35,min(.35,(height-.4)/n-.08),.08)
    fs=[frame(f'item-{i}',q['position'][:2]+[.31],(width-.7,q['dimensions'][1]-.08),'menu-item',q['id']) for i,q in enumerate(p[1:])]
    add(id,name,category,p,fs,'Volumetric menu shell with separately hit-tested action bodies.',[bind('activate','select','row-*')],[width,height,.6])

def panel(id,name,element='C01',w=2.8,h=2,footer=False,category='containers'):
    p=[part('body',element,dimensions=(w,h,.28),material='shell')]
    f=[frame('content',(0,.12 if footer else 0,.18),(w-.36,h-.65 if footer else h-.36))]
    if footer:
        p += [part('secondary','A05',(-w*.24,-h*.32,.23),(w*.37,.35,.18),'nacre'),part('primary','A05',(w*.24,-h*.32,.23),(w*.37,.35,.18))]
        f += [frame('secondary-content',(-w*.24,-h*.32,.34),(w*.29,.18),'action','secondary'),frame('primary-content',(w*.24,-h*.32,.34),(w*.29,.18),'action','primary')]
    add(id,name,category,p,f,'A thick shell keeps content readable on a separate front-plane attachment.',[bind('open','lift'),bind('close','return-to-mount')],[w,h,.6])

# Each record is a distinct assembly role, not a theme or camera-view count.
add('UI001','Primary action pebble','actions',[part('body','A01',dimensions=(1.45,.65,.32))],[frame(wh=(1.02,.27),xyz=(0,0,.23))], 'Single broad action body with a stable content inset.',[bind('activate','commit')])
add('UI002','Secondary action lens','actions',[part('body','A03',dimensions=(1.45,.65,.25),material='nacre')],[frame(wh=(1.02,.27),xyz=(0,0,.2))],bindings=[bind('activate','commit')])
add('UI003','Circular action','actions',[part('body','A22',dimensions=(.72,.72,.32))],[frame(wh=(.3,.3),xyz=(0,0,.23),role='icon')],bindings=[bind('activate','commit')])
add('UI004','Split action','actions',[part('body','A23',dimensions=(1.8,.68,.3))],[frame('main',(-.31,0,.22),(.8,.25)),frame('secondary',(.59,0,.22),(.25,.25),'icon')],bindings=[bind('activate','commit','main'),bind('activate','open-menu','secondary')])
add('UI005','Floating action cluster','actions',[part('body','A01',(0,0,.1),(.8,.8,.35)),part('satellite-a','A03',(.8,.3,0),(.48,.48,.24)),part('satellite-b','A03',(.35,.92,-.08),(.48,.48,.24)),part('satellite-c','A03',(-.45,.78,-.12),(.48,.48,.24))],[frame(xyz=(0,0,.34),wh=(.32,.32),role='icon')],bindings=[bind('activate','expand-radial')])
add('UI006','Checkbox well','selection',[part('body','C13',dimensions=(.68,.68,.18),material='shell'),part('selection','A04',(0,0,.13),(.36,.36,.18))],meaning='Selection is a real inset body; the host controls checked state.',bindings=[bind('activate','toggle-selection','selection')])
add('UI007','Radio well','selection',[part('body','C20',dimensions=(.65,.65,.18),material='shell'),part('selection','A03',(0,0,.12),(.3,.3,.18))],bindings=[bind('activate','select-exclusive','selection')])
add('UI008','Capsule switch','selection',[part('body','B09',dimensions=(1.3,.55,.35))],bindings=[bind('drag','bounded-axis',axis='x',range=[0,1],stops=[0,1])])
add('UI009','Three-way selector','selection',[part('body','B12',dimensions=(1.8,.52,.32))],[frame('choice-a',(-.57,0,.21),(.38,.2)),frame('choice-b',(0,0,.21),(.38,.2)),frame('choice-c',(.57,0,.21),(.38,.2))],bindings=[bind('drag','bounded-axis',axis='x',range=[0,1],stops=[0,.5,1])])
add('UI010','Filter chip row','selection',[part('body','D02',dimensions=(2.9,.6,.18),material='shell')]+[part(f'chip-{i}','A05',((i-1)*.91,0,.18),(.81,.39,.22)) for i in range(3)],[frame(f'chip-{i}',((i-1)*.91,0,.32),(.6,.18),'content',f'chip-{i}') for i in range(3)],bindings=[bind('activate','toggle-filter','chip-*')])
add('UI011','Single-line input','text-entry',[part('body','C02',dimensions=(2.9,.62,.24),material='shell'),part('focus-rail','D14',(0,-.23,.16),(2.5,.035,.035),'glow')],[frame('input',(0,0,.17),(2.45,.28),'editable-text')], 'Empty input shell. Host supplies a native editable surface and IME.',[bind('focus','illuminate','focus-rail'),bind('blur','settle','focus-rail')])
add('UI012','Text area','text-entry',[part('body','C01',dimensions=(2.9,1.7,.28),material='shell'),part('grip','B23',(1.2,-.59,.18),(.26,.15,.12),'nacre')],[frame('input',(0,.05,.2),(2.42,1.13),'multiline-editable-text')],bindings=[bind('focus','illuminate'),bind('drag','resize','grip',minimum=[1.8,1.1])])
add('UI013','Search field','text-entry',[part('body','A05',dimensions=(3,.65,.27),material='shell'),part('leading','C20',(-1.14,0,.18),(.25,.25,.08),'nacre'),part('clear','A22',(1.2,0,.18),(.25,.25,.14),'nacre')],[frame('query',(.04,0,.2),(1.95,.27),'search-query')],bindings=[bind('focus','lift'),bind('activate','clear','clear')])
add('UI014','Secure-entry field','text-entry',[part('body','C02',dimensions=(2.9,.64,.24),material='shell'),part('visibility','A03',(1.13,0,.18),(.34,.28,.15),'nacre')],[frame('input',(-.17,0,.18),(2.1,.28),'secure-editable-text')],bindings=[bind('activate','toggle-obscure','visibility')])
add('UI015','Search with suggestions','text-entry',[part('body','C03',(0,-.55,-.05),(3,2,.24),'shell'),part('search','A05',(0,.52,.18),(2.7,.48,.24))]+[part(f'row-{i}','A05',(0,.05-i*.4,.14),(2.5,.3,.17)) for i in range(3)],[frame('query',(0,.52,.34),(2.35,.21),'search-query','search')]+[frame(f'result-{i}',(0,.05-i*.4,.27),(2.1,.18),'suggestion',f'row-{i}') for i in range(3)],bindings=[bind('input','filter-results','search'),bind('activate','select-result','row-*')])
add('UI016','Number stepper','text-entry',[part('body','C02',dimensions=(2.1,.66,.22),material='shell'),part('decrease','A03',(-.76,0,.17),(.46,.46,.25)),part('increase','A03',(.76,0,.17),(.46,.46,.25))],[frame('value',(0,0,.19),(.75,.3),'numeric-value')],bindings=[bind('activate','increment','increase',step=1),bind('activate','increment','decrease',step=-1)])
add('UI017','Date field','text-entry',[part('body','C02',dimensions=(2.9,.66,.23),material='shell'),part('picker','A04',(1.13,0,.18),(.36,.36,.2))],[frame('value',(-.2,0,.18),(2.08,.27),'date-value')],bindings=[bind('activate','open-date-picker','picker')])
add('UI018','Tag entry','text-entry',[part('body','C01',dimensions=(2.9,1.12,.24),material='shell')]+[part(f'tag-{i}','A05',(-.79+i*.84,.2,.17),(.72,.32,.18)) for i in range(3)],[frame('input',(0,-.21,.18),(2.44,.25),'editable-text')],bindings=[bind('commit','append-tag'),bind('remove','detach-tag','tag-*')])
add('UI019','Horizontal slider','continuous-controls',[part('body','B01',dimensions=(2.8,.5,.36))],bindings=[bind('drag','bounded-axis',axis='x',range=[0,1])])
add('UI020','Vertical fader','continuous-controls',[part('body','B03',dimensions=(.56,2.4,.38))],bindings=[bind('drag','bounded-axis',axis='y',range=[0,1])])
add('UI021','Range slider','continuous-controls',[part('body','B04',dimensions=(2.8,.52,.35))],bindings=[bind('drag','ordered-range',axis='x',range=[0,1],crossing='clamp')])
add('UI022','Stepped slider','continuous-controls',[part('body','B05',dimensions=(2.8,.53,.35))],bindings=[bind('drag','bounded-axis',axis='x',range=[0,1],stops=[0,.25,.5,.75,1])])
add('UI023','Circular dial','continuous-controls',[part('body','B13',dimensions=(1.3,1.3,.45))],[frame('value',(0,0,.28),(.52,.4),'numeric-value')],bindings=[bind('drag','angular-value',range=[0,1],sweepRadians=4.712)])
add('UI024','Concentric dual dial','continuous-controls',[part('body','B15',dimensions=(1.5,1.5,.4))],[frame('value',(0,0,.24),(.55,.35),'numeric-value')],bindings=[bind('drag','dual-angular-value',range=[0,1])])
add('UI025','XY control','continuous-controls',[part('body','B21',dimensions=(1.8,1.5,.4))],bindings=[bind('drag','bounded-plane',axes=['x','y'],range=[[0,1],[0,1]])])
add('UI026','Pressure surface','continuous-controls',[part('body','B20',dimensions=(1.6,1.2,.33))],bindings=[bind('pressurechange','bounded-pressure',range=[0,1])])
menu('UI027','Context menu',4,width=2.3,height=2.1)
menu('UI028','Dropdown menu',5,width=2.7,height=2.55)
menu('UI029','Compact popup menu',3,width=2,height=1.65)
menu('UI030','Navigation side panel',6,width=2.1,height=3.2)
menu('UI031','Action sheet',4,width=3.3,height=2.25)
add('UI032','Radial context menu','menus',[part('body','D05',dimensions=(2.3,2.3,.26),material='shell')]+[part(f'action-{i}','A01',(x,y,.2),(.65,.65,.32)) for i,(x,y) in enumerate([(0,1),(1,0),(0,-1),(-1,0)])],bindings=[bind('activate','select-radial','action-*')])
add('UI033','Nested submenu','menus',[part('body','C03',(-.85,0,0),(1.65,2.25,.23),'shell'),part('submenu','C03',(.85,.22,.18),(1.65,1.8,.23),'shell')]+[part(f'row-{i}','A05',(-.85,.65-i*.43,.17),(1.4,.32,.17)) for i in range(4)]+[part(f'child-{i}','A05',(.85,.66-i*.43,.35),(1.4,.32,.17)) for i in range(3)],bindings=[bind('hover','open-submenu','row-*'),bind('activate','select','child-*')])
add('UI034','Bottom navigation dock','navigation',[part('body','D03',dimensions=(3.4,.82,.32),material='shell')]+[part(f'destination-{i}','A03',((i-2)*.61,.04,.23),(.44,.44,.23)) for i in range(5)],[frame(f'icon-{i}',((i-2)*.61,.04,.39),(.23,.23),'icon',f'destination-{i}') for i in range(5)],bindings=[bind('activate','navigate','destination-*')])
add('UI035','Tab rail','navigation',[part('body','D02',dimensions=(3,.54,.19),material='shell')]+[part(f'tab-{i}','A05',((i-1)*.94,0,.15),(.85,.37,.17)) for i in range(3)],[frame(f'tab-{i}',((i-1)*.94,0,.28),(.61,.17),'tab-content',f'tab-{i}') for i in range(3)],bindings=[bind('activate','select-tab','tab-*')])
add('UI036','Breadcrumb path','navigation',[part('body','D10',dimensions=(3,.4,.15),material='nacre')]+[part(f'crumb-{i}','A05',((i-1)*.98,0,.14),(.74,.34,.2)) for i in range(3)],bindings=[bind('activate','navigate-ancestor','crumb-*')])
add('UI037','Pagination beads','navigation',[part('body','D10',dimensions=(2.1,.3,.13),material='shell')]+[part(f'page-{i}','A03',((i-2)*.4,0,.1),(.17,.17,.13),'glow' if i==2 else 'nacre') for i in range(5)],bindings=[bind('activate','select-page','page-*')])
add('UI038','Vertical navigation rail','navigation',[part('body','D07',dimensions=(.74,3.1,.24),material='shell')]+[part(f'destination-{i}','A03',(0,1.05-i*.7,.22),(.47,.47,.26)) for i in range(4)],bindings=[bind('activate','navigate','destination-*')])
panel('UI039','Content card',w=2.7,h=1.9)
panel('UI040','Wide content card','C02',3.5,1.6)
panel('UI041','Portrait content card','C03',2.3,3.1)
panel('UI042','Dialog shell',w=3.2,h=2.3,footer=True)
panel('UI043','Full-height sheet','C03',3.2,4.1,footer=True)
panel('UI044','Bottom sheet','C01',3.4,2.65,footer=True)
panel('UI045','Popover shell','C05',2.5,1.8)
panel('UI046','Tooltip shell','C06',1.9,.75)
add('UI047','Toast shell','feedback',[part('body','C07',dimensions=(3,.65,.26),material='shell'),part('status','A03',(-1.16,0,.18),(.28,.28,.17),'glow')],[frame('message',(.18,0,.18),(2.15,.27),'message')],bindings=[bind('show','lift'),bind('timeout','return-to-mount')])
add('UI048','Inline validation shell','feedback',[part('body','C04',dimensions=(2.8,.44,.18),material='shell'),part('status','A03',(-1.16,0,.13),(.2,.2,.12),'glow')],[frame('message',(.13,0,.14),(2.16,.2),'message')],bindings=[bind('validationchange','set-status')])
add('UI049','Linear progress channel','feedback',[part('body','E12',dimensions=(2.9,.39,.3),material='shell')],bindings=[bind('progress','set-fill',range=[0,1])])
add('UI050','Circular progress ring','feedback',[part('body','E14',dimensions=(1.3,1.3,.24),material='water')],[frame('progress',(0,0,.16),(.48,.34),'numeric-value')],bindings=[bind('progress','set-arc',range=[0,1])])
add('UI051','Orbit loading assembly','feedback',[part('body','D08',dimensions=(1.5,1.5,.32),material='gel')],bindings=[bind('pending','orbit'),bind('resolve','settle')])
add('UI052','Skeleton content shell','feedback',[part('body','C01',dimensions=(2.8,2,.24),material='shell')]+[part(f'skeleton-{i}','A05',(0,.54-i*.48,.16),(2.15-i*.3,.2,.12),'nacre') for i in range(3)],bindings=[bind('pending','soft-pulse','skeleton-*'),bind('resolve','content-reveal')])
add('UI053','Status badge','feedback',[part('body','A05',dimensions=(1.02,.44,.2),material='nacre'),part('signal','A03',(-.3,0,.14),(.15,.15,.13),'glow')],[frame('status',(.14,0,.16),(.45,.17),'status')],bindings=[bind('statuschange','set-status','signal')])
add('UI054','Notification stack','feedback',[part('body','C15',dimensions=(2.8,1.5,.9),material='shell')],[frame('front-message',(0,0,.53),(2.25,.73),'message')],bindings=[bind('activate','depth-fan-expand'),bind('dismiss','depth-fan-collapse')])
add('UI055','Accordion stack','content-collections',[part('body','C03',dimensions=(3,2.8,.25),material='shell')]+[part(f'section-{i}','C04',(0,.9-i*.62,.2),(2.7,.44,.22)) for i in range(3)],[frame(f'header-{i}',(0,.9-i*.62,.35),(2.25,.21),'section-header',f'section-{i}') for i in range(3)],bindings=[bind('activate','toggle-expansion','section-*')])
add('UI056','Expandable section','content-collections',[part('body','C16',dimensions=(3,2.15,.35),material='shell')],[frame('header',(0,.63,.25),(2.5,.32),'section-header'),frame('content',(0,-.34,.25),(2.5,.78))],bindings=[bind('activate','hinge-expand')])
add('UI057','Selectable list','content-collections',[part('body','C03',dimensions=(3,2.75,.24),material='shell')]+rows(5,2.65,.34,.11,.18),[frame(f'item-{i}',(0,.9-i*.45,.3),(2.2,.18),'list-item',f'row-{i}') for i in range(5)],bindings=[bind('activate','select','row-*')])
add('UI058','Grid of cards','content-collections',[part(f'card-{i}','C01',(x,y,.08*(i%2)),(1.28,.98,.22),'shell') for i,(x,y) in enumerate([(-.72,.58),(.72,.58),(-.72,-.58),(.72,-.58)])],[frame(f'content-{i}',(x,y,.22+.08*(i%2)),(.96,.63),'content',f'card-{i}') for i,(x,y) in enumerate([(-.72,.58),(.72,.58),(-.72,-.58),(.72,-.58)])],bindings=[bind('activate','select','card-*')])
add('UI059','Depth carousel','content-collections',[part(f'card-{i}','C03',(x,0,z),(1.5,2,.24),'shell',(0,ry,0)) for i,(x,z,ry) in enumerate([(-1.1,-.42,-.35),(0,.2,0),(1.1,-.42,.35)])],bindings=[bind('swipe','orbit-reconfigure','card-*')])
add('UI060','Image frame','content-collections',[part('body','C19',dimensions=(2.8,2,.3),material='shell')],[frame('image',(0,0,.08),(2.3,1.5),'image-slot')], 'Open geometry around a media plane; artwork is supplied separately.',[bind('activate','lift')])
add('UI061','Circular image frame','content-collections',[part('body','C20',dimensions=(1.7,1.7,.29),material='shell')],[frame('image',(0,0,.09),(1.1,1.1),'circular-image-slot')],bindings=[bind('activate','lift')])
add('UI062','Tree branch navigation','navigation',[part('body','D06',dimensions=(3,2.2,.35),material='shell')]+[part(f'node-{i}','A01',(x,y,.2),(.65,.5,.27)) for i,(x,y) in enumerate([(0,.83),(-1,-.2),(1,-.2),(0,-.85)])],bindings=[bind('activate','navigate-node','node-*')])
add('UI063','Colour picker','pickers',[part('body','C08',dimensions=(2,2,.35),material='shell'),part('hue','B17',dimensions=(1.7,1.7,.25),material='gel'),part('plane','B21',(0,0,.2),(.83,.83,.2),'pigment')],bindings=[bind('drag','select-hue','hue'),bind('drag','select-saturation-value','plane')])
add('UI064','Date grid picker','pickers',[part('body','C01',dimensions=(3.3,3.4,.25),material='shell')]+[part(f'day-{i}','A04',((i%7-3)*.43,1-(i//7)*.47,.17),(.33,.34,.18)) for i in range(35)], [frame('month',(0,1.43,.19),(2.5,.22),'month-header')],bindings=[bind('activate','select-date','day-*')])
add('UI065','Time wheel picker','pickers',[part('body','C01',dimensions=(2.5,2,.26),material='shell'),part('hours','B22',(-.58,0,.2),(.64,1.45,.45)),part('minutes','B22',(.58,0,.2),(.64,1.45,.45))],bindings=[bind('drag','select-hour','hours',range=[0,23]),bind('drag','select-minute','minutes',range=[0,59])])
add('UI066','Option wheel picker','pickers',[part('body','C03',dimensions=(2.4,2.6,.28),material='shell'),part('wheel','B22',dimensions=(1.9,2,.55),material='nacre')],[frame('selection',(0,0,.35),(1.6,.38),'option')],bindings=[bind('drag','select-option','wheel')])
add('UI067','Resizable split view','layout',[part('body','C16',dimensions=(3.7,2.4,.26),material='shell'),part('divider','B23',(0,0,.22),(.18,1.8,.2),'nacre')],[frame('left',(-.95,0,.2),(1.45,1.9)),frame('right',(.95,0,.2),(1.45,1.9))],bindings=[bind('drag','resize-split','divider',minimumFraction=.2)])
add('UI068','Scrollbar rail','layout',[part('body','B03',dimensions=(.25,2.8,.17),material='nacre')],bindings=[bind('drag','scroll',axis='y',range=[0,1])])
add('UI069','Panel resize handle','layout',[part('body','B23',dimensions=(.7,.35,.22),material='nacre')],bindings=[bind('drag','resize-owner',minimum=[1,1])])
add('UI070','Reorder handle row','layout',[part('body','C04',dimensions=(3,.62,.24),material='shell'),part('grip','B23',(-1.13,0,.18),(.36,.3,.2),'nacre')],[frame('content',(.23,0,.18),(2.06,.28),'list-item')],bindings=[bind('drag','reorder','grip')])
add('UI071','Immersive empty-state stage','scenes',[part('body','J03',(0,-.85,-.3),(3.8,.6,1.9),'stone'),part('halo','D08',(0,.25,0),(1.5,1.5,.6),'shell'),part('seed','A08',(0,.3,.2),(.55,.8,.4),'gel')],[frame('content',(0,-.35,.3),(2.4,.45),'empty-state-message')],bindings=[bind('show','settle')])
add('UI072','Immersive panel with shoreline','scenes',[part('body','C03',(0,.35,0),(2.5,3,.32),'shell'),part('shore','J04',(0,-1.35,-.1),(3.9,.6,1.6),'stone'),part('frond','J13',(-1.67,-.1,-.38),(.85,2,.25),'leaf',(0,0,-.22)),part('droplet','E09',(1.5,.87,.2),(.42,.7,.38),'water')],[frame('content',(0,.4,.22),(2.04,2.48))],bindings=[bind('pointermove','parallax','body'),bind('show','lift')])
add('UI073','Liquid control panel','scenes',[part('body','C03',dimensions=(3.15,3.8,.22),material='shell'),part('switch-main','B09',(.6,1.12,.21),(1.18,.52,.25)),part('switch-secondary','B10',(.72,.32,.2),(.92,.44,.25)),part('flow-primary','E12',(0,-.48,.19),(2.6,.4,.27),'water'),part('flow-secondary','B07',(0,-1.17,.19),(2.6,.34,.24),'water')],[frame('header',(0,1.59,.18),(2.5,.23),'panel-header'),frame('switch-main-label',(-.79,1.12,.18),(.82,.32),'label'),frame('switch-secondary-label',(-.74,.32,.18),(.92,.3),'label')], 'Clear shell, independently deforming pearl switches and cyan liquid channels. Foreground content stays separate.',[bind('drag','velocity-linked-thumb-stretch','switch-main'),bind('valuechange','advancing-meniscus','flow-primary'),bind('press','origin-rings','flow-primary')],[3.15,3.8,.6])
add('UI074','Liquid emergence notification','scenes',[part('body','C05',(0,.5,.15),(2.2,1.35,.65),'blue'),part('water','E03',(0,-.76,0),(3.7,2.8,.2),'water',(-1.57079632679,0,0)),part('neck','E11',(0,-.44,.06),(.33,.72,.25),'water'),part('ring','E14',(0,-.66,0),(2.45,1.9,.075),'water',(-1.57079632679,0,0)),part('drop-left','E10',(-.56,-.26,.22),(.09,.16,.09),'water'),part('drop-right','E10',(.66,-.18,.11),(.075,.13,.075),'water')],[frame('content',(0,.52,.54),(1.42,.59),'notification-content')], 'Dimensional speech shell, separate draining neck, free droplets and water receiver. This is a staged assembly snapshot, with a reusable choreography contract.',[bind('show','liquid-emerge','body',phases=['subsurface-light','rise-skirt','neck-drain','detach','rotate','recontact','settle','excite']),bind('contact','water-ring','ring'),bind('settled','curved-emission-filaments','body',particlePreset='G07')],[3.7,2.3,2.8])
RECIPES[-1]['physicalLinks']=[{'source':'body','target':'neck','role':'draining-contact','implementation':'authored geometry; conserved breakup requires an additional solver'},{'source':'body','target':'water','role':'contact-impulse','implementation':'host binds numerical coupling or authored ring response'},{'source':'body','target':'G07','role':'delayed-emission','implementation':'host spawns actual particle preset after settle event'}]
add('UI075','Seed-and-leaf control dock','scenes',[part('body','N02',dimensions=(3.5,1.55,.44),material='shell'),part('seed-left','N01',(-.92,.07,.27),(.59,.83,.38)),part('seed-middle','N01',(0,.07,.27),(.59,.83,.38)),part('lens-right','N03',(.93,.07,.27),(.62,.69,.32),'water'),part('tendril','N04',(-1.7,-.24,-.08),(.64,1.34,.27),'shell'),part('spore','N06',(1.83,.88,.08),(.27,.66,.25),'glow')],[frame('seed-left-content',(-.92,.07,.52),(.24,.28),'icon','seed-left'),frame('seed-middle-content',(0,.07,.52),(.24,.28),'icon','seed-middle'),frame('lens-right-content',(.93,.07,.49),(.28,.24),'icon','lens-right')], 'Three real recessed leaf compartments hold separate seed controls and a dew lens; a curled connector and luminous spore complete the assembly.',[bind('activate','select-compartment','seed-*'),bind('contact','neighbour-impulse','body')],[4.2,2.2,.8])
add('UI076','Water-root membrane panel','scenes',[part('body','N07',(0,0,0),(2.7,3.7,.58),'shell'),part('frond','N05',(-1.85,-.17,-.12),(1.06,2.72,.33),'shell',(0,.18,-.24)),part('spore-a','N06',(1.72,.88,.08),(.25,.75,.24),'glow'),part('spore-b','N06',(1.95,-.25,-.3),(.18,.54,.18),'glow'),part('dew','N03',(.97,-1.9,.19),(.43,.49,.27),'water')],[frame('content',(0,.46,.35),(1.92,1.68),'panel-content')], 'Hollow shell, separate contained water, hanging roots, glass foliage and sparse spores. Content stays on a stable front attachment.',[bind('pointermove','shared-wind','frond'),bind('focus','local-excitation','body')],[4.5,4.15,1])

tokens={
 'version':'3.0.0','units':{'length':'metres','angle':'radians','time':'seconds','mass':'kilograms','temperature':'kelvin'},
 'coordinates':{'handedness':'right','up':[0,1,0],'front':[0,0,1],'right':[1,0,0],'rotationOrder':'XYZ','geometryScale':'authoring scene metres; map to app physical scale explicitly'},
 'spacing':{'micro':.025,'small':.05,'medium':.1,'large':.2,'section':.4,'environment':.8},
 'surfaces':{'controlMinThickness':.12,'panelDefaultThickness':.28,'contentLift':.02,'touchInset':.04,'quietContentMargin':.08,'colliderSkin':.008},
 'lighting':{'keyDirection':[-.45,.8,.5],'keyTemperature':5600,'fillTemperature':7200,'rimTemperature':6400,'rule':'one shared scene light rig, all parts receive it'},
 'camera':{'perspectiveFovDegrees':38,'productView':[4,3,7],'detailView':[2,1.3,4],'orthographicForMeasurementOnly':True},
 'materials':{'gel':{'family':'dense opaline elastomer','ior':1.39,'opticalRole':'soft body'},'water':{'family':'clear liquid','ior':1.333,'opticalRole':'liquid'},'shell':{'family':'clear structural shell','ior':1.46,'opticalRole':'container'},'film':{'family':'thin interference film','ior':1.333,'opticalRole':'bubble skin'},'nacre':{'family':'milky pearl solid','ior':1.52,'opticalRole':'support'},'stone':{'family':'wet mineral','ior':1.5,'opticalRole':'rigid environment'},'leaf':{'family':'botanical solid','ior':1.35,'opticalRole':'vegetation'},'glow':{'family':'emissive solid','ior':1.37,'opticalRole':'particle or core'},'pigment':{'family':'colour carrier','ior':1.37,'opticalRole':'inner content'},'blue':{'family':'blue opaline elastomer','ior':1.4,'opticalRole':'accent body'}},
 'themes':[
  {'id':'tidal','name':'Tidal','gel':'#c6f0f3','accent':'#a8fff1','environment':'#122b3c','nature':'#467c66'},
  {'id':'opal','name':'Opal','gel':'#f1e5f3','accent':'#fff0c9','environment':'#252c44','nature':'#77978c'},
  {'id':'moss','name':'Moss','gel':'#cee7cb','accent':'#d9f8a7','environment':'#162f28','nature':'#7fa665'},
  {'id':'obsidian','name':'Obsidian','gel':'#788296','accent':'#a3dcf0','environment':'#0b111d','nature':'#476477'},
  {'id':'aurora','name':'Aurora','gel':'#d0cef5','accent':'#a0ffe0','environment':'#171e39','nature':'#829c99'},
  {'id':'amber','name':'Amber','gel':'#f0d7af','accent':'#ffe4a3','environment':'#302c2a','nature':'#8b976a'}],
 'motion':{'fixedDt':1/120,'contact':'localized force at hit point','release':'remove pin and preserve velocity','cancel':'release ownership and contact constraints','content':'keep semantic content on a stable attachment plane','worldCoupling':'share collision and field state where the solver API supports it'},
 'accessibilityContract':{'textBakedIntoAssets':False,'hostOwnsSemantics':True,'focusVisible':True,'keyboardAndIme':'required in host adapter','reducedMotion':'host should offer a settled scene and explicit transitions','flashPolicy':'avoid repetitive full-scene flashes'},
 'note':'Design targets and assembly defaults. Numerical material shader values live in src/materials.js and can differ where required by its renderer.'
}

write('compositions.json',RECIPES)
write('design-tokens.json',tokens)
(ROOT/'docs'/'COMPOSITION-INDEX.md').write_text('# Generic UI composition index\n\nAll '+str(len(RECIPES))+' compositions are text-free. Content slots remain empty; application semantics are host adapter contracts.\n\n| ID | Composition | Category | Parts |\n|---|---|---|---:|\n'+'\n'.join(f'| {r["id"]} | {r["name"]} | {r["category"]} | {len(r["parts"])} |' for r in RECIPES)+'\n')

# Record only files that really exist. Missing intended artwork never becomes a phantom asset.
assets=[]
artwork_metadata={}
if (OUT/'artwork-manifest.json').exists():
    artwork_metadata={a['path']:a for a in json.loads((OUT/'artwork-manifest.json').read_text())['artworks']}
for folder in ['assets','src/shaders','vendor']:
    for p in sorted((ROOT/folder).rglob('*')) if (ROOT/folder).exists() else []:
        if not p.is_file(): continue
        suffix=p.suffix.lower(); typ={'png':'raster-artwork','glb':'mesh','gltf':'mesh','blend':'editable-blender-scene','js':'source','glsl':'shader','frag':'shader','vert':'shader'}.get(suffix[1:],'support-file')
        if 'pathtraced' in p.parts and suffix=='.png': typ='path-traced-reference-render'
        if folder=='vendor': typ='third-party-dependency'
        item={'id':p.relative_to(ROOT).as_posix(),'path':p.relative_to(ROOT).as_posix(),'type':typ,'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
        if suffix=='.png':
            try:
                import struct
                data=p.read_bytes(); item['width'],item['height']=struct.unpack('>II',data[16:24]); item['textFree']='authored requirement; visually verify'
            except Exception: pass
            item['use']='art direction, pre-rendered backdrop or surface reference; never substitute a flat PNG for interactive geometry'
            cached=artwork_metadata.get(item['path'])
            if cached and cached.get('sha256')==item['sha256']:
                for key in ['width','height','mode','hasAlphaChannel','alphaRange','transparentPixels','fullyTransparentPixels','semiTransparentPixels','transparencyFraction','role','theme','generation','physicalMap']:
                    item[key]=cached[key]
        assets.append(item)
write('assets-manifest.json',{'version':'3.0.0','count':len(assets),'assets':assets})

art_links={
 'button-pebble':(['A01','A03','A04','A22'],['UI001','UI002','UI003'],'localized gel contact','Separate closed body, mount and stable content plane; local force, release and contact light.'),
 'input-capsule':(['A05','C02','D14'],['UI011','UI013','UI014'],'focus and local compression','Keep editable content rigid; host focus excites the separate light conduit.'),
 'textarea-panel':(['C01','C03','B23'],['UI012'],'panel shell and resize grip','Resize changes layout and physical dimensions; caret and content stay on a separate plane.'),
 'menu-panel':(['C03','C15','A05'],['UI027','UI028','UI030','UI031'],'independent row contacts','Parent lift and row-local contact have separate ownership; content remains empty.'),
 'bottom-dock':(['D01','D02','D03'],['UI034'],'shared support with child mounts','Child bodies compress locally; global support motion is a separate constraint.'),
 'slider-capsule':(['B01','B07','E12'],['UI019','UI049','UI073'],'bounded travel and meniscus','Travel changes thumb geometry; full free-surface meniscus conservation needs solver binding.'),
 'toggle-capsule':(['B09','B10','B12'],['UI008','UI009','UI073'],'velocity-linked pearl stretch','Round at rest, stretch along travel while moving, recover at declared detents.'),
 'dial-ring':(['B13','B15','B17'],['UI023','UI024'],'angular motion','Rotor moves independently of the collar; application value is a stable angular coordinate.'),
 'tooltip-shell':(['C05','C06','C07'],['UI045','UI046'],'anchored lift','Tail is actual geometry, and empty content frame stays readable.'),
 'modal-shell':(['C01','C03','C15'],['UI042','UI043','UI044'],'foreground lift','Move through actual depth with occlusion; no baked backdrop shadow should be duplicated.'),
 'floating-droplet':(['A09','E08','E09','E10'],['UI072','UI074'],'gravity, drag and draining','Separate a non-tearing gel drop from a free liquid domain; topology breakup is a different solver.'),
 'glow-seed':(['A08','G01','G04','G07'],['UI071','UI074'],'wind and delayed trail emission','Use live particle history for tails and shared force fields; emission follows events.'),
 'nature-dew-button':(['N03'],['UI075'],'separate lens and meniscus','Closed biconvex volume with its own curved seat; optical depth follows geometry.'),
 'nature-fern-frond':(['N05','J13'],['UI076'],'wind-loaded glass foliage','Separate leaflets and stem; host binds cloth/rod or authored wind response.'),
 'nature-leaf-tray':(['N02'],['UI075'],'shared leaf support','Three true recessed compartments support independent child controls.'),
 'nature-moss-pedestal':(['J08','J15'],['UI071'],'rigid receiving support','Mineral support receives light and contact; small moss forms remain separate.'),
 'nature-seed-button':(['N01','A08'],['UI075'],'local seed contact','Thick asymmetric closed body, separate seam and dew indicator.'),
 'nature-tendril':(['N04','D12','D14'],['UI075'],'connector bend and light travel','Actual curved branch/socket geometry; rod mechanics and light excitation need explicit bindings.'),
}
links=[]
for p in sorted((ROOT/'assets'/'artwork').glob('*.png')):
    stem=p.stem
    if stem.startswith('background-'):
        ids=['E01','E03','J04','J06','J08','J10','J24']; recipes=['UI071','UI072','UI074']; motion='abstract environment'; anatomy='Distant PNG plane plus real near-field liquid/mineral meshes; perspective parallax and contacts belong to geometry.'
    else:
        ids,recipes,motion,anatomy=art_links.get(stem,([],[],'art direction','Generated artwork; see its corresponding geometry/recipe before assigning mechanical behavior.'))
    links.append({'artwork':p.relative_to(ROOT).as_posix(),'geometryOrEffectIds':ids,'compositionIds':recipes,'motionRole':motion,'anatomy':anatomy,'bakedArtwork':True,'isPhysicalMap':False,'limitations':['Not a depth/normal/roughness/displacement measurement.','Baked light and shadows do not prove live geometry, solver or dynamic optical behavior.']})
write('artwork-motion-map.json',{'version':'3.0.0','artDirection':'Abstract sculptural liquid and mineral environments; text-free reusable elements.','entries':links})

if __name__=='__main__':
    ids={e['id'] for e in json.loads((OUT/'elements.json').read_text())}
    bad=[(r['id'],p['element']) for r in RECIPES for p in r['parts'] if p['element'] not in ids]
    if bad: raise SystemExit(f'Unknown element references: {bad}')
    if len({r['id'] for r in RECIPES}) != len(RECIPES): raise SystemExit('Duplicate composition IDs')
    for recipe in RECIPES:
        local={p['id'] for p in recipe['parts']}
        if len(local)!=len(recipe['parts']): raise SystemExit(f'Duplicate part IDs in {recipe["id"]}')
        for content in recipe['contentFrames']:
            if content['owner'] not in local: raise SystemExit(f'Unknown content owner in {recipe["id"]}: {content["owner"]}')
        for p in recipe['parts']:
            if any(v<=0 for v in p['dimensions']): raise SystemExit(f'Invalid dimensions in {recipe["id"]}: {p["id"]}')
    print(f'{len(ids)} catalogue entries; {len(RECIPES)} distinct UI recipes; {sum(len(r["parts"]) for r in RECIPES)} assembly parts; {len(assets)} actual asset files indexed.')
