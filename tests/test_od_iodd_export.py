#!/usr/bin/env python3
"""对象字典 → IODD 导出契约。

P4/D1：参数的事实源是 C 对象字典表，IODD 只是导出物。本测试锁定
examples/switching_sensor 的 C 表与既有 IODD 描述在参数语义上一致，
并确认生成器确实使用表项而不是 JSON 变量。
"""
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))

import iodd_gen  # noqa: E402


class OdIoddExport(unittest.TestCase):
    def setUp(self):
        self.od_source = ROOT / 'examples/switching_sensor/switching_sensor_od.c'
        self.config_path = ROOT / 'examples/switching_sensor/device.json'
        self.variables = iodd_gen.parse_od_source(self.od_source)
        self.config = json.loads(self.config_path.read_text())

    def test_c_table_describes_the_same_parameters_as_the_iodd(self):
        legacy = self.config['variables']
        self.assertEqual([v['index'] for v in self.variables], [v['index'] for v in legacy])
        for table, iodd in zip(self.variables, legacy):
            self.assertEqual(table['datatype'], iodd['datatype'])
            self.assertEqual(table['bitLength'], iodd['bitLength'])
            self.assertEqual(table['accessRights'], iodd.get('accessRights', 'rw'))
            self.assertEqual(table.get('defaultValue'), iodd.get('defaultValue'))

    def test_generator_exports_table_variables(self):
        without = json.loads(self.config_path.read_text())
        without.pop('variables')
        xml = iodd_gen.generate_bytes(without, iodd_gen.load_od_variables([self.od_source]))
        for variable in self.variables:
            self.assertIn(f'index="{variable["index"]}"'.encode(), xml)
        self.assertIn(b'<SingleValue value="1"', xml)  # teach 的 1..1 范围

    def test_duplicate_index_is_rejected(self):
        table = 'IOLINK_OD_SCALAR(%s, IOLINK_OD_DT_U8, IOLINK_OD_ACCESS_RO, ' \
                'IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_VOLATILE, ' \
                'IOLINK_OD_HANDLER_BACKEND, 0U, 1U, 0U, "%s"),\n'
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'duplicate.c'
            path.write_text('#define IDX 0x0100U\n' + table % ('IDX', 'A') + table % ('IDX', 'B'))
            with self.assertRaises(ValueError):
                iodd_gen.parse_od_source(path)


if __name__ == '__main__':
    unittest.main()
