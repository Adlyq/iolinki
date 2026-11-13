#!/usr/bin/env python3
"""Generate the supported IODD 1.1 subset.

参数（Variable）的事实源是 C 对象字典表：用 --od 给出包含
IOLINK_OD_*_ENTRY 宏的 C 源文件，脚本解析表项并生成 IODD 变量。JSON 只保留
对象字典无法表达的设备元数据（身份、过程数据、物理层、测试配置）。
未给出 --od 时仍接受旧的 JSON "variables" 字段，用于既有调用点兼容。"""
import argparse
import json
import datetime
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

NS = 'http://www.io-link.com/IODD/2010/10'
XSI = 'http://www.w3.org/2001/XMLSchema-instance'
ET.register_namespace('', NS)
ET.register_namespace('xsi', XSI)


def element(parent, tag, attrs=None, text=None):
    node = ET.SubElement(parent, '{' + NS + '}' + tag, attrs or {})
    if text is not None:
        node.text = str(text)
    return node


def integer(value, label, low, high):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f'{label} must be an integer from {low} to {high}')
    return value


def validate_config(config, skip_variables=False):
    if not isinstance(config, dict):
        raise ValueError('Configuration must be a JSON object')
    for key in ('processData',):
        if not isinstance(config.get(key, []), list):
            raise ValueError(key + ' must be an array')
    try:
        datetime.date.fromisoformat(config.get('releaseDate', '2026-10-02'))
    except (TypeError, ValueError):
        raise ValueError('releaseDate must be an ISO YYYY-MM-DD date') from None
    if not re.fullmatch(r'V[0-9]+(?:\.[0-9]+){1,7}', config.get('version', 'V1.0')):
        raise ValueError('version must start with V and contain decimal components')
    integer(config.get('vendorId'), 'vendorId', 1, 65535)
    integer(config.get('deviceId'), 'deviceId', 1, 16777215)
    if not config.get('vendorName') or not config.get('productName', 'Example device'):
        raise ValueError('vendorName and productName must be nonempty')
    ids = {'V_DirectParameters_1', 'V_DirectParameters_2', 'V_ProductName', 'V_ApplicationSpecificTag', 'V_ProductID', 'M_Identification', 'M_Parameter'}
    # ProductName, ProductID and ApplicationSpecificTag are declared below.
    indexes = {18, 19, 24}
    if skip_variables:
        variables = []
    else:
        variables = config.get('variables', [])
        if not isinstance(variables, list):
            raise ValueError('variables must be an array')
    def unique_id(value):
        if not isinstance(value, str) or not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_.-]*', value) or value in ids:
            raise ValueError(f'Invalid or duplicate id: {value}')
        ids.add(value)
    for variable in variables:
        unique_id(variable['id'])
        index = integer(variable['index'], 'index', 16, 65535)
        if index in indexes:
            raise ValueError(f'Duplicate variable index {index}')
        indexes.add(index)
        kind = variable.get('datatype', 'UIntegerT')
        bits = integer(variable.get('bitLength', 8), 'bitLength', 1, 1856)
        if kind not in ('UIntegerT', 'IntegerT', 'StringT', 'OctetStringT'):
            raise ValueError(f'Unsupported datatype {kind}')
        if kind in ('UIntegerT', 'IntegerT') and bits > 64:
            raise ValueError('Integer variables cannot exceed 64 bits; use StringT or OctetStringT')
        if kind in ('UIntegerT', 'IntegerT'):
            if bits < 2:
                raise ValueError('Integer variables require at least two bits')
            minimum = -(1 << (bits - 1)) if kind == 'IntegerT' else 0
            maximum = (1 << (bits - (kind == 'IntegerT'))) - 1
            lower = integer(variable.get('lowerValue', minimum), 'lowerValue', minimum, maximum)
            upper = integer(variable.get('upperValue', maximum), 'upperValue', minimum, maximum)
            if lower > upper:
                raise ValueError('lowerValue exceeds upperValue')
            if 'defaultValue' in variable:
                integer(variable['defaultValue'], 'defaultValue', lower, upper)
        if kind in ('StringT', 'OctetStringT') and bits % 8:
            raise ValueError('String/octet length must be whole bytes')
        if variable.get('accessRights', 'rw') not in ('ro', 'rw', 'wo'):
            raise ValueError('Invalid accessRights')
    data = config.get('processData', [])
    if len(data) != 1:
        raise ValueError('Exactly one unconditional processData entry is supported')
    for pd in data:
        unique_id(pd['id'])
        if not ('in' in pd or 'out' in pd):
            raise ValueError('Process data must have an input or output')
        for direction in ('in', 'out'):
            if direction not in pd:
                continue
            item = pd[direction]
            unique_id(item['id'])
            bits = integer(item['bitLength'], 'process bitLength', 1, 256)
            occupied = set()
            for field in item.get('fields', []):
                offset = integer(field['bitOffset'], 'bitOffset', 0, bits - 1)
                width = integer(field['bitLength'], 'field bitLength', 1, min(64, bits))
                kind = field.get('datatype', 'UIntegerT')
                if kind not in ('UIntegerT', 'IntegerT', 'BooleanT') or (kind == 'BooleanT' and width != 1):
                    raise ValueError('Unsupported process field datatype or width')
                positions = set(range(offset, offset + width))
                if offset + width > bits or positions & occupied:
                    raise ValueError('Process data fields overlap or exceed record size')
                occupied |= positions
    features = config.get('features', {})
    if not isinstance(features, dict) or set(features) - {'blockParameter', 'dataStorage'}:
        raise ValueError('Invalid features object')
    for key, value in features.items():
        if type(value) is not bool:
            raise ValueError(f'{key} must be a boolean')
    physical = config.get('physical', {})
    if physical.get('bitrate', 'COM2') not in ('COM1', 'COM2', 'COM3'):
        raise ValueError('Invalid bitrate')
    integer(physical.get('minCycleTime', 1000), 'minCycleTime', 1, 132800)
    integer(physical.get('mSequenceCapability', 0), 'mSequenceCapability', 0, 255)
    tests = config.get('testConfig', {})
    if not isinstance(tests, dict) or set(tests) - {'Config1', 'Config2', 'Config3'}:
        raise ValueError('Invalid testConfig')
    for name, test in tests.items():
        if not isinstance(test, dict) or set(test) != {'index', 'testValue'}:
            raise ValueError('Test configuration requires index and testValue')
        integer(test['index'], name + ' index', 256 if name == 'Config2' else 0, 65535)
        if not isinstance(test['testValue'], str) or not re.fullmatch(r'0x[0-9A-Fa-f]{2}(?:,0x[0-9A-Fa-f]{2}){0,231}', test['testValue']):
            raise ValueError('testValue requires bounded comma-separated octets')
        count = len(test['testValue'].split(','))
        if (name == 'Config1' and count > 12) or (name == 'Config3' and count <= 12):
            raise ValueError('Config1 requires <=12 octets; Config3 requires >12 octets')


