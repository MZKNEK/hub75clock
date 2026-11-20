#!/usr/bin/env python3
"""
Zamienia GIF na plik .g64 dla trybu GIF zegara (panel 64x64) i opcjonalnie
od razu wgrywa go na zegar (POST /gifup). To samo robi strona zegara
(zakladka Obrazy) - ten skrypt jest dla GIF-ow obrabianych na komputerze.

Uzycie:
    pip install pillow
    python narzedzia/gif2g64.py moj.gif --zegar 192.168.1.121
    python narzedzia/gif2g64.py moj.gif -o moj.g64          (tylko plik)

- GIF jest skalowany do 64x64 (z zachowaniem proporcji, czarne pasy),
  klatki skladane w pelne obrazy (disposal, przezroczystosc = czern),
  wspolna paleta do 256 kolorow (median cut).
- Format pliku: opis w gif_animation.cpp. Jedna klatka = 4 KB na partycji.
"""
import argparse
import os
import struct
import sys
import urllib.request
import uuid

from PIL import Image, ImageSequence

SIZE = 64
MAX_FRAMES = 600
NAME_BYTES = 32


def load_frames(path, smooth):
    # Pillow sam sklada klatki GIF-a w pelne obrazy (disposal, przezroczystosc);
    # przezroczyste miejsca zamieniamy na czarne tlo.
    image = Image.open(path)
    frames, delays = [], []
    for frame in ImageSequence.Iterator(image):
        composed = Image.new('RGBA', image.size, (0, 0, 0, 255))
        composed.alpha_composite(frame.convert('RGBA'))
        frames.append(fit(composed.convert('RGB'), smooth))
        duration = frame.info.get('duration', image.info.get('duration', 100)) or 100
        delays.append(max(20, min(65535, int(duration))))
    return frames, delays


def fit(img, smooth):
    if img.size == (SIZE, SIZE):
        return img
    scale = min(SIZE / img.width, SIZE / img.height)
    w, h = max(1, round(img.width * scale)), max(1, round(img.height * scale))
    resample = Image.LANCZOS if smooth else Image.NEAREST
    resized = img.resize((w, h), resample)
    out = Image.new('RGB', (SIZE, SIZE), (0, 0, 0))
    out.paste(resized, ((SIZE - w) // 2, (SIZE - h) // 2))
    return out


def quantize(frames):
    strip = Image.new('RGB', (SIZE, SIZE * len(frames)))
    for i, f in enumerate(frames):
        strip.paste(f, (0, i * SIZE))
    q = strip.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    indices = q.tobytes()
    used = max(indices) + 1
    return bytes(q.getpalette()[:used * 3]), indices


def pack(path, smooth):
    frames, delays = load_frames(path, smooth)
    if len(frames) > MAX_FRAMES:
        sys.exit(f'{path}: {len(frames)} klatek, zegar przyjmie najwyzej {MAX_FRAMES}')
    palette, indices = quantize(frames)
    name = os.path.splitext(os.path.basename(path))[0].encode('utf-8')
    while len(name) > NAME_BYTES - 1:
        name = name[:-1]
        try:
            name.decode('utf-8')
        except UnicodeDecodeError:
            continue
    header = b'G64\x01' + struct.pack('<HH', len(frames), len(palette) // 3)
    header += name.ljust(NAME_BYTES, b'\0')
    data = header + struct.pack(f'<{len(delays)}H', *delays) + palette + indices
    print(f'{path}: {len(frames)} klatek, {len(palette) // 3} kolorow, {len(data) // 1024} KB',
          file=sys.stderr)
    return data


def upload(data, address):
    boundary = uuid.uuid4().hex
    body = (f'--{boundary}\r\nContent-Disposition: form-data; name="f"; filename="gif.g64"\r\n'
            'Content-Type: application/octet-stream\r\n\r\n').encode() + data
    body += f'\r\n--{boundary}--\r\n'.encode()
    request = urllib.request.Request(f'http://{address}/gifup', data=body, method='POST',
                                     headers={'Content-Type': f'multipart/form-data; boundary={boundary}'})
    try:
        with urllib.request.urlopen(request, timeout=120) as response:
            return response.status == 200
    except urllib.error.HTTPError:
        return False


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('gifs', nargs='+')
    parser.add_argument('-o', '--output', help='zapisz .g64 (tylko przy jednym GIF-ie)')
    parser.add_argument('--zegar', help='adres zegara - wgraj od razu (np. 192.168.1.121)')
    parser.add_argument('--smooth', action='store_true',
                        help='skaluj gladko (zdjecia); domyslnie ostro (pixel art)')
    args = parser.parse_args()
    if args.output and len(args.gifs) > 1:
        sys.exit('-o tylko z jednym GIF-em')
    for path in args.gifs:
        data = pack(path, args.smooth)
        if args.output:
            with open(args.output, 'wb') as out:
                out.write(data)
        if args.zegar:
            print('  wgrany' if upload(data, args.zegar) else '  BLAD wgrywania', file=sys.stderr)


if __name__ == '__main__':
    main()
