from pathlib import Path
import sys, tempfile, unittest, subprocess, json
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'app'))
import ordenador as o
c=o.c

def fixture(header='CE3Xv3.2 Residencial'):
    walls=[[name,kind]+[0]*11 for name,kind in [('Fachada norte','Fachada'),('Cubierta','Cubierta'),('Fachada sur','Fachada')]]
    shared=[c.LegacyString('datos comunes'),3.14]
    windows=[]
    for name,parent in [('Norte 1','Fachada norte'),('Sur 1','Fachada sur'),('Norte 2','Fachada norte')]:
        w=c.CexObject(c.GlobalRef('Envolvente.objetosEnvolvente','HuecoConocidas'))
        w.state={'descripcion':name,'cerramientoAsociado':parent,'superficie':2.3,'extra':shared}
        windows.append(w)
    bridges=[[name,'PT',0,1.2,5,False,'defecto',parent,'Edificio Objeto'] for name,parent in [('Pilar norte','Fachada norte'),('Pilar sur','Fachada sur'),('Forjado norte','Fachada norte')]]
    return [c.LegacyString(header),{'datos':'áéñ'},[123],[walls,windows,bridges,[]]]+[[{'conservar':i,'valor':c.LegacyString('sin tocar')} ] for i in range(11)]

class OrderTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
        self.path=self.root/'Vivienda ñ.cex';self.path.write_bytes(c.encoded(fixture()))
        self.original=self.path.read_bytes();self.doc=o.Document(self.path)
    def tearDown(self):self.temp.cleanup()
    def test_noop_exact(self):
        dest=self.root/'copy.cex';self.doc.save(dest);self.assertEqual(dest.read_bytes(),self.original)
    def test_wall_and_children(self):
        self.doc.place('e2',direction='first');self.assertEqual(self.doc.order,[[2,0,1],[1,0,2],[1,0,2]])
        dest=self.doc.save(self.root/'out.cex');new=o.Document(dest)
        self.assertEqual(new.names,['Fachada sur','Fachada norte','Cubierta'])
        self.assertEqual(new.env[1][0].state['cerramientoAsociado'],'Fachada sur')
        self.assertEqual(self.path.read_bytes(),self.original)
        a=o.raw_blocks(self.original);b=o.raw_blocks(dest.read_bytes())
        self.assertEqual([a[i] for i in range(15) if i!=3],[b[i] for i in range(15) if i!=3])
    def test_window_within_wall(self):
        self.doc.place('w2',direction='up');self.assertEqual(self.doc.order[1],[2,1,0])
        self.doc.save(self.root/'out.cex')
    def test_bridge(self):
        self.doc.place('b0',direction='last');self.assertEqual(self.doc.order[2],[2,1,0]);self.doc.save(self.root/'out.cex')
    def test_drag(self):
        self.doc.place('e2',before='e0');self.assertEqual(self.doc.order[0],[2,0,1])
    def test_reparent_rejected(self):
        for a,b in [('w0','w1'),('b0','b1'),('e0','w0')]:
            with self.assertRaises(c.ConversionError):self.doc.place(a,before=b)
        self.assertFalse(self.doc.dirty)
    def test_undo_redo_savepoint(self):
        self.doc.place('e2',direction='first');self.doc.undo();self.assertFalse(self.doc.dirty)
        self.doc.redo();self.assertTrue(self.doc.dirty);self.doc.save(self.root/'out.cex');self.assertFalse(self.doc.dirty)
        self.doc.undo();self.assertTrue(self.doc.dirty);self.doc.redo();self.assertFalse(self.doc.dirty)
    def test_noop_does_not_clear_redo(self):
        self.doc.place('e2',direction='first');self.doc.undo();self.doc.place('e0',direction='up');self.assertTrue(self.doc.redo_stack)
    def test_original_and_existing_protected(self):
        with self.assertRaises(c.ConversionError):self.doc.save(self.path,True)
        out=self.root/'out.cex';out.write_bytes(b'existing')
        with self.assertRaises(c.ConversionError):self.doc.save(out)
        self.assertEqual(out.read_bytes(),b'existing');self.doc.save(out,True);self.assertEqual(out.read_bytes(),self.original)
    def test_hardlink_protected(self):
        alias=self.root/'alias.cex'
        try:alias.hardlink_to(self.path)
        except OSError:self.skipTest('hardlinks unavailable')
        with self.assertRaises(c.ConversionError):self.doc.save(alias,True)
    def test_external_change(self):
        self.path.write_bytes(self.original+b' ')
        with self.assertRaises(c.ConversionError):self.doc.save(self.root/'out.cex')
        self.assertFalse((self.root/'out.cex').exists())
    def test_version_23_and_lf(self):
        self.path.write_bytes(c.encoded(fixture('CEXv2.3 Residencial')).replace(b'\r\n',b'\n'))
        d=o.Document(self.path);d.place('e2',direction='first');out=d.save(self.root/'out.cex')
        self.assertEqual(str(c.read_cex(out)[0]),'CEXv2.3 Residencial')
    def test_duplicates_blocked(self):
        blocks=fixture();blocks[3][0][2][0]='Fachada norte';self.path.write_bytes(c.encoded(blocks))
        with self.assertRaises(c.ConversionError):o.Document(self.path)
    def test_orphans_visible(self):
        blocks=fixture();blocks[3][1][0].state['cerramientoAsociado']='Eliminado';self.path.write_bytes(c.encoded(blocks));d=o.Document(self.path)
        self.assertIn(('orphans','root','Sin cerramiento reconocido'),d.nodes());d.place('e2',direction='first');d.save(self.root/'out.cex')
    def test_shared_references(self):
        self.doc.place('w2',direction='first');out=self.doc.save(self.root/'out.cex');w=c.read_cex(out)[3][1]
        self.assertIs(w[0].state['extra'],w[1].state['extra'])
    def test_pickle_code_never_executed(self):
        marker=self.root/'MUST_NOT_EXIST';payload=b"cos\nsystem\n(S'echo bad'\ntR."
        self.path.write_bytes(c.encoded([c.LegacyString('CE3Xv3.2 Residencial')])+payload+c.encoded([[]]*13))
        with self.assertRaises(c.ConversionError):o.Document(self.path)
        self.assertFalse(marker.exists())
    def test_worker_paths_unicode_and_error_recovery(self):
        out=self.root/'salida ñ.cex'
        requests=[{'command':'open','path':str(self.path)},{'command':'move','id':'w0','before':'w1'},{'command':'move','id':'e2','direction':'first'},{'command':'save','path':str(out)},{'command':'exit'}]
        p=subprocess.run([sys.executable,'-I','-X','utf8','-B',str(Path(o.__file__)),'serve'],input=''.join(json.dumps(x)+'\n' for x in requests),capture_output=True,text=True,encoding='utf-8')
        self.assertEqual(p.returncode,0,p.stderr);lines=p.stdout.splitlines();self.assertEqual(len(lines),4,p.stdout)
        self.assertTrue(lines[1].startswith('ERROR\t'));self.assertTrue(lines[3].startswith('OK\t'));self.assertEqual(o.Document(out).names[0],'Fachada sur')
    def test_truncated_unknown_version(self):
        self.path.write_bytes(self.original[:-1])
        with self.assertRaises(c.ConversionError):o.Document(self.path)
        blocks=fixture('Otra version');self.path.write_bytes(c.encoded(blocks))
        with self.assertRaises(c.ConversionError):o.Document(self.path)

if __name__=='__main__':unittest.main()