# ---------------------------------------------------------------------------
# C 对象字典 → IODD 变量（C 表是事实源，IODD 是导出物）
# ---------------------------------------------------------------------------

OD_DATATYPES = {
    'IOLINK_OD_DT_U8': ('UIntegerT', 8),
    'IOLINK_OD_DT_U16': ('UIntegerT', 16),
    'IOLINK_OD_DT_U32': ('UIntegerT', 32),
    'IOLINK_OD_DT_BOOL': ('BooleanT', 1),
    'IOLINK_OD_DT_OCTET_STRING': ('OctetStringT', None),
}

OD_ACCESS = {
    'IOLINK_OD_ACCESS_RO': 'ro',
    'IOLINK_OD_ACCESS_RW': 'rw',
    'IOLINK_OD_ACCESS_RW_LOCKED': 'rw',
    'IOLINK_OD_ACCESS_WO': 'wo',
    'IOLINK_OD_ACCESS_WO_LOCKED': 'wo',
}

OD_ENTRY_RE = re.compile(r'IOLINK_OD_(SCALAR_EX|SCALAR|STRING)\s*\(', re.S)


def _strip_c_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def _split_macro_args(text, start):
    """从 start（左括号之后）切分宏实参，返回 (args, end_index)。"""
    args, depth, quoted, current = [], 1, None, []
    i = start
    while i < len(text):
        char = text[i]
        if quoted:
            current.append(char)
            if char == '\\':
                i += 1
                if i < len(text):
                    current.append(text[i])
            elif char == quoted:
                quoted = None
        elif char in ('"', "'"):
            quoted = char
            current.append(char)
        elif char == '(':
            depth += 1
            current.append(char)
        elif char == ')':
            depth -= 1
            if depth == 0:
                args.append(''.join(current).strip())
                return args, i + 1
            current.append(char)
        elif char == ',' and depth == 1:
            args.append(''.join(current).strip())
            current = []
        else:
            current.append(char)
        i += 1
    raise ValueError('unterminated IOLINK_OD entry macro')


