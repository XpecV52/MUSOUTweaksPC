"""Create (or reuse) a GitHub release and upload the packaged asset.

``gh`` is not available here, so this talks to the REST API directly. The token
comes from the git credential helper, the same one ``git push`` uses.
"""
from pathlib import Path
import argparse
import json
import subprocess
import sys
import urllib.error
import urllib.request

REPO = 'XpecV52/MUSOUTweaksPC'
ROOT = Path(__file__).resolve().parent
API = 'https://api.github.com'


def token():
    out = subprocess.run(['git', 'credential', 'fill'],
                         input='protocol=https\nhost=github.com\n\n',
                         capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        if line.startswith('password='):
            return line.split('=', 1)[1]
    raise SystemExit('no github token in the credential helper')


def call(method, url, tok, body=None, raw=None, content_type='application/json', missing_ok=False):
    data = raw if raw is not None else (json.dumps(body).encode() if body else None)
    req = urllib.request.Request(url, data=data, method=method, headers={
        'Authorization': 'Bearer ' + tok,
        'Accept': 'application/vnd.github+json',
        'User-Agent': 'MUSOUTweaksPC-release',
        'Content-Type': content_type,
    })
    try:
        with urllib.request.urlopen(req) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        if missing_ok and error.code == 404:
            return None
        detail = error.read().decode('utf-8', 'replace')
        raise SystemExit(f'{method} {url} -> {error.code}\n{detail}')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('version', help='release tag, e.g. V1.0.1')
    parser.add_argument('--asset', help='file to upload (default MUSOUTweaksPC_<version>.zip)')
    parser.add_argument('--notes', help='markdown file with the release body')
    parser.add_argument('--prerelease', action='store_true')
    args = parser.parse_args()

    tag = args.version if args.version.startswith('v') else 'v' + args.version.lstrip('V')
    name = 'MUSOUTweaksPC_V' + args.version.upper().lstrip('V')
    asset = Path(args.asset) if args.asset else ROOT / '_rel' / f'{name}.zip'
    if not asset.is_file():
        raise SystemExit(f'missing asset: {asset}')
    body = Path(args.notes).read_text(encoding='utf-8') if args.notes else ''

    tok = token()
    release = call('GET', f'{API}/repos/{REPO}/releases/tags/{tag}', tok, missing_ok=True)
    if release:
        print(f'reusing release {tag} ({release["html_url"]})')
    else:
        release = call('POST', f'{API}/repos/{REPO}/releases', tok, {
            'tag_name': tag,
            'name': name,
            'body': body,
            'draft': False,
            'prerelease': args.prerelease,
        })
        print(f'created release {tag} ({release["html_url"]})')

    for old in release['assets']:
        if old['name'] == asset.name:
            call('DELETE', f'{API}/repos/{REPO}/releases/assets/{old["id"]}', tok)
            print(f'deleted previous asset {old["name"]}')

    upload = release['upload_url'].split('{')[0]
    info = call('POST', f'{upload}?name={asset.name}', tok,
                raw=asset.read_bytes(), content_type='application/zip')
    print(f'uploaded {info["name"]} {info["size"]} bytes')
    print(info['browser_download_url'])


if __name__ == '__main__':
    sys.exit(main())
