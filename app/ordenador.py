"""Reordena exclusivamente las tres listas de elementos de la envolvente CE3X."""
from pathlib import Path
import argparse
import copy
import hashlib
import io
import json
import os
import re
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parent))
import cex_codec as c

VERSION='0.3.0'
UPDATE_API='https://api.github.com/repos/xSeeSx/Ordenador-de-elementos-CE3X/releases/latest'
HEADERS={'CEXv2.3 Residencial','CE3Xv3.2 Residencial'}


def check_updates():
    """Consulta pública acotada; no lee ni transmite certificados o diagnósticos."""
    import urllib.request
    try:
        request = urllib.request.Request(UPDATE_API, headers={
            'Accept':'application/vnd.github+json', 'User-Agent':'Ordenador-CE3X/'+VERSION})
        with urllib.request.urlopen(request, timeout=4) as response:
            raw = response.read(131073)
        if len(raw) > 131072:
            raise ValueError('Respuesta demasiado larga')
        release = json.loads(raw)
        tag = release.get('tag_name', '')
        match = re.fullmatch(r'v(\d{1,5})\.(\d{1,5})\.(\d{1,5})', tag)
        if not match or release.get('draft') is not False or release.get('prerelease') is not False:
            raise ValueError('No es una version publica estable')
        version = '.'.join(match.groups())
        package = 'Ordenador_CE3X_Windows_'+version.replace('.', '_')+'.zip'
        if not any(asset.get('name') == package and asset.get('state') == 'uploaded'
                   and isinstance(asset.get('size'), int) and asset['size'] > 0
                   for asset in release.get('assets', [])):
            raise ValueError('No hay paquete de Windows publicado')
        status = 'UPDATE' if tuple(map(int, match.groups())) > tuple(map(int, VERSION.split('.'))) else 'CURRENT'
        return status+'\t'+version
    except Exception:
        return 'UNAVAILABLE\t'



def raw_blocks(data):
    normalized=data.replace(b'\r\n',b'\n');ns=rs=0;parts=[]
    while ns<len(normalized):
        end=None
        for op,arg,pos in c.protocol0_ops(normalized[ns:]):
            if op.name=='STOP':end=ns+pos+1
        c.require(end is not None,'E_FORMAT','Bloque incompleto')
        re_=rs+(end-ns);scan=rs
        while True:
            cr=data.find(b'\r\n',scan)
            if cr<0 or cr>=re_:break
            re_+=1;scan=cr+2
        part=data[rs:re_]
        c.require(part.replace(b'\r\n',b'\n')==normalized[ns:end],'E_FORMAT','Limites de bloque inconsistentes')
        parts.append(part);ns=end;rs=re_
    c.require(len(parts)==15 and b''.join(parts)==data,'E_FORMAT','Se esperaban 15 bloques')
    return parts