ROOT = Path(__file__).resolve().parents[1]


def _collect_defines(path, defines, visited):
    """收集可解析的整数 #define（含被引号 include 的本地头文件）。"""
    resolved = Path(path).resolve()
    if resolved in visited or not resolved.is_file():
        return
    visited.add(resolved)
    text = _strip_c_comments(resolved.read_text())
    for include in re.findall(r'#\s*include\s+"([^"]+)"', text):
        for base in (resolved.parent, ROOT / 'include'):
            if (base / include).is_file():
                _collect_defines(base / include, defines, visited)
                break
    for name, value in re.findall(r'#\s*define\s+([A-Za-z_]\w*)\s+([^\n]+)', text):
        defines.setdefault(name, value.strip())


def _resolve_int(token, defines, depth=0):
    token = token.strip()
    while token.startswith('(') and token.endswith(')') and token.count('(') == token.count(')'):
        token = token[1:-1].strip()
    token = token.rstrip('uUlL')
    try:
        return int(token, 0)
    except ValueError:
        pass
    if depth < 8 and token in defines:
        return _resolve_int(defines[token], defines, depth + 1)
    raise ValueError(f'cannot resolve integer expression: {token}')


def _str_arg(token):
    token = token.strip()
    if not (len(token) >= 2 and token[0] == '"' and token[-1] == '"'):
        raise ValueError(f'expected a string literal, got {token}')
    return token[1:-1]


def parse_od_source(path):
    """解析 C 源文件里的 IOLINK_OD_* 表项，返回 IODD 变量描述列表。"""
    defines = {}
    _collect_defines(path, defines, set())
    text = _strip_c_comments(Path(path).read_text())
    variables, seen = [], set()
    for match in OD_ENTRY_RE.finditer(text):
        kind = match.group(1)
        args, _ = _split_macro_args(text, match.end())
        if kind == 'STRING':
            if len(args) != 7:
                raise ValueError(f'{path}: IOLINK_OD_STRING expects 7 arguments')
            index = _resolve_int(args[0], defines)
            access = OD_ACCESS.get(args[1], None)
            datatype, bits = OD_DATATYPES['IOLINK_OD_DT_OCTET_STRING']
            bits = _resolve_int(args[5], defines) * 8
            lower = upper = None
            default = None
            name = _str_arg(args[6])
        else:
            expected = 10 if kind == 'SCALAR' else 12
            if len(args) != expected:
                raise ValueError(f'{path}: IOLINK_OD_{kind} expects {expected} arguments')
            index = _resolve_int(args[0], defines)
            datatype, bits = OD_DATATYPES.get(args[1], (None, None))
            access = OD_ACCESS.get(args[2], None)
            lower, upper = _resolve_int(args[6], defines), _resolve_int(args[7], defines)
            default = _resolve_int(args[8], defines)
            name = _str_arg(args[9])
        if datatype is None:
            raise ValueError(f'{path}: unsupported datatype {args[1]}')
        if access is None:
            raise ValueError(f'{path}: unsupported access {args[2]}')
        if not 16 <= index <= 65535:
            raise ValueError(f'{path}: variable index {index} out of range')
        if index in seen:
            raise ValueError(f'{path}: duplicate variable index {index}')
        seen.add(index)
        variable = {
            'id': f'V_OD_{index:04X}',
            'index': index,
            'name': name,
            'datatype': datatype,
            'bitLength': bits,
            'accessRights': access,
        }
        if datatype == 'BooleanT':
            variable['bitLength'] = 1
            variable['lowerValue'], variable['upperValue'] = 0, 1
        elif datatype in ('UIntegerT', 'IntegerT'):
            variable['lowerValue'], variable['upperValue'] = lower, upper
            if lower <= default <= upper:
                variable['defaultValue'] = default
        variables.append(variable)
    if not variables:
        raise ValueError(f'{path}: no IOLINK_OD entry found')
    return variables


def load_od_variables(paths):
    variables = []
    for path in paths or []:
        for variable in parse_od_source(path):
            variables.append(variable)
    ids = [v['id'] for v in variables]
    if len(ids) != len(set(ids)):
        raise ValueError('duplicate OD variable ids across sources')
    indexes = [v['index'] for v in variables]
    if len(indexes) != len(set(indexes)):
        raise ValueError('duplicate OD variable indexes across sources')
    return variables


