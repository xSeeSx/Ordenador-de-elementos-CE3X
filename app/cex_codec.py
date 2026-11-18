"""Lector y escritor inertes CE3X. Adaptado del proyecto Conversor CE3X, mismo autor.
No importa clases de CE3X ni ejecuta funciones contenidas en los pickle.
"""
from __future__ import annotations
import argparse
import codecs
import copy
import datetime as dt
from decimal import Decimal
import hashlib
import io
import json
import math
import os
from pathlib import Path
import pickle
import pickletools
import platform
import re
import sys
import traceback
import tempfile
import zipfile
from dataclasses import dataclass, field

VERSION = '0.1.0'
ROOT = Path(__file__).resolve().parent
MAX_BYTES = 64 * 1024 * 1024
BLOCK_NAMES = ['version', 'administrativos', 'generales', 'envolvente', 'instalaciones',
               'medidas_mejora', 'analisis_economico', 'subgrupos', 'composiciones',
               'sombras', 'nombres_sombras', 'informe', 'complementos', 'reservado_13', 'reservado_14']

class ConversionError(Exception):
    def __init__(self, code, message, path=''):
        super().__init__(message)
        self.code, self.path = code, path

class LegacyString(str):
    """STRING de Python 2, separado de UNICODE para conservar la codificacion."""

@dataclass(frozen=True)
class GlobalRef:
    module: str
    name: str

@dataclass
class CexObject:
    cls: GlobalRef
    state: dict = field(default_factory=dict)
    style: str = 'inst'

RECONSTRUCTOR = GlobalRef('copy_reg', '_reconstructor')
OBJECT = GlobalRef('__builtin__', 'object')

class InertUnpickler(pickle._Unpickler):
    """Solo crea datos inertes. GLOBAL nunca resuelve ni importa codigo."""
    dispatch = pickle._Unpickler.dispatch.copy()

    def find_class(self, module, name):
        return GlobalRef(module, name)

    def _decode_string(self, value):
        return LegacyString(value.decode('latin1'))

    def load_reduce(self):
        args = self.stack.pop()
        func = self.stack.pop()
        if func == RECONSTRUCTOR and isinstance(args, tuple) and len(args) == 3:
            cls, base, state = args
            if isinstance(cls, GlobalRef) and base == OBJECT and state is None:
                self.append(CexObject(cls, style="reduce"))
                return
        if func == GlobalRef('decimal', 'Decimal') and isinstance(args, tuple) and len(args)==1 and isinstance(args[0], str):
            require(len(args[0]) <= 1000, 'E_DECIMAL', 'Decimal demasiado largo')
            value=Decimal(args[0])
            require(value.is_finite(), 'E_DECIMAL', 'Decimal no finito')
            self.append(value)
            return
        raise ConversionError('E_PICKLE_REDUCE', 'Funcion serializada no admitida: %r' % (func,))
    dispatch[pickle.REDUCE[0]] = load_reduce

    def load_inst(self):
        module = self.readline()[:-1].decode("ascii")
        name = self.readline()[:-1].decode("ascii")
        args = self.pop_mark()
        if args:
            raise ConversionError("E_PICKLE_INST_ARGS", "Constructor INST con argumentos no admitido")
        self.append(CexObject(GlobalRef(module, name), style="inst"))
    dispatch[pickle.INST[0]] = load_inst

    def load_build(self):
        state = self.stack.pop()
        obj = self.stack[-1]
        if not isinstance(obj, CexObject) or not isinstance(state, dict):
            raise ConversionError('E_PICKLE_STATE', 'Estado de objeto no reconocido')
        obj.state.update(state)
    dispatch[pickle.BUILD[0]] = load_build

    def forbidden(self):
        raise ConversionError('E_PICKLE_OPCODE', 'Operacion pickle no admitida')
    for _op in (pickle.OBJ, pickle.NEWOBJ, pickle.NEWOBJ_EX,
                pickle.EXT1, pickle.EXT2, pickle.EXT4, pickle.PERSID, pickle.BINPERSID):
        dispatch[_op[0]] = forbidden

