"""Empaqueta exclusivamente la aplicacion y el runtime, sin proyectos CEX."""
from pathlib import Path
import hashlib,json,zipfile
root=Path(__file__).resolve().parents[1]
files={name:(root/name).read_bytes() for name in ['Ordenador_CE3X.exe','app/cex_codec.py','app/ordenador.py','README.md','CHANGELOG.md']}
for name in ['python.exe','python311.dll','python311.zip','python311._pth','LICENSE.txt']:
    if not (root/'runtime'/name).is_file():raise FileNotFoundError(name)
for path in sorted((root/'runtime').rglob('*')):
    if path.is_file() and '__pycache__' not in path.parts and path.suffix!='.pyc':files[path.relative_to(root).as_posix()]=path.read_bytes()
files['LEEME.txt']=('ORDENADOR DE ELEMENTOS CE3X 0.1.0\n\nWindows 10/11 de 64 bits. Python incluido.\nExtrae todo el ZIP y abre Ordenador_CE3X.exe.\nAbre tu .cex residencial 2.3 o 3.2, selecciona un elemento y utiliza\nSubir, Bajar, Al inicio, Al final o arrastra delante de otro elemento.\nLos huecos y puentes solo se ordenan dentro de su cerramiento.\nDeshacer: Ctrl+Z. Rehacer: Ctrl+Y. Guardar copia: Ctrl+S.\nLa salida propuesta es nombre_ORDENADO.cex junto al original.\nEl original se conserva y no cambia la version del archivo.\nNo se generan logs automaticamente. No cambian propiedades ni calculos.\nHerramienta experimental, pendiente de comprobacion visual en CE3X.\nComprueba el orden del arbol y del informe al abrir el resultado.\nConserva app/ y runtime/ junto al ejecutable.\nLicencia de Python: runtime/LICENSE.txt\n').encode('utf-8')
hashes={name:hashlib.sha256(data).hexdigest() for name,data in sorted(files.items())}
files['SHA256.json']=(json.dumps(hashes,indent=2)+'\n').encode()
out=root/'_release';out.mkdir(exist_ok=True);dest=out/'Ordenador_CE3X_Windows_0_1_0.zip';prefix='Ordenador_CE3X_0_1_0/'
with zipfile.ZipFile(dest,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
    for name,data in sorted(files.items()):z.writestr(prefix+name,data)
with zipfile.ZipFile(dest) as z:
    assert z.testzip() is None
    assert not any(n.lower().endswith('.cex') for n in z.namelist())
    for name,digest in hashes.items():assert hashlib.sha256(z.read(prefix+name)).hexdigest()==digest
(out/'SHA256SUMS.txt').write_text(hashlib.sha256(dest.read_bytes()).hexdigest()+'  '+dest.name+'\n',encoding='ascii')
print(dest)
