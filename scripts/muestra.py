"""Muestra sintetica exclusivamente para pruebas y captura de interfaz."""
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'fuentes'))
from test_ordenador import fixture,c
out=Path(sys.argv[1]);out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(c.encoded(fixture()))
