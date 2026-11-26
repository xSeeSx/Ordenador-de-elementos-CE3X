"""Consulta de versiones sin depender de la red en las pruebas."""
import io
import json
from pathlib import Path
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'app'))
import ordenador as c


class UpdateTests(unittest.TestCase):
    def release(self, tag):
        return {'tag_name':tag, 'draft':False, 'prerelease':False,
                'assets':[{'name':'Ordenador_CE3X_Windows_'+tag[1:].replace('.', '_')+'.zip',
                           'state':'uploaded', 'size':100}]}

    def check(self, payload):
        with mock.patch('urllib.request.urlopen', return_value=io.BytesIO(json.dumps(payload).encode())) as fetch:
            result = c.check_updates()
            request = fetch.call_args.args[0]
            self.assertEqual(request.full_url, c.UPDATE_API)
            self.assertIsNone(request.data)
            self.assertEqual(fetch.call_args.kwargs['timeout'], 4)
            return result

    def test_numeric_version_order_and_no_downgrades(self):
        with mock.patch.object(c, 'VERSION', '1.9.0'):
            self.assertEqual(self.check(self.release('v1.10.0')), 'UPDATE\t1.10.0')
            self.assertEqual(self.check(self.release('v1.9.0')), 'CURRENT\t1.9.0')
            self.assertEqual(self.check(self.release('v1.8.5')), 'CURRENT\t1.8.5')

    def test_ignore_drafts_previews_and_missing_packages(self):
        for changed in ({'draft':True}, {'prerelease':True}, {'assets':[]},
                        {'tag_name':'v9.0.0-beta'}, {'tag_name':'v9.0.0\nUPDATE\tmalicious'},
                        {'assets':[{'name':'Ordenador_CE3X_Windows_9_0_0.zip','size':1,'state':'new'}]}):
            with self.subTest(changed=changed):
                release = self.release('v9.0.0'); release.update(changed)
                self.assertEqual(self.check(release), 'UNAVAILABLE\t')

    def test_offline_rate_limit_and_invalid_json_are_nonfatal(self):
        for error in (TimeoutError(), OSError('sin conexión')):
            with mock.patch('urllib.request.urlopen', side_effect=error):
                self.assertEqual(c.check_updates(), 'UNAVAILABLE\t')
        for body in (b'not json', b'[]', b'x'*131073):
            with mock.patch('urllib.request.urlopen', return_value=io.BytesIO(body)):
                self.assertEqual(c.check_updates(), 'UNAVAILABLE\t')


class HumanErrorTests(unittest.TestCase):
    def test_friendly_errors(self):
        self.assertIn('original', c.human_error(c.c.ConversionError('E_PRESERVATION','internal'), 'save'))
        self.assertIn('carpeta', c.human_error(PermissionError(), 'save'))
        self.assertNotIn('internal', c.human_error(ValueError('internal'), 'open'))

    def test_named_move_error(self):
        from test_ordenador import fixture
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'sample.cex'
            path.write_bytes(c.c.encoded(fixture()))
            d=c.Document(path)
            with self.assertRaises(c.c.ConversionError) as caught:
                d.place('w0', before='w1')
            message=c.human_error(caught.exception,'move')
            for name in ('Norte 1','Sur 1','Fachada norte','Fachada sur'):
                self.assertIn(name,message)
            self.assertFalse(d.dirty)
