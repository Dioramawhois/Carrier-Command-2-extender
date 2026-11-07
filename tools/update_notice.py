"""Generate the owned SDK Workshop menu extension; no network or native loading."""
import json
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]


def payload(version):
    if not re.fullmatch(r'\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?',version):
        raise ValueError('Invalid SDK notice version')
    source=(ROOT/'sdk/ui/update_notice.lua').read_bytes()
    helper=b'do\nlocal install=(function()\n'+source+b'\nend)()\ninstall(_G,"'+version.encode('ascii')+b'")\nend\n'
    descriptor={'schema':1,'extensions':[{'screen':'scripts/screen_menu_main.lua','source':'cc2x/ui/sdk_update.lua'}]}
    return {'content/cc2x/ui/sdk_update.lua':helper,
            'content/cc2x/screen_extensions.json':(json.dumps(descriptor,indent=2)+'\n').encode('utf-8')}
