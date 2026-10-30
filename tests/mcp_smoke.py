"""End-to-end check of MagnumMcp.exe: starts the server on a throw-away project, starts the real editor through it and uses
most tools the way an AI client would.

    python tests/mcp_smoke.py [path\\to\\bin]        (default: build-Release\\bin next to this repository)

Exit code 0 = everything worked. Needs a GPU and a desktop session, like the editor itself.
"""
import base64, json, os, shutil, subprocess, sys, tempfile, time

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
bindir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, 'build-Release', 'bin')
project = os.path.join(tempfile.gettempdir(), 'magnum_mcp_smoke_project')
shutil.rmtree(project, ignore_errors=True)

server = subprocess.Popen([os.path.join(bindir, 'MagnumMcp.exe'), '--project', project], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                          stderr=subprocess.DEVNULL, bufsize=0)
failures = []
next_id = [0]


def rpc(method, params=None, notify=False):
    msg = {'jsonrpc': '2.0', 'method': method}
    if params is not None:
        msg['params'] = params
    if not notify:
        next_id[0] += 1
        msg['id'] = next_id[0]
    server.stdin.write((json.dumps(msg) + '\n').encode())
    server.stdin.flush()
    if notify:
        return None
    while True:
        line = server.stdout.readline()
        if not line:
            raise RuntimeError('the server closed')
        reply = json.loads(line)
        if reply.get('id') == msg['id']:
            return reply


def call(_tool, **args):
    reply = rpc('tools/call', {'name': _tool, 'arguments': args})
    if 'error' in reply:
        return True, reply['error']['message'], []
    result = reply['result']
    text = '\n'.join(c['text'] for c in result['content'] if c['type'] == 'text')
    images = [c for c in result['content'] if c['type'] == 'image']
    return result.get('isError', False), text, images


def check(name, condition, detail=''):
    print(('ok    ' if condition else 'FAIL  ') + name + ('' if condition else '   ' + str(detail)[:300]))
    if not condition:
        failures.append(name)


