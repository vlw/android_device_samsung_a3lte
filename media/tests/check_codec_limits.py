#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check A3 XML/sample consistency, optionally against a live public-API dump."""
import argparse
import json
import math
from pathlib import Path
import xml.etree.ElementTree as ET


def check(live=None):
    base = Path(__file__).resolve().parent.parent
    samples = json.loads((base / 'tests/codec-performance-samples.json').read_text())['samples']
    grouped = {}
    for s in samples:
        assert s['frames'] >= 60 and s['fps'] > 0, s
        key = (s['codec'], s['width'], s['height'])
        grouped.setdefault(key, []).append(s['fps'])
    entries = {}
    measured = {}
    for name in ['media_codecs_performance.xml', 'media_codecs_performance_c2.xml']:
        root = ET.parse(base / name).getroot()
        for section in ['Decoders', 'Encoders']:
            for codec in root.find(section):
                name = codec.attrib['name']
                assert name not in entries, name
                assert codec.attrib['update'] == 'true', name
                entries[name] = (section, codec)
                keys = set()
                for limit in codec:
                    key = limit.attrib['name']
                    assert key not in keys, (name, key)
                    keys.add(key)
                    if key.startswith('measured-frame-rate-'):
                        w, h = map(int, key.removeprefix('measured-frame-rate-').split('x'))
                        expected = grouped[(name, w, h)]
                        lo, hi = map(int, limit.attrib['range'].split('-'))
                        assert (lo, hi) == (math.floor(min(expected)), math.floor(max(expected))), (name,key)
                        measured[(name, w, h)] = (lo, hi)
                if name.endswith('.secure'):
                    assert not any(k.startswith('measured-') for k in keys), name
                else:
                    assert any(k.startswith('measured-') for k in keys), name
    assert len(entries) == 22, len(entries)
    assert {key[0] for key in grouped} == {n for n in entries if not n.endswith('.secure')}
    assert set(measured) == set(grouped)
    if live:
        actual = {c['name']: c for c in json.loads(Path(live).read_text()) if not c['name'].endswith('.secure')}
        assert set(actual) == set(entries), (set(actual) - set(entries), set(entries) - set(actual))
        for name, (section, codec) in entries.items():
            c = actual[name]
            assert c['encoder'] == (section == 'Encoders')
            limits = {x.attrib['name']: x.attrib for x in codec}
            lo, hi = map(int, limits['frame-rate']['range'].split('-'))
            assert c['frameRate'][0] >= lo and c['frameRate'][1] <= hi, (name,c['frameRate'])
            if 'size' in limits:
                w,h = map(int,limits['size']['max'].split('x'))
                assert c['width'][1] <= w and c['height'][1] <= h, (name,c['width'],c['height'])
            for size in c['sizes']:
                key = name,size['width'],size['height']
                if key in measured:
                    assert size['supported'], key
                    got = size.get('achievableRates')
                    assert got is not None, key
                    assert all(abs(a-b) < .001 for a,b in zip(got,measured[key])), (key,got,measured[key])
                if name.startswith('c2.') and size['width'] == 1920 and size['height'] == 1920:
                    assert not size['supported'], name
        assert set(measured) <= {(n,s['width'],s['height']) for n,c in actual.items() for s in c['sizes']}
    print(f'PASS: {len(entries)} components, {len(grouped)} measured sizes, {len(samples)} samples' + ('; public API matches' if live else ''))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--live', help='JSON stdout of VideoCapabilitiesProbe on device')
    check(parser.parse_args().live)
