"""Exercise the actual SD web assets at desktop/tablet/mobile widths.
Requires playwright + Chromium (or the local Google Chrome on macOS).
The API is a deterministic double; hardware HTTP tests are separate.
"""
from pathlib import Path
from http.server import ThreadingHTTPServer,BaseHTTPRequestHandler
import json,threading,os,tempfile
from urllib.parse import urlparse,parse_qs
files={"launcher":b"", "firmware.bin":b"A",**{f"extra-{i:02}.txt":b"text" for i in range(60)}};fail_once=True;uploads=[]
from playwright.sync_api import sync_playwright
ROOT=Path(__file__).resolve().parents[2]
ASSETS=ROOT/'sdcard/launcher/web'
config=dict(schema=1,theme='midnight',color='EF5368',bright=80,dim=60,screenOff=300,wifiAuto=True,buttonOnly=False,bootTime=3)
class Handler(BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def do_GET(self):
  if self.path.split('?')[0] in ('/','/style.css','/app.js','/transfers.js'):
   file=ASSETS/('index.html' if self.path=='/' else self.path[1:]);self.send_response(200);self.send_header('Content-Type','text/html; charset=utf-8' if file.suffix=='.html' else 'text/css' if file.suffix=='.css' else 'application/javascript');self.end_headers();self.wfile.write(file.read_bytes());return
  self.api()
 def do_POST(self):self.api()
 def api(self):
  global fail_once
  path=self.path.split('?')[0];query=parse_qs(urlparse(self.path).query)
  body=self.rfile.read(int(self.headers.get('Content-Length','0')))
  cookie='tab5_session=0123456789abcdef0123456789abcdef'
  if path=='/api/session' and self.command=='POST':
   assert self.headers.get('X-Launcher-Token')=='12345678';self.send_response(200);self.send_header('Set-Cookie',cookie+'; Path=/; HttpOnly; SameSite=Strict');self.end_headers();self.wfile.write(b'{"authenticated":true}');return
  if cookie not in self.headers.get('Cookie',''):self.send_response(401);self.end_headers();return
  assert self.headers.get('X-Launcher-Token') is None
  if self.command=='POST':assert self.headers.get('X-Launcher-Client')=='web'
  if path=='/api/session':value=dict(authenticated=True)
  elif path=='/api/logout':
   self.send_response(200);self.send_header('Set-Cookie','tab5_session=; Path=/; Max-Age=0');self.end_headers();self.wfile.write(b'{}');return
  elif path=='/api/status':value=dict(version='0.3.1',ip='192.168.137.116',rssi=-46,batteryEstimate=93,batteryMv=8270,internalFree=170000,internalMinimum=165000,psramFree=27000000,reset='USB')
  elif path=='/api/apps':value=[dict(slot=0,label='app0',name='MicroCity',version='0.11.0',descriptorValid=True),dict(slot=1,label='app1',name='Outro aplicativo',version='1.0',descriptorValid=True)]
  elif path=='/api/files':
   base=query.get('path',['/sdcard'])[0];names={k[len(base.removeprefix('/sdcard').strip('/'))+1:] if base!='/sdcard' else k:v for k,v in files.items() if base=='/sdcard' or k.startswith(base.removeprefix('/sdcard').strip('/')+'/')};value=dict(total=len(names),page=0,entries=[dict(name=k,size=len(v),directory=k=='launcher') for k,v in names.items()])
  elif path=='/api/operation':value=dict(ok=True,ticket='test',percent=100,running=False,message='Operação verificada')
  elif path=='/api/config':
   if self.command=='POST':config.update(json.loads(body))
   value=config
  elif path=='/api/file-action':
   action=json.loads(body);name=action['path'].removeprefix('/sdcard/');kind=action['kind']
   if kind=='mkdir':files[action['value']]=b''
   elif kind=='rename':files[action['value']]=files.pop(name)
   elif kind=='delete':files.pop(name,None)
   value=dict(ok=True)
  elif path=='/api/mkdir':value=dict(created=True)
  elif path=='/api/upload':
   name=query['relative'][0];uploads.append(name)
   if name.endswith('retry.txt') and fail_once:
    fail_once=False;self.send_response(507);self.end_headers();self.wfile.write(b'Cartao ocupado');return
   files[name]=body;value=dict(path='/sdcard/'+name,verified=True,message='Arquivo enviado e verificado')
  else:value=dict(message='Pedido recebido')
  self.send_response(200);self.send_header('Content-Type','application/json');self.end_headers();self.wfile.write(json.dumps(value).encode())
server=ThreadingHTTPServer(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start()
chrome=Path('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
with sync_playwright() as p:
 browser=p.chromium.launch(headless=True,**({'executable_path':str(chrome)} if chrome.exists() else {}))
 for width,height in [(1280,900),(768,1024),(390,844),(320,700)]:
  page=browser.new_page(viewport=dict(width=width,height=height),device_scale_factor=1)
  errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
  page.goto(f'http://127.0.0.1:{server.server_port}')
  page.locator('#token').fill('12345678');page.locator('#connect').click();page.wait_for_function("document.getElementById('message').textContent.startsWith('Conectado.')")
  assert page.evaluate('document.documentElement.scrollWidth <= innerWidth'),f'Horizontal overflow at {width}'
  if width<=600:page.locator('#menu-toggle').click()
  page.locator('[data-tab=settings]').click();page.locator('#bright').fill('66');page.locator('#swatches button').nth(3).click();page.locator('#config-form button[type=submit]').click();page.wait_for_function("document.getElementById('message').textContent.includes('Configuração salva')")
  assert config['bright']==66 and config['color']=='22C55E'
  assert page.evaluate('document.documentElement.scrollWidth <= innerWidth'),f'Config overflow at {width}'
  page.screenshot(path=f'/tmp/tab5-web-040-{width}.png',full_page=True)
  if width<=600:page.locator('#menu-toggle').click()
  page.locator('[data-tab=files]').click()
  assert page.locator('#files-list tr').count()>=62
  assert 'extra-59.txt' in page.locator('#files-list').inner_text()
  assert page.locator('#file-drop .file-type').first.inner_text()=='Pasta'
  assert page.evaluate('document.documentElement.scrollWidth <= innerWidth'),f'Files overflow at {width}'
  page.locator('#new-folder').click();page.locator('#file-action-value').fill(f'nova-{width}');page.locator('#file-action-confirm').click()
  page.wait_for_function("document.getElementById('message').textContent==='Pasta criada.'")
  assert f'nova-{width}' in page.locator('#files-list').inner_text()
  page.evaluate("""async()=>{const data=new DataTransfer();data.items.add(new File(['drop'],'solto.txt',{type:'text/plain'}));document.getElementById('file-drop').dispatchEvent(new DragEvent('drop',{dataTransfer:data,bubbles:true,cancelable:true}));}""")
  page.wait_for_function("document.getElementById('message').textContent.startsWith('Envio concluído')")
  assert files['solto.txt']==b'drop'
  page.locator('#transfer-close').click()
  page.locator('#upload').set_input_files(dict(name='app.bin',mimeType='application/octet-stream',buffer=b'ABC'));page.locator('#send').click();page.wait_for_function("document.getElementById('message').textContent.startsWith('Envio concluído')")
  assert 'app.bin' in page.locator('#files-list').inner_text()
  assert page.locator('#token').input_value()==''
  assert 'tab5_session' not in page.evaluate('document.cookie')
  page.locator('#transfer-close').click();page.reload();page.wait_for_function("document.getElementById('message').textContent==='Sessão restaurada.'")
  assert page.locator('#token').input_value()==''
  if width<=600:page.locator('#menu-toggle').click()
  page.locator('[data-tab=files]').click()
  if width==1280:
   with tempfile.TemporaryDirectory() as tmp:
    folder=Path(tmp)/'pasta';(folder/'sub').mkdir(parents=True);(folder/'sub'/'ok.txt').write_text('OK');(folder/'retry.txt').write_text('retry');(folder/'empty.txt').touch()
    page.locator('#folder-upload').set_input_files(str(folder));page.locator('#send').click();page.wait_for_function("document.querySelector('#transfer-list [data-state=failed]')!==null && !document.getElementById('transfer-retry').disabled")
    assert 'Cartao ocupado' in page.locator('#transfer-list').inner_text();page.locator('#transfer-retry').click();page.wait_for_function("document.getElementById('message').textContent.startsWith('Envio concluído')")
    assert files['pasta/sub/ok.txt']==b'OK' and files['pasta/empty.txt']==b'' and uploads.count('pasta/retry.txt')==2
   page.locator('#transfer-close').click()
  if width<=600:page.locator('#menu-toggle').click()
  page.locator('[data-tab=maintenance]').click()
  assert page.evaluate('document.documentElement.scrollWidth <= innerWidth'),f'Maintenance overflow at {width}'
  page.on('dialog',lambda d:d.accept());page.locator('#flash-backup').click();page.wait_for_function("document.getElementById('operation-state').textContent==='Operação verificada'")
  if width==1280:
   page.locator('[data-tab=files]').click()
   page.locator('#image-source').set_input_files(str(ROOT/'managed_components/lvgl__lvgl/tests/src/test_assets/test_img_lvgl_logo.png'))
   page.wait_for_selector('#image-modal[open]');page.locator('#image-preset').select_option('icon');page.locator('#image-rotate').click();page.locator('#image-save').click();page.wait_for_function("document.getElementById('message').textContent.startsWith('Envio concluído')")
   prepared=[v for k,v in files.items() if k.startswith('icon-')][-1]
   assert prepared[:8]==b'\x89PNG\r\n\x1a\n' and int.from_bytes(prepared[16:20],'big')==256 and int.from_bytes(prepared[20:24],'big')==256
   page.locator('#transfer-close').click()
  assert not errors,errors
  page.close()
 browser.close()
server.shutdown()
print('Actual SD HTML/CSS/JS passed 1280/768/390/320px: login, navigation, swatches, save, upload, no horizontal overflow or JS errors.')
