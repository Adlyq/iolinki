import copy
import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / 'tools/iodd_cli.py'
NS = {'i': 'http://www.io-link.com/IODD/2010/10'}

sys.path.insert(0, str(ROOT / 'tools'))
import iodd_gen  # noqa: E402

# 参数只能来自 C 对象字典表；提供 OD 源的示例在此登记。
OD_SOURCES = {
    'switching_sensor': ROOT / 'examples/switching_sensor/switching_sensor_od.c',
    'conformance_device': ROOT / 'examples/conformance_device/conformance_device_od.c',
}


def scalar(index, datatype='IOLINK_OD_DT_U16', low='0U', high='0xFFFFU', default='0U'):
    return (f'IOLINK_OD_SCALAR({index}, {datatype}, IOLINK_OD_ACCESS_RW, '
            f'IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_VOLATILE, '
            f'IOLINK_OD_HANDLER_BACKEND, {low}, {high}, {default}, "T")')


class IoddCLI(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.xml = Path(self.tmp.name) / 'device.xml'
        self.config = json.loads((ROOT / 'examples/switching_sensor/device.json').read_text())
        self.od = OD_SOURCES['switching_sensor']

    def run_cli(self, *args, success=True):
        result = subprocess.run([sys.executable, str(CLI), *map(str, args)], capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result

    def write_od(self, entries, name='od.c'):
        path = Path(self.tmp.name) / name
        path.write_text('#include "iolinki/od.h"\n'
                        f'static const iolink_od_entry_t t[] = {{{entries}}};\n')
        return path

    def generate(self, config=None, od='default', ok=True):
        path = Path(self.tmp.name) / 'config.json'
        path.write_text(json.dumps(self.config if config is None else config))
        sources = [self.od] if od == 'default' else list(od or [])
        args = ['generate', path, '-o', self.xml]
        for source in sources:
            args += ['--od', source]
        return self.run_cli(*args, success=ok)

    def generate_od(self, entries, config=None, ok=True):
        return self.generate(config, od=[self.write_od(entries)], ok=ok)

    def test_generation_inspection_and_tamper_detection(self):
        self.generate()
        self.run_cli('validate', self.xml)
        info = json.loads(self.run_cli('inspect', self.xml).stdout)
        self.assertEqual(info['deviceId'], 5679)
        self.assertEqual([v['index'] for v in info['variables']], [256, 257, 258, 259])
        self.assertEqual(info['processData'][0]['in']['bitLength'], 24)
        tree = ET.parse(self.xml)
        self.assertEqual(tree.find('.//i:Variable/i:Datatype', NS).get('{http://www.w3.org/2001/XMLSchema-instance}type'), 'UIntegerT')
        self.xml.write_bytes(self.xml.read_bytes().replace(b'Switching sensor', b'Switching SENSOR', 1))
        self.run_cli('validate', self.xml, success=False)

    def test_duplicate_index_and_overlapping_process_bits_rejected(self):
        entry = scalar('0x0100U')
        self.generate_od(f'{entry}, {entry}', ok=False)
        self.assertFalse(self.xml.exists())
        cfg = copy.deepcopy(self.config)
        cfg['processData'][0]['in']['fields'][1]['bitOffset'] = 8
        self.generate(cfg, od=[], ok=False)
        self.assertFalse(self.xml.exists())

    def test_pack_is_reproducible_and_contains_manifest(self):
        self.generate()
        a, b = [Path(self.tmp.name) / n for n in ('a.zip', 'b.zip')]
        self.run_cli('pack', self.xml, '-o', a)
        self.run_cli('pack', self.xml, '-o', b)
        self.assertEqual(a.read_bytes(), b.read_bytes())
        with zipfile.ZipFile(a) as archive:
            self.assertEqual(set(archive.namelist()), {'device.xml', 'manifest.json'})
            self.assertIn('sha256', json.loads(archive.read('manifest.json'))['files'][0])

    def test_dangling_text_reference_rejected_even_with_correct_crc(self):
        self.generate()
        self.xml.write_bytes(self.xml.read_bytes().replace(b'textId="T_DeviceName"', b'textId="T_Missing"', 1))
        subprocess.run(['node', str(ROOT / 'tools/iodd_stamp.mjs'), 'write', str(self.xml)], check=True, capture_output=True)
        self.run_cli('validate', self.xml, success=False)

    def test_pack_rejects_resource_path_traversal(self):
        self.generate()
        data = self.xml.read_bytes().replace(b'<DeviceName ', b'<VendorLogo name="../outside.png" /><DeviceName ', 1)
        self.xml.write_bytes(data)
        subprocess.run(['node', str(ROOT / 'tools/iodd_stamp.mjs'), 'write', str(self.xml)], check=True, capture_output=True)
        self.run_cli('pack', self.xml, '-o', Path(self.tmp.name) / 'bad.zip', success=False)

    def test_generator_keeps_output_on_invalid_input(self):
        self.generate()
        original = self.xml.read_bytes()
        bad = self.write_od(scalar('0x0100U', low='100U', high='10U'))
        self.run_cli('generate', Path(self.tmp.name) / 'config.json', '-o', self.xml, '--od', bad,
                     success=False)
        self.assertEqual(self.xml.read_bytes(), original)

    def test_wrong_datatype_reference_rejected(self):
        self.generate()
        tree = ET.parse(self.xml)
        variable = tree.find('.//i:Variable', NS)
        variable.remove(variable.find('i:Datatype', NS))
        variable.insert(0, ET.Element('{'+NS['i']+'}DatatypeRef', {'datatypeId': 'D_missing'}))
        tree.write(self.xml, encoding='utf-8', xml_declaration=True)
        self.run_cli('validate', self.xml, success=False)

    def test_example_maps_and_boolean_fields(self):
        for name, device_id, output_bits in [('reference_device',5678,8), ('switching_sensor',5679,0)]:
            args = ['generate', ROOT / f'examples/{name}/device.json', '-o', self.xml]
            if name in OD_SOURCES:
                args += ['--od', OD_SOURCES[name]]
            self.run_cli(*args)
            info = json.loads(self.run_cli('inspect', self.xml).stdout)
            self.assertEqual(info['deviceId'], device_id)
            self.assertEqual(info['processData'][0]['in']['fields'][0], {'bitOffset':8,'bitLength':16})
            self.assertEqual(info['processData'][0].get('out',{}).get('bitLength',0), output_bits)
            tree = ET.parse(self.xml)
            self.assertEqual(tree.find('.//i:PhysicalLayer',NS).get('mSequenceCapability'), '11')
            if name == 'switching_sensor':
                self.assertEqual([v['defaultValue'] for v in info['variables']], ['5000','200','0',None])

    def test_invalid_test_configs_rejected_before_output(self):
        for test in [{'Config2': {'index': 24, 'testValue': '0x49'}},
                     {'Config1': {'index': 24, 'testValue': ','.join(['0x49'] * 13)}},
                     {'Config3': {'index': 24, 'testValue': '0x49'}},
                     {'Config1': {'index': 24, 'testValue': 'arbitrary'}}]:
            cfg = copy.deepcopy(self.config)
            cfg['testConfig'] = test
            path = Path(self.tmp.name) / 'invalid-tests.json'
            path.write_text(json.dumps(cfg))
            self.run_cli('generate', path, '-o', self.xml, '--od', self.od, success=False)
            self.assertFalse(self.xml.exists())

    def test_standard_index_collision_rejected(self):
        for index in ('18U', '19U', '24U'):
            self.generate_od(scalar(index), ok=False)
            self.assertFalse(self.xml.exists())

    def test_generated_business_rule_requirements(self):
        for name, value in [('reference_device', '0x00'), ('switching_sensor', '0x13,0x88')]:
            args = ['generate', ROOT / f'examples/{name}/device.json', '-o', self.xml]
            if name in OD_SOURCES:
                args += ['--od', OD_SOURCES[name]]
            self.run_cli(*args)
            tree = ET.parse(self.xml)
            refs = {n.get('id') for n in tree.findall('.//i:StdVariableRef', NS)}
            self.assertTrue({'V_DirectParameters_2', 'V_ProductName', 'V_ApplicationSpecificTag'} <= refs)
            self.assertEqual(tree.find('.//i:Config1', NS).attrib, {'index': '24', 'testValue': '0x49'})
            self.assertEqual(tree.find('.//i:Config2', NS).attrib, {'index': '256', 'testValue': value})
            self.assertEqual(len(tree.find('.//i:Config3', NS).get('testValue').split(',')), 13)
            if name == 'switching_sensor':
                dt = tree.find('.//i:Variable[@id="V_OD_0103"]/i:Datatype', NS)
                self.assertIsNone(dt.find('i:ValueRange', NS))
                self.assertEqual(dt.find('i:SingleValue', NS).get('value'), '1')

    def test_release_packages_use_canonical_names_and_exact_sources(self):
        subprocess.run([sys.executable, str(ROOT / 'tools/package_release_iodds.py'), self.tmp.name, '2.1.1'], check=True)
        for example, name in [('reference_device', 'counter'), ('switching_sensor', 'switching-sensor')]:
            with zipfile.ZipFile(Path(self.tmp.name) / f'iolinki-2.1.1-{name}-IODD.zip') as archive:
                filename = f'iolinki-{name}-20261002-IODD1.1.xml'
                self.assertEqual(set(archive.namelist()), {filename, 'manifest.json'})
                self.assertEqual(archive.read(filename), (ROOT / 'examples' / example / 'device.xml').read_bytes())
                if os.environ.get('IODD_CHECKER'):
                    staged = Path(self.tmp.name) / filename
                    staged.write_bytes(archive.read(filename))
                    result = subprocess.run([os.environ['IODD_CHECKER'], str(staged)], capture_output=True, text=True, timeout=45)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipUnless(os.environ.get('IODD_CHECKER'), 'set IODD_CHECKER to an operator-owned genuine Checker wrapper')
    def test_genuine_checker_accepts_examples_and_rejects_missing_ref(self):
        checker = os.environ['IODD_CHECKER']
        for example in ('reference_device', 'switching_sensor'):
            filename = Path(self.tmp.name) / f'iolinki-{example}-20261002-IODD1.1.xml'
            args = ['generate', ROOT / f'examples/{example}/device.json', '-o', filename]
            if example in OD_SOURCES:
                args += ['--od', OD_SOURCES[example]]
            self.run_cli(*args)
            positive = subprocess.run([checker, str(filename)], capture_output=True, text=True, timeout=45)
            self.assertEqual(positive.returncode, 0, positive.stdout + positive.stderr)
            filename.write_bytes(filename.read_bytes().replace(b'        <StdVariableRef id="V_ProductName" />\n', b''))
            subprocess.run(['node', str(ROOT / 'tools/iodd_stamp.mjs'), 'write', str(filename)], check=True, capture_output=True)
            negative = subprocess.run([checker, str(filename)], capture_output=True, text=True, timeout=45)
            self.assertNotEqual(negative.returncode, 0)
            self.assertIn('V_ProductName is missing', negative.stdout + negative.stderr)

    def test_shipped_xml_matches_configuration(self):
        for name, stem in (('reference_device', 'device'), ('switching_sensor', 'device'),
                           ('conformance_device', 'device')):
            base = ROOT / 'examples' / name
            args = ['generate', base / (stem + '.json'), '-o', self.xml]
            if name in OD_SOURCES:
                args += ['--od', OD_SOURCES[name]]
            self.run_cli(*args)
            self.assertEqual(self.xml.read_bytes(), (base / (stem + '.xml')).read_bytes(), name)

    def test_invalid_od_entries_rejected(self):
        cases = {
            'range': scalar('0x0100U', low='100U', high='10U'),
            'default': scalar('0x0100U', low='0U', high='0xFFFFU', default='70000U'),
            'type': scalar('0x0100U', datatype='IOLINK_OD_DT_UNKNOWN'),
            'width': ('IOLINK_OD_STRING(0x0100U, IOLINK_OD_ACCESS_RW, IOLINK_OD_WRITE_VOLATILE, '
                      'IOLINK_OD_STORAGE_VOLATILE, IOLINK_OD_HANDLER_BACKEND, 0U, "T")'),
        }
        for label, entry in cases.items():
            with self.subTest(label):
                self.generate_od(entry, ok=False)
                self.assertFalse(self.xml.exists(), label)
        cfg = copy.deepcopy(self.config)
        cfg['processData'][0]['in']['fields'][0]['datatype'] = 'UnknownT'
        self.generate(cfg, ok=False)
        self.assertFalse(self.xml.exists())

    def test_invalid_document_metadata_rejected(self):
        for key, value in [('releaseDate','tomorrow'), ('version','1'), ('vendorId',True),
                           ('variables', [])]:
            cfg = copy.deepcopy(self.config)
            cfg[key] = value
            path = Path(self.tmp.name) / 'bad.json'
            path.write_text(json.dumps(cfg))
            self.run_cli('generate', path, '-o', self.xml, '--od', self.od, success=False)

    def test_sensor_c_wire_data_and_parameter_services_match_iodd(self):
        """Decode bytes from the actual application C implementation using IODD offsets."""
        library = Path(self.tmp.name) / 'sensor.so'
        shim = Path(self.tmp.name) / 'sensor_shim.c'
        # 测试只链接示例的 OD 应用；这里用一个测试专用垫片把 ctypes 调用
        # 接到对象字典 API。
        shim.write_text(r"""
#include "switching_sensor.h"
#include "iolinki/od.h"
#include <stddef.h>

static switching_sensor_t g_sensor;
static iolink_od_app_t g_app;

void shim_init(void)
{
    switching_sensor_init(&g_sensor);
    switching_sensor_od_init(&g_app, &g_sensor);
}

const unsigned char* shim_pd(void) { return g_sensor.pd; }
int shim_sample(unsigned value, int valid)
{
    return switching_sensor_sample(&g_sensor, (unsigned short) value, valid != 0) ? 1 : 0;
}
unsigned shim_threshold(void) { return (unsigned) g_sensor.threshold; }

int shim_read(unsigned index, unsigned sub, unsigned char* out, size_t cap, size_t* len)
{
    iolink_od_ctx_t ctx = {0};
    const iolink_od_entry_t* entry = iolink_od_find(&g_app, (unsigned short) index,
                                                    (unsigned char) sub);
    ctx.app = &g_app;
    if (entry == 0) {
        return -1;
    }
    iolink_result_t r = iolink_od_read_value(&ctx, entry, (unsigned char) sub, out, cap);
    *len = (size_t) r.len;
    return r.code;
}

int shim_write(unsigned index, unsigned sub, const unsigned char* data, size_t len)
{
    iolink_od_ctx_t ctx = {0};
    const iolink_od_entry_t* entry = iolink_od_find(&g_app, (unsigned short) index,
                                                    (unsigned char) sub);
    ctx.app = &g_app;
    if (entry == 0) {
        return -1;
    }
    return iolink_od_write_value(&ctx, entry, (unsigned char) sub, data, len).code;
}
""")
        subprocess.run(['cc', '-shared', '-fPIC', '-I' + str(ROOT / 'include'),
                        '-I' + str(ROOT / 'examples/switching_sensor'),
                        str(ROOT / 'examples/switching_sensor/switching_sensor.c'),
                        str(ROOT / 'examples/switching_sensor/switching_sensor_od.c'),
                        str(ROOT / 'src/od.c'), str(ROOT / 'src/params.c'),
                        str(ROOT / 'src/device_info.c'), str(ROOT / 'src/platform.c'),
                        str(shim), '-o', str(library)], check=True)
        lib = ctypes.CDLL(str(library))
        lib.shim_sample.argtypes = [ctypes.c_uint, ctypes.c_int]
        lib.shim_sample.restype = ctypes.c_int
        lib.shim_read.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p,
                                  ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
        lib.shim_read.restype = ctypes.c_int
        lib.shim_write.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p, ctypes.c_size_t]
        lib.shim_write.restype = ctypes.c_int
        lib.shim_threshold.restype = ctypes.c_uint
        lib.shim_pd.restype = ctypes.POINTER(ctypes.c_uint8 * 3)
        lib.shim_init()
        self.generate()
        info = json.loads(self.run_cli('inspect', self.xml).stdout)
        for sample, valid in [(5100, True), (4900, True), (4800, True), (12345, False)]:
            output = lib.shim_sample(sample, 1 if valid else 0)
            wire = int.from_bytes(bytes(lib.shim_pd().contents), 'big')
            fields = info['processData'][0]['in']['fields']
            values = [(wire >> f['bitOffset']) & ((1 << f['bitLength']) - 1) for f in fields]
            self.assertEqual(values, [sample, int(valid), int(output)])
        table = {v['index']: v['bitLength'] for v in iodd_gen.parse_od_source(self.od)}
        for variable in info['variables']:
            index = variable['index']
            if variable['accessRights'] != 'wo':
                data = (ctypes.c_uint8 * 8)()
                length = ctypes.c_size_t(8)
                error = lib.shim_read(index, 0, data, 8, ctypes.byref(length))
                self.assertEqual(error, 0)
                self.assertEqual(length.value, table[index] // 8)
                self.assertEqual(int.from_bytes(bytes(data[:length.value]), 'big'),
                                 int(variable['defaultValue']))
        lib.shim_sample(5432, 1)
        teach = (ctypes.c_uint8 * 1)(1)
        self.assertEqual(lib.shim_write(259, 0, teach, 1), 0)
        self.assertEqual(lib.shim_threshold(), 5432)
        self.assertNotEqual(lib.shim_write(259, 1, teach, 1), 0)

    @unittest.skipUnless(os.environ.get('IODD_SCHEMA'), 'set IODD_SCHEMA to an externally obtained official IODD1.1.xsd')
    def test_examples_validate_against_official_schema(self):
        for name, stem in (('reference_device', 'device'), ('switching_sensor', 'device'),
                           ('conformance_device', 'device')):
            args = ['generate', ROOT / f'examples/{name}/' / (stem + '.json'), '-o', self.xml]
            if name in OD_SOURCES:
                args += ['--od', OD_SOURCES[name]]
            self.run_cli(*args)
            self.run_cli('validate', self.xml, '--schema', os.environ['IODD_SCHEMA'])

if __name__ == '__main__':
    unittest.main()
