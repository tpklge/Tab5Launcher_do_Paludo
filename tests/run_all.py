"""Build and run the firmware's host regressions. Requires cc, cmake, OpenSSL, IDF sources."""
from pathlib import Path
import os,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[1]
os.chdir(ROOT)
env=os.environ.copy()
sdk=Path(env.get('IDF_PATH',str(Path.home()/'esp/esp-idf')))
env['IDF_PATH']=str(sdk)
mac_sdk=Path('/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk')
if sys.platform=='darwin' and mac_sdk.exists(): env.setdefault('SDKROOT',str(mac_sdk))
def run(args):
 print('+ '+' '.join(map(str,args)),flush=True)
 subprocess.run(list(map(str,args)),env=env,check=True)
if (ROOT/'build/esp-idf/ui').exists(): run([sys.executable,ROOT/'tools/check_ui_stack.py'])
for script in ('tools/test_usb_client.py','tools/test_flash_layout_info.py','storage_guard/run.py','usb_msc/run.py','usb_mode/run.py','tools/test_dynamic_upgrade.py','catalog_http/run.py','catalog_resume/run.py','catalog_tracking/run.py','catalog_sources/run.py','flash_snapshot/run.py','app_data/run.py','launcher_store/run.py','launcher_state/run.py','usb/run.py','web/run.py','pi_link/run.py','pi_services/run.py','c6_update/run.py','catalog/run.py','test_storage_host.py','firmware/run.py','installer/run.py','installer/large.py','bundles/run.py','sd_files/run.py','sd_transport/run.py','layout/run.py','layout/transaction.py','multiboot/run.py','boot_policy/run.py','host_wifi/run.py'):
 run([sys.executable,ROOT/'tests'/script])
with tempfile.TemporaryDirectory(prefix='tab5-host-') as tmp:
 folder=Path(tmp)
 for name,includes,sources in [
  ('power',['components/board/include'],['tests/power/test.c','components/board/power_model.c']),
  ('rtc',['components/board/include'],['tests/rtc/test.c','components/board/rtc_codec.c']),
  ('registry',['tests/installer/stubs','components/storage/include'],['tests/registry/test.c','components/storage/app_registry.c','components/storage/sd_files.c','components/storage/storage_guard.c']),
  ('boot_runtime',['tests/boot_runtime/stubs','tests/installer/stubs','components/boot_mgr/include','components/storage/include'],['tests/boot_runtime/test.c'])]:
  command=['cc','-std=c99','-g','-fsanitize=address,undefined']
  for include in includes: command+=['-I',include]
  run(command+sources+['-o',folder/name]); run([folder/name])
ui_build=Path(os.environ.get('TAB5_UI_TEST_BUILD','/tmp/tab5-host-ui'))
run(['cmake','-S','tests/host_ui','-B',ui_build]); run(['cmake','--build',ui_build,'-j','8'])
for target in ('test_services','test_nav','test_home','test_sd','test_wifi','test_cfg','test_visual','test_status'):
 run([ui_build/target])
assets=ROOT/'managed_components/lvgl__lvgl/tests/src/test_assets'
run([ui_build/'test_assets',assets/'test_img_lvgl_logo.png',assets/'test_img_lvgl_logo.jpg'])
run([ui_build/'test_fonts','S:'+str(ROOT/'sdcard/launcher/fonts/cjk.bin'),'S:'+str(ROOT/'sdcard/launcher/fonts/emoji.bin')])
print('All host regressions passed. Hardware behavior still requires device validation.')