class LegacyPickler(pickle._Pickler):
    dispatch = pickle._Pickler.dispatch.copy()

    def save_legacy_string(self, obj):
        # repr(bytes) produce escapes para S; nunca aplicar esos escapes a V.
        try:
            raw = str(obj).encode('latin1')
        except UnicodeEncodeError as exc:
            raise ConversionError('E_ENCODING', 'Cadena STRING fuera de Latin-1') from exc
        self.write(b'S' + repr(raw)[1:].encode('ascii') + b'\n')
        self.memoize(obj)
    dispatch[LegacyString] = save_legacy_string

    def save_decimal(self, obj):
        self.save(GlobalRef('decimal', 'Decimal'))
        self.save((str(obj),))
        self.write(pickle.REDUCE)
        self.memoize(obj)
    dispatch[Decimal] = save_decimal

    def save_ref(self, obj):
        if not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9.]*', obj.module) or not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', obj.name):
            raise ConversionError('E_GLOBAL_NAME', 'Nombre de clase invalido')
        self.write(b'c' + obj.module.encode('ascii') + b'\n' + obj.name.encode('ascii') + b'\n')
        self.memoize(obj)
    dispatch[GlobalRef] = save_ref

    def save_object(self, obj):
        if obj.style == "inst":
            self.write(b"(i" + obj.cls.module.encode("ascii") + b"\n" + obj.cls.name.encode("ascii") + b"\n")
        elif obj.style == "reduce":
            self.save(RECONSTRUCTOR)
            self.save((obj.cls, OBJECT, None))
            self.write(pickle.REDUCE)
        else:
            raise ConversionError("E_OBJECT_STYLE", "Estilo de objeto desconocido")
        self.memoize(obj)
        self.save(obj.state)
        self.write(pickle.BUILD)
    dispatch[CexObject] = save_object


def encoded(blocks):
    out = io.BytesIO()
    for value in blocks:
        LegacyPickler(out, protocol=0, fix_imports=False).dump(value)
    return out.getvalue().replace(b'\n', b'\r\n')


def protocol0_ops(data):
    """Inspeccion con STRING Latin-1 explicito en todas las versiones de Python.

    pickletools de Python 3.11 decodifica STRING como ASCII, mientras versiones
    posteriores usan Latin-1. CE3X guarda bytes de Python 2, tambien escapados.
    Se conserva el flujo original y sus offsets, sin parches globales a stdlib.
    """
    stream = io.BytesIO(data)
    while True:
        pos = stream.tell()
        code = stream.read(1)
        op = pickletools.code2op.get(code.decode('latin1'))
        if op is None:
            raise ConversionError('E_TRUNCATED' if not code else 'E_PICKLE_OPCODE',
                                  f'Operacion ausente o desconocida en offset {pos}: {code!r}')
        if op.proto > 0:
            raise ConversionError('E_PROTOCOL', 'Solo se admite pickle protocolo 0')
        if op.name == 'STRING':
            literal = pickletools.read_stringnl(stream, decode=False)
            arg = codecs.escape_decode(literal)[0].decode('latin1')
        else:
            arg = op.arg.reader(stream) if op.arg is not None else None
        yield op, arg, pos
        if op.name == 'STOP':
            return


