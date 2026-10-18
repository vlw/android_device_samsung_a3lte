#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Validate profiles against captured native HAL capabilities, without a device.

Optionally pass a stock media_profiles.xml to verify all stock recording profiles
and image-quality declarations are preserved exactly.
"""
import argparse
import json
from pathlib import Path
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stock', type=Path)
    args = parser.parse_args()
    device = Path(__file__).resolve().parents[2]
    root = ET.parse(device / 'configs/media_profiles_V1_0.xml').getroot()
    native = json.loads((Path(__file__).parent / 'fixtures/a300fu-capabilities.json').read_text())
    stock = ET.parse(args.stock).getroot() if args.stock else None
    video_caps = {cap.get('name'): cap for cap in root.findall('VideoEncoderCap')}
    audio_caps = {cap.get('name'): cap for cap in root.findall('AudioEncoderCap')}
    cameras = root.findall('CamcorderProfiles')
    assert [cam.get('cameraId') for cam in cameras] == ['0', '1']
    for cam in cameras:
        camera_id = cam.get('cameraId')
        native_sizes = set(native['cameras'][camera_id]['video-size-values'].split(','))
        profiles = cam.findall('EncoderProfile')
        names = [profile.get('quality') for profile in profiles]
        assert len(names) == len(set(names)), f'camera {camera_id}: duplicate quality'
        for profile in profiles:
            video, audio = profile.find('Video'), profile.find('Audio')
            size = f"{video.get('width')}x{video.get('height')}"
            assert size in native_sizes, f'camera {camera_id}: unsupported video size {size}'
            assert video.get('frameRate') == '30'
            cap = video_caps[video.get('codec')]
            for attribute, minimum, maximum in [
                ('width', 'minFrameWidth', 'maxFrameWidth'),
                ('height', 'minFrameHeight', 'maxFrameHeight'),
                ('frameRate', 'minFrameRate', 'maxFrameRate'),
                ('bitRate', 'minBitRate', 'maxBitRate'),
            ]:
                assert int(cap.get(minimum)) <= int(video.get(attribute)) <= int(cap.get(maximum))
            cap = audio_caps[audio.get('codec')]
            for attribute, minimum, maximum in [
                ('bitRate', 'minBitRate', 'maxBitRate'),
                ('sampleRate', 'minSampleRate', 'maxSampleRate'),
                ('channels', 'minChannels', 'maxChannels'),
            ]:
                assert int(cap.get(minimum)) <= int(audio.get(attribute)) <= int(cap.get(maximum))
            if not profile.get('quality').startswith('timelapse'):
                timelapse = cam.find(f"EncoderProfile[@quality='timelapse{profile.get('quality')}']")
                assert timelapse is not None
                assert timelapse.find('Video').attrib == video.attrib
        for quality, size in [('low', '176x144'), ('high', '1920x1080'), ('1080p', '1920x1080')]:
            video = cam.find(f"EncoderProfile[@quality='{quality}']/Video")
            assert f"{video.get('width')}x{video.get('height')}" == size
        if stock is not None:
            reference = stock.find(f"CamcorderProfiles[@cameraId='{camera_id}']")
            assert cam.attrib == reference.attrib
            for profile in reference.findall('EncoderProfile'):
                actual = cam.find(f"EncoderProfile[@quality='{profile.get('quality')}']")
                assert actual is not None and actual.attrib == profile.attrib
                assert actual.find('Video').attrib == profile.find('Video').attrib
                assert actual.find('Audio').attrib == profile.find('Audio').attrib
            assert [el.attrib for el in cam.findall('ImageEncoding')] == [
                el.attrib for el in reference.findall('ImageEncoding')]
        print(f'PASS camera {camera_id}: {len(profiles)} valid profiles, native size and codec limits')
    if stock is not None:
        print(f'PASS all stock profiles preserved: {args.stock}')


if __name__ == '__main__':
    main()