class Document:
    def __init__(self,path):
        self.path=Path(path).resolve()
        c.require(self.path.stat().st_size<=c.MAX_BYTES,'E_SIZE','El archivo supera 64 MiB')
        self.raw=self.path.read_bytes()
        c.require(len(self.raw)<=c.MAX_BYTES,'E_SIZE','El archivo supera 64 MiB')
        self.hash=hashlib.sha256(self.raw).hexdigest()
        self.blocks=c.read_cex(self.path);self.chunks=raw_blocks(self.raw)
        c.require(c.sha(self.path)==self.hash,'E_CHANGED','El origen cambio durante la lectura')
        c.require(str(self.blocks[0]) in HEADERS,'E_VERSION','Solo se admiten CEX residenciales 2.3 y 3.2 contrastados')
        self.env=self.blocks[3]
        c.require(isinstance(self.env,list) and len(self.env)==4 and all(isinstance(x,list) for x in self.env),
                  'E_ENVELOPE','Envolvente desconocida')
        c.require(len({id(x) for x in self.env[:3]})==3,'E_ENVELOPE','Listas de elementos compartidas no admitidas')
        self.names=[]
        for row in self.env[0]:
            c.require(isinstance(row,list) and len(row)>=13 and isinstance(row[0],str),'E_ENVELOPE','Cerramiento desconocido')
            self.names.append(str(row[0]))
        duplicates=sorted({name for name in self.names if self.names.count(name)>1})
        c.require(not duplicates,'E_NAMES','Hay cerramientos con nombres duplicados: '+', '.join(repr(n) for n in duplicates)+'. Renómbralos en CE3X y vuelve a abrir el archivo.')
        self.parents=[None,[],[]]
        for w in self.env[1]:
            c.require(isinstance(w,c.CexObject) and isinstance(w.state.get('cerramientoAsociado'),str)
                      and isinstance(w.state.get('descripcion'),str),'E_WINDOW','Hueco desconocido')
            self.parents[1].append(str(w.state['cerramientoAsociado']))
        for p in self.env[2]:
            c.require(isinstance(p,list) and len(p)==9 and isinstance(p[7],str),'E_BRIDGE','Puente termico desconocido')
            self.parents[2].append(str(p[7]))
        self.order=[list(range(len(g))) for g in self.env[:3]]
        self.initial=copy.deepcopy(self.order);self.saved=copy.deepcopy(self.order)
        self.undo_stack=[];self.redo_stack=[]
    @property
    def dirty(self):return self.order!=self.saved
    def identify(self,key):
        c.require(isinstance(key,str) and re.fullmatch(r'[ewb][0-9]+',key),'E_SELECTION','Selecciona un elemento')
        kind='ewb'.index(key[0]);idx=int(key[1:])
        c.require(idx<len(self.env[kind]),'E_SELECTION','Elemento inexistente')
        return kind,idx
    def element_label(self,kind,idx):
        row=self.env[kind][idx]
        name=row.state['descripcion'] if kind==1 else row[0]
        label=('cerramiento','hueco','puente térmico')[kind]+' '+repr(str(name))
        if kind:label+=' (cerramiento '+repr(self.parents[kind][idx])+')'
        return label
    def siblings(self,kind,idx):
        return self.order[kind] if kind==0 else [i for i in self.order[kind] if self.parents[kind][i]==self.parents[kind][idx]]
    def coordinate(self):
        rank={self.names[i]:pos for pos,i in enumerate(self.order[0])}
        for g in (1,2):self.order[g].sort(key=lambda i:rank.get(self.parents[g][i],len(rank)))
    def place(self,key,before=None,direction=None):
        kind,idx=self.identify(key);siblings=self.siblings(kind,idx);pos=siblings.index(idx)
        if before:
            k,j=self.identify(before)
            c.require(k==kind and j in siblings,'E_PARENT','No se puede mover '+self.element_label(kind,idx)+' delante de '+self.element_label(k,j)+'. Solo se pueden ordenar elementos del mismo tipo y cerramiento asociado.')
            target=siblings.index(j)
            if idx==j:return False
            reordered=siblings.copy();reordered.remove(idx);reordered.insert(reordered.index(j),idx)
        else:
            c.require(direction in ('up','down','first','last'),'E_MOVE','Movimiento desconocido')
            target={'up':max(0,pos-1),'down':min(len(siblings)-1,pos+1),'first':0,'last':len(siblings)-1}[direction]
            reordered=siblings.copy();reordered.insert(target,reordered.pop(pos))
        if reordered==siblings:return False
        old=copy.deepcopy(self.order)
        it=iter(reordered);slots=set(siblings)
        self.order[kind]=[next(it) if i in slots else i for i in self.order[kind]]
        if kind==0:self.coordinate()
        self.undo_stack.append(old);self.redo_stack.clear();return True
    def undo(self):
        if self.undo_stack:self.redo_stack.append(self.order);self.order=self.undo_stack.pop()
    def redo(self):
        if self.redo_stack:self.undo_stack.append(self.order);self.order=self.redo_stack.pop()
    def nodes(self):
        nodes=[('root','-',str(self.blocks[0]))]
        def children(parent,name):
            for group,prefix,label in [(1,'w','Huecos'),(2,'b','Puentes térmicos')]:
                ids=[i for i in self.order[group] if self.parents[group][i]==name]
                if not ids:continue
                folder=prefix+'g'+parent;nodes.append((folder,parent,label+' ('+str(len(ids))+')'))
                for i in ids:
                    row=self.env[group][i];title=row.state['descripcion'] if group==1 else row[0]
                    nodes.append((prefix+str(i),folder,str(title)))
        for i in self.order[0]:
            key='e'+str(i);row=self.env[0][i]
            nodes.append((key,'root',str(row[0])+' · '+str(row[1])));children(key,self.names[i])
        orphans=sorted(set(self.parents[1]+self.parents[2])-set(self.names))
        if orphans:
            nodes.append(('orphans','root','Sin cerramiento reconocido'))
            for j,name in enumerate(orphans):
                key='o'+str(j);nodes.append((key,'orphans',name));children(key,name)
        return nodes
    def save(self,destination,overwrite=False):
        destination=Path(destination).resolve()
        c.require(destination!=self.path,'E_ORIGINAL','Guarda una copia con otro nombre; el original se conserva')
        c.require(not destination.exists() or not os.path.samefile(destination,self.path),'E_ORIGINAL','La salida apunta al original')
        c.require(overwrite or not destination.exists(),'E_EXISTS','La salida ya existe')
        c.require(c.sha(self.path)==self.hash,'E_CHANGED','El original ha cambiado; vuelve a abrirlo')
        for g in range(3):c.require(sorted(self.order[g])==list(range(len(self.env[g]))),'E_ORDER','Orden no valido')
        if self.order==self.initial:result=self.raw
        else:
            envelope=list(self.env)
            for g in range(3):envelope[g]=[self.env[g][i] for i in self.order[g]]
            pieces=list(self.chunks);pieces[3]=c.encoded([envelope]);result=b''.join(pieces)
        tmp=None
        try:
            with tempfile.NamedTemporaryFile(dir=destination.parent,prefix='.ordenador_',suffix='.partial',delete=False) as f:
                tmp=Path(f.name);f.write(result)
            checked=c.read_cex(tmp);chunks=raw_blocks(result)
            c.require(all(chunks[i]==self.chunks[i] for i in range(15) if i!=3),'E_PRESERVATION','Se ha alterado un bloque ajeno a la envolvente')
            restored=list(checked[3])
            for g in range(3):
                inverse={original:pos for pos,original in enumerate(self.order[g])}
                restored[g]=[checked[3][g][inverse[i]] for i in range(len(self.env[g]))]
            c.require(c.typed_value(restored)==c.typed_value(self.env),'E_PRESERVATION','Propiedades, tipos o referencias alterados')
            c.require(c.sha(self.path)==self.hash,'E_CHANGED','El original cambio durante el guardado')
            if overwrite:os.replace(tmp,destination)
            elif os.name=='nt':os.rename(tmp,destination)
            else:os.link(tmp,destination);tmp.unlink()
            self.saved=copy.deepcopy(self.order)
        finally:
            if tmp is not None:tmp.unlink(missing_ok=True)
        return destination