def read_cex(path, audit=None, label='source'):
    path = Path(path)
    if path.stat().st_size > MAX_BYTES:
        raise ConversionError('E_SIZE', 'El archivo supera 64 MiB', str(path))
    original = path.read_bytes()
    # Perfil observado: protocolo 0 textual, con CRLF al guardarse en Windows.
    if original[:1] not in (b'S', b'V'):
        raise ConversionError('E_FORMAT', 'No es un CEX textual del perfil compatible', str(path))
    raw = original.replace(b'\r\n', b'\n')
    stream = io.BytesIO(raw)
    blocks = []
    while stream.tell() < len(raw):
        offset = stream.tell()
        if len(blocks) >= 15:
            raise ConversionError('E_BLOCK_COUNT', 'Hay datos despues del bloque 14', f'{label}[15]')
        if audit:
            audit.stage = f'leer {label} bloque {len(blocks)} ({BLOCK_NAMES[len(blocks)]}), offset LF {offset}'
        try:
            # Preinspeccion independiente. Rechaza protocolos binarios antes de leer.
            end = None
            for op, arg, pos in protocol0_ops(raw[offset:]):
                if op.name == 'STOP':
                    end = offset + pos + 1
            if end is None:
                raise ConversionError('E_TRUNCATED', 'Bloque incompleto')
            obj = InertUnpickler(stream, encoding='latin1').load()
            if stream.tell() != end:
                raise ConversionError('E_OFFSET', 'La lectura y la preinspeccion no coinciden')
            blocks.append(obj)
            if audit:
                audit.event('INFO', 'BLOCK_READ', audit.stage, size_lf=end-offset)
        except Exception as exc:
            if isinstance(exc, ConversionError):
                raise
            raise ConversionError('E_READ', f'No se puede leer el bloque {len(blocks)}: {exc}', f'{label}[{len(blocks)}] @ {offset}') from exc
    if len(blocks) != 15:
        raise ConversionError('E_BLOCK_COUNT', f'Se esperaban 15 bloques; encontrados {len(blocks)}')
    return blocks


def json_value(value, depth=0):
    if depth > 120:
        raise ConversionError('E_DEPTH', 'Anidamiento excesivo')
    if isinstance(value, Decimal): return {'decimal':str(value)}
    if isinstance(value, CexObject):
        return {'class': value.cls.module + '.' + value.cls.name,
                'state': json_value(value.state, depth+1)}
    if isinstance(value, GlobalRef):
        return {'global': value.module + '.' + value.name}
    if isinstance(value, dict):
        return {str(k): json_value(v, depth+1) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [json_value(v, depth+1) for v in value]
    if isinstance(value, str):
        return str(value)
    if value is None or isinstance(value, (bool, int, float)):
        return value
    raise ConversionError('E_TYPE', f'Tipo no soportado: {type(value).__name__}')


def typed_value(value, seen=None):
    """Representacion tipada de un grafo, incluidas referencias compartidas/ciclos."""
    if seen is None: seen = {}
    if isinstance(value, Decimal): return ['DECIMAL', str(value)]
    if isinstance(value, LegacyString): return ['STRING', str(value)]
    if isinstance(value, str): return ['UNICODE', value]
    if isinstance(value, GlobalRef): return ['GLOBAL', value.module, value.name]
    if isinstance(value, (CexObject, dict, list, tuple)):
        identity = id(value)
        if identity in seen: return ['REF', seen[identity]]
        number = len(seen)
        seen[identity] = number
        child = lambda x: typed_value(x, seen)
        if isinstance(value, CexObject):
            return ['OBJECT', number, value.style, child(value.cls), child(value.state)]
        if isinstance(value, dict): return ['DICT', number, [[child(k), child(v)] for k,v in value.items()]]
        if isinstance(value, list): return ['LIST', number, [child(v) for v in value]]
        if isinstance(value, tuple): return ['TUPLE', number, [child(v) for v in value]]
    return [type(value).__name__, value]


def write_json(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + '\n', encoding='utf-8')


def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def require(condition, code, text, path=''):
    if not condition: raise ConversionError(code, text, path)


def empty(value):
    return value is None or value is False or value == '' or value == []


def as_num(value, path, minimum=None):
    try:
        require(not isinstance(value, bool), 'E_NUMBER', 'Un booleano no es un numero', path)
        result = float(str(value).replace(',', '.'))
        require(math.isfinite(result), 'E_NUMBER', 'Numero no finito', path)
        if minimum is not None:
            require(result >= minimum, 'E_NUMBER', f'Valor menor que {minimum}', path)
        return result
    except (TypeError, ValueError) as exc:
        raise ConversionError('E_NUMBER', f'Valor numerico invalido: {value!r}', path) from exc