try:
    init = rpc('initialize', {'protocolVersion': '2025-03-26', 'capabilities': {}, 'clientInfo': {'name': 'smoke', 'version': '1'}})
    check('initialize', init['result']['serverInfo']['name'] == 'magnum-engine' and init['result']['protocolVersion'] == '2025-03-26', init)
    rpc('notifications/initialized', notify=True)
    check('ping', rpc('ping')['result'] == {})
    tools = rpc('tools/list')['result']['tools']
    names = {t['name'] for t in tools}
    check('tools/list', len(tools) >= 40 and all('inputSchema' in t and t['description'] for t in tools), len(tools))
    check('unknown method', rpc('nonsense')['error']['code'] == -32601)
    check('unknown tool', rpc('tools/call', {'name': 'nope', 'arguments': {}})['error']['code'] == -32602)

    err, text, _ = call('project_info')
    check('project_info works without the editor', not err and 'magnum_mcp_smoke_project' in text, text)
    err, text, _ = call('project_write', path='../escape.txt', content='x')
    check('path escape refused', err and 'leaves the project' in text, text)
    err, text, _ = call('project_write', path='Engine/Lua/x.lua', content='x')
    check('engine files are read only', err and 'read only' in text, text)

    t0 = time.time()
    err, text, _ = call('editor_status')   # starts the editor on the new project
    check('editor starts by itself', not err and '"playing": false' in text, text)
    print('      editor ready in %.1f s' % (time.time() - t0))
    status = json.loads(text)
    check('project was created from the template', os.path.exists(os.path.join(project, 'Content', 'UI', 'hud.lua')))

    err, text, _ = call('scene_list')
    check('scene_list', not err and 'actor(s)' in text, text)
    err, text, _ = call('scene_spawn', kind='cube', name='Box', position=[2, 1, 0], properties={'Collider': {}, 'Rigidbody': {'Gravity scale': 0.5}})
    check('scene_spawn with properties', not err and '"Box"' in text and '"Rigidbody"' in text and '0.5' in text, text)
    err, text, _ = call('scene_set', actor='Box', properties={'Transform': {'Position': [3, 2, 1]}})
    check('scene_set', not err and '3.0' in text, text)
    err, text, _ = call('scene_set', actor='Box', component='Transform', property='Nope', value=1)
    check('scene_set reports a bad property', err and "no property 'Nope'" in text, text)
    err, text, _ = call('scene_set', actor='Box', component='Mesh', property='Primitive', value='Sphere')
    check('enum by name', not err and 'Sphere' in text, text)
    err, text, _ = call('scene_get_actor', actor='Nobody')
    check('unknown actor', err and 'no actor' in text, text)
    err, text, _ = call('scene_duplicate', actor='Box')
    check('scene_duplicate', not err and 'Box' in text, text)
    err, text, _ = call('scene_undo')
    check('scene_undo', not err, text)
    err, text, _ = call('reference_components')
    check('reference_components', not err and '## Sprite' in text and 'canvas' in text, text[:200])

    # Files: a UI document with a mistake, then a good one.
    err, text, _ = call('scene_spawn', kind='canvas', name='Test Canvas', properties={'Canvas': {'Document': 'Content/UI/test.lua'}})
    err, text, _ = call('project_write', path='Content/UI/test.lua', content='return ui.panel{ nonsense_property = 1 }\n')
    check('write reports a Lua error from the editor', err and 'nonsense_property' in text, text)
    good = 'return ui.panel{ anchor = "fill", ui.text{ "Hello MCP", name = "Greeting", anchor = "center", font_size = 40 } }\n'
    err, text, _ = call('project_write', path='Content/UI/test.lua', content=good)
    check('write a good UI document', not err and 'without problems' in text, text)
    err, text, _ = call('project_edit', path='Content/UI/test.lua', find='Hello MCP', replace='Hello again')
    check('project_edit', not err, text)
    err, text, _ = call('project_edit', path='Content/UI/test.lua', find='zzz', replace='y')
    check('project_edit needs a match', err and 'not found' in text, text)
    err, text, _ = call('project_read', path='Content/UI/test.lua')
    check('project_read', not err and 'Hello again' in text, text)
    err, text, _ = call('project_files', folder='Content/UI')
    check('project_files', not err and 'test.lua' in text and 'hud.lua' in text, text)
    err, text, _ = call('project_create_asset', kind='script', path='Content/Scripts/smoke.lua')
    check('project_create_asset', not err and 'Created' in text, text)
    err, text, _ = call('project_read', path='Engine/Shaders/sprite.lua')
    check('engine file readable', not err and 'shader("Sprite")' in text, text)
    err, text, _ = call('ui_tree')
    check('ui_tree shows the preview', not err and 'Greeting' in text, text)

    err, text, images = call('viewport_screenshot')
    ok_png = bool(images) and base64.b64decode(images[0]['data'])[:8] == b'\x89PNG\r\n\x1a\n'
    check('viewport_screenshot', not err and ok_png, text)
    err, text, _ = call('viewport_camera', mode='3d', position=[0, 3, 8], look_at=[0, 1, 0])
    check('viewport_camera', not err and 'camera' in text.lower(), text)

    # Playing.
    err, text, _ = call('scene_spawn', kind='cube', name='Runner', position=[0, 1, 0],
                        properties={'Script': {'Script': 'Content/Scripts/smoke.lua'}})
    err, text, _ = call('play_start')
    check('play_start', not err and 'Play started' in text, text)
    err, text, _ = call('play_keys', keys=['d'], action='hold', frames=20)
    check('play_keys', not err, text)
    err, text, _ = call('play_lua', code='local r = world.find("Runner"); log("runner at " .. tostring(r.position.x))')
    check('play_lua', not err and 'runner at' in text, text)
    err, text, _ = call('play_lua', code='error("boom")')
    check('play_lua reports errors', err and 'boom' in text, text)
    err, text, _ = call('play_mouse', x=0.5, y=0.5, action='click')
    check('play_mouse', not err, text)
    err, text, _ = call('play_pause', paused=True)
    err, text, _ = call('play_pause', paused=False)
    err, text, _ = call('play_wait', frames=10)
    err, text, _ = call('play_stop')
    check('play_stop', not err and 'restored' in text, text)

    err, text, _ = call('console_run', command='nonsense')
    check('console_run flags unknown commands', err, text)
    err, text, _ = call('log_read', max=5)
    check('log_read', not err and text.strip(), text)
    err, text, _ = call('scene_save', path='Content/Scenes/Smoke.scene')
    check('scene_save', not err and os.path.exists(os.path.join(project, 'Content', 'Scenes', 'Smoke.scene')), text)
    err, text, _ = call('docs_read')
    check('docs_read lists', not err and 'UI.md' in text and 'CHANGELOG.md' in text, text)
    err, text, _ = call('docs_read', name='UI.md')
    check('docs_read UI.md', not err and '# UI' in text, text[:80])

    res = rpc('resources/list')['result']['resources']
    check('resources/list', any(r['uri'] == 'magnum://guide' for r in res) and any(r['uri'] == 'magnum://docs/UI.md' for r in res), res)
    read = rpc('resources/read', {'uri': 'magnum://reference/components'})['result']['contents'][0]['text']
    check('resource: components', '## Transform' in read)
    prompt = rpc('prompts/get', {'name': 'make_a_game', 'arguments': {'idea': 'a platformer'}})['result']['messages'][0]['content']['text']
    check('prompts/get', 'a platformer' in prompt)

    err, text, _ = call('run_tests', filter='SpriteSizes')
    check('run_tests', not err and 'all tests passed' in text, text)

    err, text, _ = call('editor_quit', discard_changes=True)
    check('editor_quit', not err, text)
finally:
    try:
        server.stdin.close()
        server.wait(timeout=10)
    except Exception:
        server.kill()
    shutil.rmtree(project, ignore_errors=True)

print('\n%d failure(s)' % len(failures))
sys.exit(1 if failures else 0)

