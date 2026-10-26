#!/usr/bin/env python3
# pc/3DForge/tools/mcp_test.py -- the MCP server of 3DForge for Windows (3DForge.exe --mcp) driven as an assistant would:
# initialize, the tools' list, a part built (a box, a hole, a sketch extruded), its state, a picture, the exports, the
# G-code. The window is drawn off the screen (FORGE_OFFSCREEN=1) and closed at the end.
#   python pc/3DForge/tools/mcp_test.py [out folder]
# MIT licence (Onyx).
import functools, builtins
print = functools.partial (builtins.print, flush = True)
import base64, json, os, subprocess, sys

ROOT = os.path.normpath (os.path.join (os.path.dirname (os.path.abspath (__file__)), '..', '..', '..'))
EXE = os.path.join (ROOT, 'pc', 'dist', '3DForge', '3DForge.exe')
OUT = os.path.abspath (sys.argv[1] if len (sys.argv) > 1 else os.path.join (ROOT, 'pc', '3DForge', 'build', 'mcp'))
os.makedirs (OUT, exist_ok = True)
env = dict (os.environ, FORGE_OFFSCREEN = '1')
# (a 3DForge window open would be the one driven: refused; an MCP server alone -- no window -- does no harm)
_ps = "Get-Process 3DForge -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Measure-Object | Select-Object -ExpandProperty Count"
if subprocess.run (['powershell', '-NoProfile', '-Command', _ps], capture_output = True, text = True).stdout.strip () not in ('', '0'): raise SystemExit ('A 3DForge window is open: the test would drive it. Close it first.')
p = subprocess.Popen ([EXE, '--mcp'], stdin = subprocess.PIPE, stdout = subprocess.PIPE, env = env)
n = 0
def rpc (method, params = None):
	global n; n += 1
	p.stdin.write ((json.dumps ({'jsonrpc': '2.0', 'id': n, 'method': method, 'params': params or {}}) + '\n').encode ()); p.stdin.flush ()
	r = json.loads (p.stdout.readline ())
	if 'error' in r: raise SystemExit ('%s: %s' % (method, r['error']))
	return r['result']
fails = 0
def call (_tool, **args):
	global fails
	r = rpc ('tools/call', {'name': _tool, 'arguments': args})
	texts = [c['text'] for c in r['content'] if c['type'] == 'text']
	if r.get ('isError'): fails += 1; print ('  ERROR', _tool, args, texts)
	for c in r['content']:
		if c['type'] == 'image':
			path = os.path.join (OUT, args.get ('save', 'shot') + '.png'); open (path, 'wb').write (base64.b64decode (c['data'])); texts.append ('picture -> ' + path)
	return '\n'.join (texts)

init = rpc ('initialize', {'protocolVersion': '2025-06-18', 'capabilities': {}, 'clientInfo': {'name': 'test', 'version': '1'}})
print ('server', init['serverInfo'])
p.stdin.write (b'{"jsonrpc":"2.0","method":"notifications/initialized"}\n'); p.stdin.flush ()
tools = rpc ('tools/list')['tools']; print (len (tools), 'tools:', ', '.join (t['name'] for t in tools))

print (call ('new_part', discard_changes = True))
print (call ('make_shape', shape = 'box', values = {'Width': 60, 'Depth': 40, 'Height': 12}))
print (call ('make_shape', shape = 'cylinder', values = {'Diameter': 10, 'Depth': 12, 'Centre X': 0, 'Y': 0, 'Z': 0}, operation = 'Subtract'))
# a sketch on XY lifted to the box's top: a rectangle drawn with the pointer, seen from the top, then extruded
print (call ('set_tool', tool = 'sketch'))
print (call ('set_option', label = 'Plane', value = 'XY'))
print (call ('set_field', name = 'Offset', value = '12'))
print (call ('command', name = 'start_sketch'))
st = json.loads (call ('get_state')); w, h = st['view_size']; print ('view', w, h, st['mode'])
print (call ('set_tool', tool = 'rectangle'))
print (call ('view_mouse', action = 'click', x = w // 2 + 20, y = h // 2 - 60))
print (call ('view_mouse', action = 'click', x = w // 2 + 90, y = h // 2 - 120))
print (call ('screenshot', what = 'window', save = 'sketch'))
print (call ('command', name = 'finish_sketch'))
print (call ('set_tool', tool = 'extrude'))
print (call ('set_field', name = 'Height', value = '8'))
print (call ('command', name = 'ok'))
print (call ('command', name = 'home'))
print (call ('get_panel'))
print (call ('get_state'))
print (call ('screenshot', what = 'window', save = 'part'))
print (call ('export', path = os.path.join (OUT, 'part.stl')))
print (call ('export', path = os.path.join (OUT, 'part.pdf'), view = 'top'))
print (call ('save_file', path = os.path.join (OUT, 'part.3df')))
print (call ('command', name = 'manufacture'))
print (call ('command', name = 'add_clearing'))
print (call ('write_gcode', path = os.path.join (OUT, 'part.nc')))
print (call ('set_field', name = 'Nothing', value = '1'))		# (an error, said as one)
fails -= 1
print (call ('close_window', discard_changes = True))
p.stdin.close (); p.wait (timeout = 20)
print ('failures:', fails)
sys.exit (1 if fails else 0)
