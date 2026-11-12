from pathlib import Path
import ctypes as C
import errno
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
class Entry(C.Structure):
    _fields_=[('name',C.c_char*256),('size',C.c_uint64),('modified',C.c_int64),('directory',C.c_bool)]
class Listing(C.Structure):
    _fields_=[('entries',Entry*24),('count',C.c_uint),('total',C.c_uint),('page',C.c_uint)]
with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp)/'sd'; root.mkdir(); usb=Path(tmp)/'usb';usb.mkdir(); libpath=Path(tmp)/'sd.so'
    subprocess.run(['cc','-shared','-fPIC','-g','-fsanitize=undefined',f'-DSD_FILES_ROOT="{root}"',f'-DUSB_FILES_ROOT="{usb}"',
                    '-I',str(ROOT/'components/storage/include'),str(ROOT/'components/storage/sd_files.c'),str(ROOT/'components/storage/storage_guard.c'),'-o',str(libpath)],check=True)
    lib=C.CDLL(str(libpath)); b=lambda p:str(p).encode()
    lib.sd_path_valid.argtypes=[C.c_char_p]; lib.sd_path_valid.restype=C.c_bool
    lib.sd_name_valid.argtypes=[C.c_char_p]; lib.sd_name_valid.restype=C.c_bool
    lib.sd_media_acquire.argtypes=[C.c_char_p];lib.sd_media_acquire.restype=C.c_bool
    lib.sd_media_release.argtypes=[C.c_char_p]
    assert lib.sd_path_valid(b(usb)) and not lib.sd_media_acquire(b(usb))
    listing=Listing();assert lib.sd_files_list(b(usb),0,C.byref(listing))==errno.ENODEV
    lib.sd_usb_set_available(True);assert lib.sd_media_acquire(b(usb)) and lib.sd_usb_active_operations()==1
    lib.sd_usb_set_available(False);assert not lib.sd_media_acquire(b(usb)) and lib.sd_usb_active_operations()==1
    lib.sd_media_release(b(usb));assert lib.sd_usb_active_operations()==0
    lib.sd_usb_set_available(True);(usb/'test.txt').write_text('usb')
    assert lib.sd_files_list(b(usb),0,C.byref(listing))==0 and listing.count==1
    assert lib.sd_files_delete_recursive(b(usb))==errno.EINVAL
    lib.sd_files_list.argtypes=[C.c_char_p,C.c_uint,C.POINTER(Listing)]
    for path in (str(root)+'2',str(root)+'/../x',str(root)+'//x',str(root)+'/./x','/etc/passwd'):
        assert not lib.sd_path_valid(path.encode())
    for name in ('','..','a/b','../a','a\\b','a:','a.','a ','x'*256): assert not lib.sd_name_valid(name.encode())
    assert lib.sd_name_valid('firmware com espacos.bin'.encode())
    (root/'folder A').mkdir(); (root/'folder B').mkdir()
    for i in range(50): (root/f'file-{i:02d}.txt').write_text(f'content {i}')
    names=[]
    for page in range(3):
        listing=Listing(); assert lib.sd_files_list(b(root),page,C.byref(listing))==0
        assert listing.total==52
        names.extend(e.name.decode() for e in listing.entries[:listing.count])
        if page==0: assert all(listing.entries[i].directory for i in range(2))
    assert len(names)==len(set(names))==52
    assert names==['folder A','folder B']+[f'file-{i:02d}.txt' for i in range(50)]
    assert lib.sd_files_mkdir(b(root),b'new folder')==0
    source=root/'copy source.bin'; data=bytes(range(256))*8192; source.write_bytes(data)
    dest=root/'new folder'
    assert lib.sd_files_copy(b(source),b(dest))==0
    assert (dest/source.name).read_bytes()==data
    assert lib.sd_files_copy(b(source),b(dest))==errno.EEXIST
    assert lib.sd_files_rename(b(source),b'new name.bin')==0
    source=root/'new name.bin'
    assert lib.sd_files_rename(b(source),b'../escape')==errno.EINVAL
    CALLBACK=C.CFUNCTYPE(C.c_bool,C.c_uint,C.c_void_p)
    @CALLBACK
    def cancel(percent,user): return percent<10
    lib.sd_files_copy_progress.argtypes=[C.c_char_p,C.c_char_p,CALLBACK,C.c_void_p]
    assert lib.sd_files_copy_progress(b(source),b(root/'folder A'),cancel,None)==errno.ECANCELED
    assert not (root/'folder A'/source.name).exists()
    assert not list((root/'folder A').glob('*.part'))
    text=C.create_string_buffer(32); truncated=C.c_bool()
    assert lib.sd_files_preview(b(source),text,32,C.byref(truncated))==errno.EILSEQ
    (root/'text.txt').write_text('hello\n'*30)
    assert lib.sd_files_preview(b(root/'text.txt'),text,32,C.byref(truncated))==0 and truncated.value
    assert lib.sd_files_delete(b(dest))==errno.ENOTEMPTY
    assert lib.sd_files_delete(b(root))==errno.EINVAL
    assert lib.sd_files_delete(b(source))==0
    tree=root/'tree'; (tree/'sub').mkdir(parents=True); (tree/'sub'/'data.txt').write_text('nested')
    assert lib.sd_files_copy(b(tree),b(dest))==0
    assert (dest/'tree'/'sub'/'data.txt').read_text()=='nested'
    assert lib.sd_files_copy(b(tree),b(tree/'sub'))==errno.EINVAL
    assert lib.sd_files_copy(b(tree),b(dest))==errno.EEXIST
    assert lib.sd_files_copy_progress(b(tree),b(root/'folder B'),cancel,None)==errno.ECANCELED
    assert not (root/'folder B'/'tree').exists() and not (root/'folder B'/'tree.part').exists()
    lib.sd_files_edit.argtypes=[C.c_char_p,C.c_char_p,C.c_char_p]
    editable=root/'edit.txt'; editable.write_text('original')
    assert lib.sd_files_edit(b(editable),b'original',b'new content')==0
    assert editable.read_text()=='new content' and not Path(str(editable)+'.previous').exists()
    assert lib.sd_files_edit(b(editable),b'original',b'stale')==errno.ESTALE
    assert editable.read_text()=='new content'
    assert lib.sd_files_edit(b(editable),b'new content',b'again')==0
    assert editable.read_text()=='again' and not Path(str(editable)+'.previous').exists()
    sibling=Path(str(editable)+'.previous');sibling.write_text('user-owned backup')
    assert lib.sd_files_edit(b(editable),b'again',b'overwrite')==errno.EEXIST
    assert sibling.read_text()=='user-owned backup' and editable.read_text()=='again'
    sibling.unlink()
    editable.write_bytes(b'a'*4096)
    assert lib.sd_files_edit(b(editable),b'a'*4095,b'x')==errno.ESTALE
    lib.sd_files_delete_recursive.argtypes=[C.c_char_p]
    assert lib.sd_files_delete_recursive(b(root))==errno.EINVAL
    outside=Path(tmp)/'outside';outside.mkdir();(outside/'safe').write_text('keep')
    (tree/'link').symlink_to(outside,target_is_directory=True)
    assert lib.sd_files_delete_recursive(b(tree))==errno.EINVAL
    assert (tree/'sub'/'data.txt').exists() and (outside/'safe').exists()
    (tree/'link').unlink()
    assert lib.sd_files_delete_recursive(b(tree))==0 and not tree.exists()
    assert (outside/'safe').exists()
    deep=root/'too-deep';deep.mkdir();leaf=deep
    for i in range(18):leaf=leaf/'d';leaf.mkdir()
    (deep/'preserved').write_text('keep')
    assert lib.sd_files_delete_recursive(b(deep))==errno.ELOOP
    assert (deep/'preserved').exists()
print('SD files: sorted pagination, integrity/cancel, stale edits and recursive deletion preflight/containment passed (UBSan).')
