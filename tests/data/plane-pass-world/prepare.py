"""Stage original-ink world/HUD plane PASS_MOUSE probes, without launching processes."""
from pathlib import Path
import json
import subprocess
import sys

source = Path(__file__).resolve().parent
if len(sys.argv) != 2:
    raise SystemExit('usage: prepare.py <new-build-runtime>')
target = Path(sys.argv[1]).resolve()
subprocess.run([sys.executable, '-B', str(source.parent/'stock-shadow-world'/'prepare.py'), str(target)], check=True)
world = target/'world.dm'
text = world.read_text()
# Remove per-client probe leaves before the shared composition fixture resets.
marker = '\t\t\tclient.images = null'
assert text.count(marker) == 1
text = text.replace(marker, '''\t\t\tfor(var/mob/pass_leaf/L in world)
\t\t\t\tdel L
\t\t\tfor(var/turf/T in world)
\t\t\t\tT.name = "floor"
\t\t\t\tT.plane = 0
\t\t\t\tT.icon_state = "floor"
\t\t\tfor(var/obj/part/B in world)
\t\t\t\tB.mouse_opacity = 1
\t\t\tclient.images = null''')
marker = '\t\t\tnote("READY [raw]")'
assert text.count(marker) == 1
text = text.replace(marker, '''\t\t\tif(findtext(p,"shadow-pass-"))
\t\t\t\tvar/list/c = json_decode(file2text("cases.json"))[p]
\t\t\t\tif(c["unfiltered"]) M.filters = null
\t\t\t\tif(c["blur"]) M.filters = filter(type="blur",size=2)
\t\t\t\tif(c["outline"]) M.filters = filter(type="outline",size=2,color="#80c02080")
\t\t\t\tif(c["master_zero"]) M.alpha = 0
\t\t\t\tfor(var/obj/part/B in world)
\t\t\t\t\tif(B.name != "red") continue
\t\t\t\t\tB.mouse_opacity = c["mouse"]
\t\t\t\t\tif(c["leaf"] == "mob")
\t\t\t\t\t\tnew /mob/pass_leaf(B.loc)
\t\t\t\t\t\tB.mouse_opacity = 0
\t\t\t\t\t\tB.alpha = 0
\t\t\t\t\tif(c["leaf"] == "turf")
\t\t\t\t\t\tvar/turf/T = B.loc
\t\t\t\t\t\tT.name = "source-floor"
\t\t\t\t\t\tT.plane = 1
\t\t\t\t\t\tT.icon_state = "st"
\t\t\t\t\t\tB.mouse_opacity = 0
\t\t\t\t\t\tB.alpha = 0
\t\t\t\t\tif(c["leaf"] == "hud")
\t\t\t\t\t\tvar/obj/part/H = new
\t\t\t\t\t\tH.name = "hud-leaf"
\t\t\t\t\t\tH.screen_loc = "4,4"
\t\t\t\t\t\tclient.screen += H
\t\t\t\t\t\tB.mouse_opacity = 0
\t\t\t\t\t\tB.alpha = 0
\t\t\t\tif(c["chain"])
\t\t\t\t\tvar/obj/master/N = new
\t\t\t\t\tN.name = "middle"
\t\t\t\t\tN.plane = 3
\t\t\t\t\tN.render_target = "*middle"
\t\t\t\t\tclient.screen += N
\t\t\t\t\tvar/obj/part/R = new
\t\t\t\t\tR.name = "outer"
\t\t\t\t\tR.icon = null
\t\t\t\t\tR.plane = 5
\t\t\t\t\tR.screen_loc = "1,1"
\t\t\t\t\tR.render_source = "*middle"
\t\t\t\t\tR.appearance_flags = PASS_MOUSE
\t\t\t\t\tclient.screen += R
\t\t\tnote("READY [raw]")''')
# Intermediate masters are also per-phase; the primary M remains owned by phases().
text = text.replace('\t\t\tfor(var/mob/pass_leaf/L in world)', '\t\t\tfor(var/obj/master/N in client.screen)\n\t\t\t\tif(N != M)\n\t\t\t\t\tclient.screen -= N\n\t\t\t\t\tdel N\n\t\t\tfor(var/mob/pass_leaf/L in world)')
text += '''
mob/pass_leaf
\tname = "source-mob"
\ticon = 'z.dmi'
\ticon_state = "st"
\tplane = 1
'''
world.write_text(text)
cases = {}
for name, changes in [
    ('object', {}), ('unfiltered', {'unfiltered': True}),
    ('blur', {'blur': True}), ('outline', {'outline': True}),
    ('mouse0', {'mouse': 0}), ('mouse2', {'mouse': 2}),
    ('partial', {'source_alpha': 128}), ('zero', {'source_alpha': 0}),
    ('master-zero', {'master_zero': True}), ('mob', {'leaf': 'mob'}),
    ('turf', {'leaf': 'turf'}), ('hud', {'leaf': 'hud'}), ('chain', {'chain': True}),
]:
    cases['shadow-pass-'+name] = dict(size=4, offset=0, x=0, y=-2, color='#04080FAA',
        source_alpha=255, target=True, mouse=1, leaf='object') | changes
(target/'cases.json').write_text(json.dumps(cases, indent=2)+'\n')