def wire(document,selected='-',message=''):
    hx=lambda s:str(s).encode('utf-8').hex()
    rows=';'.join(','.join((key,parent,hx(label))) for key,parent,label in document.nodes()) if document else '-'
    flags=(int(document.dirty),int(bool(document.undo_stack)),int(bool(document.redo_stack))) if document else (0,0,0)
    return '\t'.join(['OK',selected or '-',','.join(map(str,flags)),hx(message) or '-',rows])+'\n'


def human_error(exc, command):
    if isinstance(exc,c.ConversionError):
        if exc.code in ('E_FORMAT','E_VERSION','E_PROTOCOL','E_TRUNCATED','E_BLOCK_COUNT','E_READ','E_OFFSET'):
            return 'No se puede abrir o verificar este certificado. Comprueba que sea un archivo CEX residencial de CE3X 2.3 o 3.2 y que no esté dañado. No se ha guardado ningún cambio.'
        if exc.code in ('E_PICKLE_REDUCE','E_PICKLE_INST_ARGS','E_PICKLE_STATE','E_PICKLE_OPCODE','E_ENCODING','E_GLOBAL_NAME','E_OBJECT_STYLE'):
            return 'El certificado contiene una estructura que esta versión no reconoce. No se ha guardado ningún cambio.'
        if exc.code=='E_PRESERVATION':
            return 'La comprobación de seguridad ha detectado cambios distintos del orden. No se ha guardado la copia. El original se conserva.'
        return str(exc)
    if isinstance(exc,FileNotFoundError):
        return 'No se encuentra el archivo de origen o la carpeta de destino. Comprueba la ruta y vuelve a intentarlo.'
    if isinstance(exc,PermissionError):
        return 'No hay permiso para '+('guardar la copia. Cierra el archivo de destino si está abierto o elige otra carpeta.' if command=='save' else 'leer el archivo. Comprueba que esté accesible.')
    if isinstance(exc,OSError):
        return 'No se pudo '+('guardar la copia' if command=='save' else 'leer el archivo')+'. Comprueba el acceso y el espacio disponible.'
    return 'No se pudo completar la operación. El original no se modifica. Si el problema persiste, comunica la incidencia indicando qué acción estabas realizando.'


def serve():
    doc=None
    for line in sys.stdin:
        selected='-';cmd=''
        try:
            req=json.loads(line);cmd=req['command'];message=''
            if cmd=='exit':return
            if cmd=='open':doc=Document(req['path']);message='Archivo abierto. Arrastra o utiliza Subir y Bajar.'
            else:
                c.require(doc is not None,'E_OPEN','Primero abre un archivo')
                if cmd=='move':
                    selected=req['id'];doc.place(selected,before=req.get('before'),direction=req.get('direction'))
                elif cmd=='undo':doc.undo()
                elif cmd=='redo':doc.redo()
                elif cmd=='save':message='Copia guardada: '+str(doc.save(req['path'],req.get('overwrite',False)))
                elif cmd!='snapshot':raise ValueError('Operacion desconocida')
            sys.stdout.write(wire(doc,selected,message))
        except Exception as exc:
            sys.stdout.write('ERROR\t'+human_error(exc,cmd).encode('utf-8').hex()+'\n')
        sys.stdout.flush()


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('command',choices=['serve','check-updates']);args=parser.parse_args()
    if args.command=='check-updates':print(check_updates())
    else:serve()