def generate_bytes(config, od_variables=None):
    validate_config(config, skip_variables=od_variables is not None)
    param_list = list(config.get('variables', [])) if od_variables is None else list(od_variables)
    root = ET.Element('{' + NS + '}IODevice', {'{' + XSI + '}schemaLocation': NS + ' IODD1.1.xsd'})
    element(root, 'DocumentInfo', {'copyright': config.get('copyright', 'iolinki-project'), 'releaseDate': config.get('releaseDate', '2026-10-02'), 'version': config.get('version', 'V1.0')})
    header = element(root, 'ProfileHeader')
    for tag, text in [('ProfileIdentification', 'IO Device Profile'), ('ProfileRevision', '1.1'), ('ProfileName', 'Device Profile for IO Devices'), ('ProfileSource', 'IO-Link Consortium'), ('ProfileClassID', 'Device')]:
        element(header, tag, text=text)
    reference = element(header, 'ISO15745Reference')
    for tag, text in [('ISO15745Part', '1'), ('ISO15745Edition', '1'), ('ProfileTechnology', 'IODD')]:
        element(reference, tag, text=text)
    body = element(root, 'ProfileBody')
    identity = element(body, 'DeviceIdentity', {key: str(config[key]) for key in ('vendorId', 'deviceId', 'vendorName')})
    texts = {}
    def textref(parent, tag, id_, value):
        if id_ in texts and texts[id_] != str(value):
            raise ValueError(f'Duplicate text id {id_}')
        texts[id_] = str(value)
        return element(parent, tag, {'textId': id_})
    product = config.get('productName', 'Example device')
    product_id = config.get('productId', 'iolinki-example-' + str(config['deviceId']))
    for tag, value in [('VendorText', config['vendorName']), ('VendorUrl', config.get('vendorUrl', 'https://iolinki.com')), ('DeviceName', product), ('DeviceFamily', config.get('deviceFamily', 'iolinki reference examples'))]:
        textref(identity, tag, 'T_' + tag, value)
    variant = element(element(identity, 'DeviceVariantCollection'), 'DeviceVariant', {'productId': product_id})
    textref(variant, 'Name', 'T_ProductName', product)
    textref(variant, 'Description', 'T_Description', config.get('description', 'Example application; replace illustrative IDs with assigned production identifiers.'))
    function = element(body, 'DeviceFunction')
    features = config.get('features', {})
    element(function, 'Features', {
        'blockParameter': str(features.get('blockParameter', False)).lower(),
        'dataStorage': str(features.get('dataStorage', False)).lower(),
    })
    variables = element(function, 'VariableCollection')
    # DirectParameters_1 is the mandatory communication-parameter block.
    element(variables, 'StdVariableRef', {'id': 'V_DirectParameters_1'})
    for id_ in ('V_DirectParameters_2', 'V_ProductName', 'V_ApplicationSpecificTag'):
        element(variables, 'StdVariableRef', {'id': id_})
    element(variables, 'StdVariableRef', {'id': 'V_ProductID', 'defaultValue': product_id})
    for variable in param_list:
        attrs = {key: str(variable[key]) for key in ('id', 'index')}
        attrs['accessRights'] = variable.get('accessRights', 'rw')
        if 'defaultValue' in variable:
            attrs['defaultValue'] = str(variable['defaultValue'])
        node = element(variables, 'Variable', attrs)
        kind = variable.get('datatype', 'UIntegerT')
        bits = variable.get('bitLength', 8)
        dtattrs = {'{' + XSI + '}type': kind}
        if kind in ('StringT', 'OctetStringT'):
            dtattrs['fixedLength'] = str(bits // 8)
            if kind == 'StringT':
                dtattrs['encoding'] = 'UTF-8'
        else:
            dtattrs['bitLength'] = str(bits)
        datatype = element(node, 'Datatype', dtattrs)
        if 'lowerValue' in variable or 'upperValue' in variable:
            lower = variable.get('lowerValue', 0)
            upper = variable.get('upperValue', (1 << bits) - 1)
            if lower == upper:
                single = element(datatype, 'SingleValue', {'value': str(lower)})
                textref(single, 'Name', 'T_' + variable['id'], variable['name'])
            else:
                element(datatype, 'ValueRange', {'lowerValue': str(lower), 'upperValue': str(upper)})
        textref(node, 'Name', 'T_' + variable['id'], variable['name'])
        if variable.get('description'):
            textref(node, 'Description', 'TD_' + variable['id'], variable['description'])
    collection = element(function, 'ProcessDataCollection')
    for pd in config['processData']:
        pdnode = element(collection, 'ProcessData', {'id': pd['id']})
        for direction, tag in [('in', 'ProcessDataIn'), ('out', 'ProcessDataOut')]:
            if direction not in pd:
                continue
            item = pd[direction]
            node = element(pdnode, tag, {'id': item['id'], 'bitLength': str(item['bitLength'])})
            fields = item.get('fields', [])
            if fields:
                record = element(node, 'Datatype', {'{' + XSI + '}type': 'RecordT', 'bitLength': str(item['bitLength'])})
                for index, field in enumerate(fields, 1):
                    child = element(record, 'RecordItem', {'subindex': str(index), 'bitOffset': str(field['bitOffset'])})
                    dtattrs = {'{' + XSI + '}type': 'BooleanT'} if field['bitLength'] == 1 else {'{' + XSI + '}type': field.get('datatype', 'UIntegerT'), 'bitLength': str(field['bitLength'])}
                    element(child, 'SimpleDatatype', dtattrs)
                    textref(child, 'Name', f'T_{item["id"]}_{index}', field['name'])
            else:
                element(node, 'Datatype', {'{' + XSI + '}type': 'OctetStringT', 'fixedLength': str((item['bitLength'] + 7) // 8)})
            textref(node, 'Name', 'T_' + item['id'], 'Process data ' + direction)
    ui = element(function, 'UserInterface')
    menus = element(ui, 'MenuCollection')
    for role in ('Identification', 'Parameter'):
        menu = element(menus, 'Menu', {'id': 'M_' + role})
        textref(menu, 'Name', 'T_Menu_' + role, role)
        refs = ['V_DirectParameters_1', 'V_ApplicationSpecificTag'] if role == 'Identification' else ['V_DirectParameters_1'] if not param_list else [v['id'] for v in param_list]
        for id_ in refs:
            element(menu, 'VariableRef', {'variableId': id_})
    for role in ('Observer', 'Maintenance', 'Specialist'):
        role_node = element(ui, role + 'RoleMenuSet')
        element(role_node, 'IdentificationMenu', {'menuId': 'M_Identification'})
        element(role_node, 'ParameterMenu', {'menuId': 'M_Parameter'})
    comm = element(root, 'CommNetworkProfile', {'{' + XSI + '}type': 'IOLinkCommNetworkProfileT', 'iolinkRevision': 'V1.1'})
    physical = config.get('physical', {})
    layer = element(element(comm, 'TransportLayers'), 'PhysicalLayer', {'bitrate': physical.get('bitrate', 'COM2'), 'minCycleTime': str(physical.get('minCycleTime', 1000)), 'sioSupported': str(physical.get('sioSupported', False)).lower(), 'mSequenceCapability': str(physical.get('mSequenceCapability', 0))})
    connection = element(layer, 'Connection', {'{' + XSI + '}type': 'M12-4ConnectionT'})
    element(connection, 'ProductRef', {'productId': product_id})
    for wire, function_name in [(1, 'L+'), (2, 'NC'), (3, 'L-'), (4, 'C/Q')]:
        element(connection, 'Wire' + str(wire), {'function': function_name})
    test = element(comm, 'Test')
    for name in ('Config1', 'Config2', 'Config3'):
        if name in config.get('testConfig', {}):
            element(test, name, {k: str(v) for k, v in config['testConfig'][name].items()})
    language = element(element(root, 'ExternalTextCollection'), 'PrimaryLanguage', {'{http://www.w3.org/XML/1998/namespace}lang': 'en'})
    for id_, value in texts.items():
        element(language, 'Text', {'id': id_, 'value': value})
    stamp = element(root, 'Stamp', {'crc': '0'})
    element(stamp, 'Checker', {'name': 'iolinki-iodd-cli', 'version': 'V1.0'})
    ET.indent(root, space='  ')
    return ET.tostring(root, encoding='utf-8', xml_declaration=True) + b'\n'


def generate_iodd(config, output=None, od_sources=None):
    data = generate_bytes(config, load_od_variables(od_sources) if od_sources else None)
    path = Path(output or config.get('output', 'device.xml'))
    path.write_bytes(data)
    subprocess.run(['node', str(Path(__file__).with_name('iodd_stamp.mjs')), 'write', str(path)], check=True)
    return path


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('config', type=Path, help='device JSON (identity/process data/physical)')
    parser.add_argument('--od', action='append', default=[], type=Path,
                        help='C source with IOLINK_OD_* entries (repeatable; replaces JSON variables)')
    parser.add_argument('-o', '--output', type=Path)
    args = parser.parse_args()
    print(generate_iodd(json.loads(args.config.read_text()), args.output, args.od))
