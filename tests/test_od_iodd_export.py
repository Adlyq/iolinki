#!/usr/bin/env python3
"""对象字典 → IODD 导出契约。

参数的事实源是 C 对象字典表，IODD 只是导出物。本测试锁定
examples/switching_sensor 的 C 表与随附 IODD 在参数语义上一致，
并确认生成器确实使用表项（而不是任何 JSON 变量）生成每个属性。
"""
import json
from pathlib import Path
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))

import iodd_gen  # noqa: E402

XSI_TYPE = '{http://www.w3.org/2001/XMLSchema-instance}type'


def strip_namespaces(root):
    for element in root.iter():
        element.tag = element.tag.split('}')[-1]


class OdIoddExport(unittest.TestCase):
    def setUp(self):
        self.od_source = ROOT / 'examples/switching_sensor/switching_sensor_od.c'
        self.config_path = ROOT / 'examples/switching_sensor/device.json'
        self.shipped_xml = ROOT / 'examples/switching_sensor/device.xml'
        self.variables = iodd_gen.parse_od_source(self.od_source)
        self.config = json.loads(self.config_path.read_text())

    def _shipped(self):
        root = ET.fromstring(self.shipped_xml.read_bytes())
        strip_namespaces(root)
        texts = {node.get('id'): node.get('value') for node in root.iter('Text')}
        variables = {}
        for node in root.iter('Variable'):
            datatype = node.find('Datatype')
            entry = {
                'accessRights': node.get('accessRights'),
                'defaultValue': node.get('defaultValue'),
                'datatype': datatype.get(XSI_TYPE),
                'bitLength': int(datatype.get('bitLength')),
                'name': texts[node.find('Name').get('textId')],
            }
            single = datatype.find('SingleValue')
            if single is not None:
                entry['lowerValue'] = entry['upperValue'] = int(single.get('value'))
            else:
                value_range = datatype.find('ValueRange')
                entry['lowerValue'] = int(value_range.get('lowerValue'))
                entry['upperValue'] = int(value_range.get('upperValue'))
            variables[int(node.get('index'))] = entry
        return variables

    def _variable(self, root, index):
        for variable in root.iter('Variable'):
            if variable.get('index') == str(index):
                return variable
        self.fail(f'variable index {index} missing from the generated IODD')

    def test_c_table_matches_the_shipped_iodd(self):
        shipped = self._shipped()
        self.assertEqual(sorted(shipped), [v['index'] for v in self.variables])
        for table in self.variables:
            msg = f'index {table["index"]}'
            iodd = shipped[table['index']]
            self.assertEqual(table['name'], iodd['name'], msg)
            self.assertEqual(table['datatype'], iodd['datatype'], msg)
            self.assertEqual(table['bitLength'], iodd['bitLength'], msg)
            self.assertEqual(table['accessRights'], iodd['accessRights'], msg)
            expected_default = table.get('defaultValue')
            expected_default = None if expected_default is None else str(expected_default)
            self.assertEqual(expected_default, iodd['defaultValue'], msg)
            self.assertEqual(table['lowerValue'], iodd['lowerValue'], msg)
            self.assertEqual(table['upperValue'], iodd['upperValue'], msg)

    def test_generator_exports_table_variables(self):
        xml = iodd_gen.generate_bytes(self.config, iodd_gen.load_od_variables([self.od_source]))
        root = ET.fromstring(xml)
        strip_namespaces(root)
        for table in self.variables:
            msg = f'index {table["index"]}'
            variable = self._variable(root, table['index'])
            self.assertEqual(variable.get('accessRights'), table['accessRights'], msg)
            datatype = variable.find('Datatype')
            self.assertIsNotNone(datatype, msg)
            self.assertEqual(datatype.get(XSI_TYPE), table['datatype'], msg)
            self.assertEqual(int(datatype.get('bitLength')), table['bitLength'], msg)
            if 'defaultValue' in table:
                self.assertEqual(variable.get('defaultValue'), str(table['defaultValue']), msg)
            else:
                self.assertIsNone(variable.get('defaultValue'), msg)
            if table['lowerValue'] == table['upperValue']:
                single = datatype.find('SingleValue')
                self.assertIsNotNone(single, msg)
                self.assertEqual(int(single.get('value')), table['lowerValue'], msg)
            else:
                value_range = datatype.find('ValueRange')
                self.assertIsNotNone(value_range, msg)
                self.assertEqual(int(value_range.get('lowerValue')), table['lowerValue'], msg)
                self.assertEqual(int(value_range.get('upperValue')), table['upperValue'], msg)

    def test_json_variables_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'C object dictionary'):
            iodd_gen.generate_bytes(dict(self.config, variables=[]), [])

    def test_duplicate_index_is_rejected(self):
        table = 'IOLINK_OD_SCALAR(%s, IOLINK_OD_DT_U8, IOLINK_OD_ACCESS_RO, ' \
                'IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_VOLATILE, ' \
                'IOLINK_OD_HANDLER_BACKEND, 0U, 1U, 0U, "%s"),\n'
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'duplicate.c'
            path.write_text('#define IDX 0x0100U\n' + table % ('IDX', 'A') + table % ('IDX', 'B'))
            with self.assertRaisesRegex(ValueError, 'duplicate variable index'):
                iodd_gen.parse_od_source(path)


if __name__ == '__main__':
    unittest.main()
