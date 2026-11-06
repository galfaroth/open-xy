"""Keep Open-XY Mods by Veff in step with Open-XY.

Run hourly by .github/workflows/veff-upstream-sync.yml, or by hand.

- An upstream change that touches only the catalog (new or updated synths,
  effects, players and extensions) is merged into veff/staging right away.
- An SDK change within the Open-XY version (VERSION) already tested with Linear by Veff
  on an OP-XY is merged right away too, once it merges (profile patches and its pinned
  digest are combined automatically) and passes `--check` (CI runs `sdk.py check`).
- A new Open-XY version, or a change that does not merge or check cleanly, goes to
  veff/upstream-candidate: the installer is marked "in testing" and an issue says what
  changed. After testing Linear by Veff with it on an OP-XY,
  `python tools/veff_sync.py --accept` releases it and records that version as tested.

The script only moves source. Firmware never enters it: kits are built and verified
where the OP-XY OS file is, outside every checkout.
"""
import argparse
import datetime
import hashlib
import json
import re
import shlex
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
STATUS = ROOT / 'builder/sync-status.json'
BRANCH = 'veff/staging'
CANDIDATE = 'veff/upstream-candidate'
CATALOG_ONLY = {'catalog/machines.json', 'catalog/extensions.json'}
ISSUE_TITLE = 'Open-XY SDK update in testing'
PROFILE = 'profiles/opxy-1.2.1.json'
CORE = 'builder/core.mjs'
PIN = re.compile(r"^export const PROFILE_SHA256 = '[0-9a-f]{64}';$")


def git(*args, check=True):
    result = subprocess.run(['git', *args], cwd=ROOT, text=True, capture_output=True)
    if check and result.returncode:
        sys.exit('git ' + ' '.join(args) + ' failed:\n' + result.stdout + result.stderr)
    return result.stdout.strip()


def gh(*args):
    return subprocess.run(['gh', *args], cwd=ROOT, text=True, capture_output=True)


