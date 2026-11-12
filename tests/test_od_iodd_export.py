#!/usr/bin/env python3
"""对象字典 → IODD 导出契约。

P4/D1：参数的事实源是 C 对象字典表，IODD 只是导出物。本测试锁定
examples/switching_sensor 的 C 表与既有 IODD 描述在参数语义上一致，
并确认生成器确实使用表项（而不是 JSON 变量）生成每个变量的完整属性。
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


class OdIoddExport(unittest.TestCase):
    def setUp(self):
        self.od_source = ROOT / 'examples/switching_sensor/switching_sensor_od.c'
        self.config_path = ROOT / 'examples/switching_sensor/device.json'
        self.variables = iodd_gen.parse_od_source(self.od_source)
        self.config = json.loads(self.config_path.read_text())

    def _variable(self, root, index):
        for variable in root.iter('Variable'):
            if variable.get('index') == str(index):
                return variable
        self.fail(f'variable index {index} missing from the generated IODD')

    def test_c_table_describes_the_same_parameters_as_the_iodd(self):
        legacy = self.config['variables']
        self.assertEqual([v['index'] for v in self.variables], [v['index'] for v in legacy])
        for table, iodd in zip(self.variables, legacy):
            msg = f'index {table["index"]}'
            self.assertEqual(table['name'], iodd['name'], msg)
            self.assertEqual(table['datatype'], iodd['datatype'], msg)
            self.assertEqual(table['bitLength'], iodd['bitLength'], msg)
            self.assertEqual(table['accessRights'], iodd.get('accessRights', 'rw'), msg)
            self.assertEqual(table.get('defaultValue'), iodd.get('defaultValue'), msg)
            self.assertEqual(table['lowerValue'], iodd['lowerValue'], msg)
            self.assertEqual(table['upperValue'], iodd['upperValue'], msg)

    def test_generator_exports_table_variables(self):
        without = json.loads(self.config_path.read_text())
        without.pop('variables')
        xml = iodd_gen.generate_bytes(without, iodd_gen.load_od_variables([self.od_source]))
        root = ET.fromstring(xml)
        # IODD 文档使用默认命名空间；去掉后按本地名遍历。
        for element in root.iter():
            element.tag = element.tag.split('}')[-1]
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