def read_status():
    try:
        return json.loads(STATUS.read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return {}


def write_status(status, message, dry_run):
    text = json.dumps(status, indent=2) + '\n'
    if STATUS.exists() and STATUS.read_text(encoding='utf-8') == text:
        return False
    if dry_run:
        print('Would commit status:', text)
        return False
    STATUS.write_text(text, encoding='utf-8')
    git('add', str(STATUS.relative_to(ROOT)))
    git('commit', '-q', '-m', message)
    return True


def today():
    return datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%d')


def changes(base, upstream):
    return [p for p in git('diff', '--name-only', base, upstream).splitlines() if p]


def merge(upstream, message):
    """Merge upstream into the checked-out branch; on a conflict leave it untouched."""
    result = subprocess.run(['git', 'merge', '--no-ff', '--no-edit', '-m', message, upstream],
                            cwd=ROOT, text=True, capture_output=True)
    if result.returncode:
        git('merge', '--abort', check=False)
        return False
    return True


def version_at(ref):
    return git('show', ref + ':VERSION', check=False).strip() or None


def show_json(ref, path):
    return json.loads(git('show', ref + ':' + path))


def merged_profile(base, ours, theirs):
    """Upstream's profile plus the fork's own patches (Linear by Veff), or None when they collide."""
    key = lambda patch: json.dumps(patch, sort_keys=True)
    base_patches = {key(x) for x in base['patches']}
    ours_keys = {key(x) for x in ours['patches']}
    if any(key(x) not in ours_keys for x in base['patches']):
        return None  # the fork changed or dropped an upstream patch: a person decides
    merged = dict(theirs)
    for name, value in ours.items():
        if name == 'patches':
            continue
        if name not in base and name not in theirs:
            merged[name] = value
        elif value != base.get(name):
            if theirs.get(name) != base.get(name) and theirs.get(name) != value:
                return None  # both sides changed the same setting
            merged[name] = value
    merged['patches'] = theirs['patches'] + [x for x in ours['patches'] if key(x) not in base_patches]
    ranges = sorted((int(x['file_offset'], 16), int(x['file_offset'], 16) + x['length'])
                    for x in merged['patches'] if 'file_offset' in x)
    if any(b[0] < a[1] for a, b in zip(ranges, ranges[1:])):
        return None  # two patches would edit the same bytes
    return merged


def merge(upstream, message):
    """Merge upstream into the checked-out branch, resolving the fork's known overlaps:
    the profile's patch list and the digest builder/core.mjs pins. Anything else that
    conflicts leaves the branch untouched."""
    head = git('rev-parse', 'HEAD')
    result = subprocess.run(['git', 'merge', '--no-ff', '--no-commit', upstream], cwd=ROOT, text=True, capture_output=True)
    conflicted = set(git('diff', '--name-only', '--diff-filter=U').splitlines())
    if result.returncode and (not conflicted or not conflicted <= {PROFILE, CORE}):
        git('merge', '--abort', check=False)
        return False
    if PROFILE in conflicted:
        base = git('merge-base', head, upstream)
        profile = merged_profile(show_json(base, PROFILE), show_json(head, PROFILE), show_json(upstream, PROFILE))
        if profile is None:
            git('merge', '--abort', check=False)
            return False
        (ROOT / PROFILE).write_text(json.dumps(profile, indent=2) + '\n', encoding='utf-8')
    # The pinned digest always follows the merged profile, conflict or not.
    if True:
        digest = hashlib.sha256((ROOT / PROFILE).read_bytes()).hexdigest()
        text = (ROOT / CORE).read_text(encoding='utf-8')
        hunks = re.findall(r'<<<<<<< [^\n]*\n(.*?)=======\n(.*?)>>>>>>> [^\n]*\n', text, re.S)
        if any(not all(PIN.match(line) for line in (a + b).splitlines()) for a, b in hunks):
            git('merge', '--abort', check=False)
            return False
        text = re.sub(r'<<<<<<< [^\n]*\n.*?>>>>>>> [^\n]*\n', "export const PROFILE_SHA256 = '" + digest + "';\n", text, flags=re.S)
        text = re.sub(r"^export const PROFILE_SHA256 = '[0-9a-f]{64}';$", "export const PROFILE_SHA256 = '" + digest + "';", text, flags=re.M)
        if text != (ROOT / CORE).read_text(encoding='utf-8'):
            (ROOT / CORE).write_text(text, encoding='utf-8')
        git('add', PROFILE, CORE)
    git('commit', '-q', '--no-edit', '-m', message + ('\n\nProfile: Open-XY\'s patches plus the fork\'s; digest re-pinned.' if conflicted else ''))
    return True


def checked(args, head):
    """Run the configured checks on the merge; on failure return to head."""
    if not args.check:
        return True
    result = subprocess.run(shlex.split(args.check), cwd=ROOT)
    if result.returncode:
        git('reset', '-q', '--hard', head)
        return False
    return True


def issue(body, close=False, dry_run=False):
    if dry_run:
        print(('Would close' if close else 'Would open or update') + ' issue:', ISSUE_TITLE)
        return
    found = gh('issue', 'list', '--state', 'open', '--search', 'in:title "' + ISSUE_TITLE + '"',
               '--json', 'number,title', '--jq', '.[] | select(.title == "' + ISSUE_TITLE + '") | .number')
    number = found.stdout.split()[0] if found.returncode == 0 and found.stdout.split() else None
    if close:
        if number:
            gh('issue', 'close', number, '--comment', body)
        return
    if number:
        gh('issue', 'edit', number, '--body', body)
    else:
        gh('issue', 'create', '--title', ISSUE_TITLE, '--body', body)


def sync(args):
    upstream = git('rev-parse', args.upstream)
    base = git('merge-base', 'HEAD', upstream)
    status = read_status()
    tested = status.get('tested')
    if base == upstream:
        if status.get('state') != 'in-testing':
            write_status({'format': 'veff-sync/1', 'state': 'up-to-date', 'upstream': upstream, 'tested': tested},
                         'Sync status: up to date with Open-XY ' + upstream[:7], args.dry_run)
        print('Up to date with', args.upstream, upstream[:7])
        return
    paths = changes(base, upstream)
    commits = git('log', '--oneline', '--no-merges', base + '..' + upstream).splitlines()
    version = version_at(upstream)
    catalog_only = set(paths) <= CATALOG_ONLY
    same_version = bool(tested) and version == tested
    reason = 'version'
    if catalog_only or same_version:
        kind = 'Catalog-only change' if catalog_only else 'SDK change within tested Open-XY ' + str(version)
        print(kind + ':', len(commits), 'commits; merging into', BRANCH)
        if args.dry_run:
            return
        head = git('rev-parse', 'HEAD')
        if merge(upstream, 'Merge Open-XY staging ' + upstream[:7] + (' (machines)' if catalog_only else ' (' + str(version) + ')')):
            if catalog_only or checked(args, head):
                write_status({'format': 'veff-sync/1', 'state': 'up-to-date', 'upstream': upstream, 'tested': tested},
                             'Sync status: up to date with Open-XY ' + upstream[:7], False)
                push(args, BRANCH)
                issue('Open-XY ' + upstream[:7] + ' merged automatically (' + kind.lower() + ').', close=True)
                return
            reason = 'checks'
        else:
            reason = 'conflict'
    # A new Open-XY version, or a change that did not merge or check cleanly: prepare the candidate.
    print('Holding Open-XY', upstream[:7], '(' + {'version': 'new version ' + str(version), 'conflict': 'does not merge cleanly',
                                               'checks': 'checks fail'}[reason] + '); preparing', CANDIDATE)
    head = git('rev-parse', 'HEAD')
    clean = True
    if not args.dry_run:
        remote_candidate = args.remote + '/' + CANDIDATE
        git('fetch', '-q', args.remote, CANDIDATE, check=False)
        prepared = subprocess.run(['git', 'merge-base', '--is-ancestor', upstream, remote_candidate],
                                  cwd=ROOT, capture_output=True).returncode == 0
        if prepared:
            print(CANDIDATE, 'already carries', upstream[:7], '(prepared by hand); leaving it')
        else:
            git('checkout', '-q', '-B', CANDIDATE, head)
            clean = merge(upstream, 'Merge Open-XY staging ' + upstream[:7] + ' for testing')
            if clean:
                push(args, CANDIDATE, force=True)
            git('checkout', '-q', BRANCH)
    pending = status.get('pending') or {}
    since = pending.get('since') if status.get('state') == 'in-testing' else None
    new_status = {'format': 'veff-sync/1', 'state': 'in-testing', 'upstream': base, 'tested': tested,
                  'pending': {'commit': upstream, 'version': version, 'reason': reason,
                              'since': since or today(), 'merges': clean}}
    if write_status(new_status, 'Sync status: Open-XY ' + upstream[:7] + ' in testing', args.dry_run):
        push(args, BRANCH)
    sdk = [p for p in paths if p not in CATALOG_ONLY]
    issue('\n'.join([
        'Open-XY `staging` moved to [`' + upstream[:7] + '`](https://github.com/etcetc000/open-xy/commit/' + upstream + ')'
        + {'version': ', Open-XY ' + str(version) + ' (tested: ' + str(tested) + '), so Linear by Veff is tested with it on an OP-XY before release.',
           'conflict': ': it does not merge into `' + BRANCH + '` on its own.',
           'checks': ': it merges, but `' + str(args.check) + '` fails.'}[reason],
        '',
        ('`' + CANDIDATE + '` merges it cleanly: build a kit from that branch and test it.' if clean else
         '**It does not merge cleanly into `' + BRANCH + '`:** resolve the conflicts by hand before testing.'),
        'After the hardware test: `python tools/veff_sync.py --accept` (releases it and closes this issue).',
        '',
        '### Commits (' + str(len(commits)) + ')', *['- ' + c for c in commits[:60]],
        *(['- …'] if len(commits) > 60 else []),
        '', '### Changed outside the catalog (' + str(len(sdk)) + ')', *['- `' + p + '`' for p in sdk[:80]],
        *(['- …'] if len(sdk) > 80 else []),
    ]), dry_run=args.dry_run)


def accept(args):
    """After the hardware test: release the tested upstream revision."""
    status = read_status()
    pending = (status.get('pending') or {}).get('commit')
    if status.get('state') != 'in-testing' or not pending:
        sys.exit('Nothing is in testing.')
    # The tested candidate (conflicts already resolved there) when it carries the pending revision.
    candidate = args.remote + '/' + CANDIDATE
    git('fetch', '-q', args.remote, CANDIDATE, check=False)
    tested = candidate if subprocess.run(['git', 'merge-base', '--is-ancestor', pending, candidate],
                                         cwd=ROOT, capture_output=True).returncode == 0 else pending
    print('Releasing', tested)
    if not merge(tested, 'Merge Open-XY staging ' + pending[:7] + ' (tested with Linear by Veff)'):
        sys.exit('Open-XY ' + pending[:7] + ' does not merge cleanly; resolve it by hand, then run --accept again.')
    write_status({'format': 'veff-sync/1', 'state': 'up-to-date', 'upstream': pending, 'tested': version_at(pending)},
                 'Sync status: Open-XY ' + pending[:7] + ' tested and released', args.dry_run)
    push(args, BRANCH)
    issue('Tested with Linear by Veff and released on `' + BRANCH + '`.', close=True, dry_run=args.dry_run)
    print('Released Open-XY', pending[:7], '; anything newer is checked on the next sync.')


def push(args, branch, force=False):
    if args.no_push or args.dry_run:
        print('Not pushing', branch)
        return
    git('push', *(['--force-with-lease'] if force else []), args.remote, branch + ':' + branch)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--upstream', default='upstream/staging', help='Open-XY staging ref (default upstream/staging)')
    parser.add_argument('--remote', default='origin', help='the fork remote to push to (default origin)')
    parser.add_argument('--accept', action='store_true', help='release the revision in testing, after the hardware test')
    parser.add_argument('--check', help='command a same-version SDK merge must pass, e.g. "python sdk.py check"')
    parser.add_argument('--dry-run', action='store_true', help='report only; change nothing')
    parser.add_argument('--no-push', action='store_true', help='commit locally, push nothing')
    args = parser.parse_args()
    if git('rev-parse', '--abbrev-ref', 'HEAD') != BRANCH:
        sys.exit('Run on ' + BRANCH + '.')
    if git('status', '--porcelain', '--untracked-files=no'):
        sys.exit('The checkout has uncommitted changes.')
    (accept if args.accept else sync)(args)


if __name__ == '__main__':
    main()
